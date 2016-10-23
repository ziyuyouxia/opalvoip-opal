/*
 * peclient.cxx
 *
 * H.323 Annex G Peer Element client protocol handler
 *
 * Open H323 Library
 *
 * Copyright (c) 2003 Equivalence Pty. Ltd.
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.0 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is Open H323 Library.
 *
 * The Initial Developer of the Original Code is Equivalence Pty. Ltd.
 *
 * Contributor(s): ______________________________________.
 *
 * $Revision$
 * $Author$
 * $Date$
 */

#include <ptlib.h>

#include <opal_config.h>

#if OPAL_H501

#ifdef __GNUC__
#pragma implementation "peclient.h"
#endif

#include <h323/peclient.h>

#include <h323/h323ep.h>
#include <h323/h323annexg.h>
#include <h323/h323pdu.h>


#define new PNEW


const unsigned ServiceRequestRetryTime       = 60;
const unsigned ServiceRequestGracePeriod     = 10;
const unsigned ServiceRelationshipTimeToLive = 60;


////////////////////////////////////////////////////////////////

H501Transaction::H501Transaction(H323PeerElement & pe, const H501PDU & pdu, PBoolean hasReject)
: H323Transaction(pe, pdu, new H501PDU, hasReject ? new H501PDU : NULL),
    requestCommon(((H501PDU &)m_request->GetPDU()).m_common),
    confirmCommon(((H501PDU &)m_confirm->GetPDU()).m_common),
    peerElement(pe)
{
}


H323TransactionPDU * H501Transaction::CreateRIP(unsigned sequenceNumber,
                                                unsigned delay) const
{
  H501PDU * rip = new H501PDU;
  rip->BuildRequestInProgress(sequenceNumber, delay);
  return rip;
}


H235Authenticator::ValidationResult H501Transaction::ValidatePDU() const
{
  return m_request->Validate(requestCommon);
}


////////////////////////////////////////////////////////////////

H501ServiceRequest::H501ServiceRequest(H323PeerElement & pe,
                                       const H501PDU & pdu)
  : H501Transaction(pe, pdu, true),
    srq((H501_ServiceRequest &)m_request->GetChoice().GetObject()),
    scf(((H501PDU &)m_confirm->GetPDU()).BuildServiceConfirmation(pdu.m_common.m_sequenceNumber)),
    srj(((H501PDU &)m_reject->GetPDU()).BuildServiceRejection(pdu.m_common.m_sequenceNumber,
                                                            H501_ServiceRejectionReason::e_undefined))
{
}


#if PTRACING
const char * H501ServiceRequest::GetName() const
{
  return "ServiceRequest";
}
#endif


void H501ServiceRequest::SetRejectReason(unsigned reasonCode)
{
  srj.m_reason.SetTag(reasonCode);
}


H323Transaction::Response H501ServiceRequest::OnHandlePDU()
{
  return peerElement.OnServiceRequest(*this);
}


////////////////////////////////////////////////////////////////

H501DescriptorUpdate::H501DescriptorUpdate(H323PeerElement & pe,
                                           const H501PDU & pdu)
  : H501Transaction(pe, pdu, false),
    du((H501_DescriptorUpdate &)m_request->GetChoice().GetObject()),
    ack(((H501PDU &)m_confirm->GetPDU()).BuildDescriptorUpdateAck(pdu.m_common.m_sequenceNumber))
{
}


#if PTRACING
const char * H501DescriptorUpdate::GetName() const
{
  return "DescriptorUpdate";
}
#endif


void H501DescriptorUpdate::SetRejectReason(unsigned /*reasonCode*/)
{
  // Not possible!
}


H323Transaction::Response H501DescriptorUpdate::OnHandlePDU()
{
  return peerElement.OnDescriptorUpdate(*this);
}


////////////////////////////////////////////////////////////////

H501AccessRequest::H501AccessRequest(H323PeerElement & pe,
                                     const H501PDU & pdu)
  : H501Transaction(pe, pdu, true),
    arq((H501_AccessRequest &)m_request->GetChoice().GetObject()),
    acf(((H501PDU &)m_confirm->GetPDU()).BuildAccessConfirmation(pdu.m_common.m_sequenceNumber)),
    arj(((H501PDU &)m_reject->GetPDU()).BuildAccessRejection(pdu.m_common.m_sequenceNumber,
                                                           H501_AccessRejectionReason::e_undefined))
{
}


#if PTRACING
const char * H501AccessRequest::GetName() const
{
  return "AccessRequest";
}
#endif


void H501AccessRequest::SetRejectReason(unsigned reasonCode)
{
  arj.m_reason.SetTag(reasonCode);
}


H323Transaction::Response H501AccessRequest::OnHandlePDU()
{
  return peerElement.OnAccessRequest(*this);
}


////////////////////////////////////////////////////////////////

H323PeerElement::H323PeerElement(H323EndPoint & ep, H323Transport * trans)
  : H323_AnnexG(ep, trans)
{
  Construct();
}

H323PeerElement::H323PeerElement(H323EndPoint & ep, const H323TransportAddress & addr)
  : H323_AnnexG(ep, addr)
{
  Construct();
}

void H323PeerElement::Construct()
{
  if (m_transport != NULL)
    m_transport->SetPromiscuous(H323Transport::AcceptFromAny);

  monitorStop       = false;
  localIdentifier   = m_endpoint.GetLocalUserName();
  basePeerOrdinal   = RemoteServiceRelationshipOrdinal;

  StartChannel();

  monitor = PThread::Create(PCREATE_NOTIFIER(MonitorMain), "PeerElementMonitor");
}

H323PeerElement::~H323PeerElement()
{
  if (monitor != NULL) {
    monitorStop = true;
    monitorTickle.Signal();
    PThread::WaitAndDelete(monitor);
  }

  StopChannel();
}


void H323PeerElement::SetLocalName(const PString & name)
{
  PWaitAndSignal m(localNameMutex);
  localIdentifier = name;
}

PString H323PeerElement::GetLocalName() const
{
  PWaitAndSignal m(localNameMutex);
  return localIdentifier;
}

void H323PeerElement::SetDomainName(const PString & name)
{
  PWaitAndSignal m(localNameMutex);
  domainName = name;
}

PString H323PeerElement::GetDomainName() const
{
  PWaitAndSignal m(localNameMutex);
  return domainName;
}


void H323PeerElement::PrintOn(ostream & strm) const
{
  if (!localIdentifier)
    strm << localIdentifier << '@';
  H323Transactor::PrintOn(strm);
}

void H323PeerElement::MonitorMain(PThread &, P_INT_PTR)
{
  PTRACE(4, "PeerElement\tBackground thread started");

  for (;;) {

    // refresh and retry remote service relationships by sending new ServiceRequests
    PTime now;
    PTime nextExpireTime = now + ServiceRequestRetryTime*1000;
    {
      for (PSafePtr<H323PeerElementServiceRelationship> sr = GetFirstRemoteServiceRelationship(PSafeReadOnly); sr != NULL; sr++) {

        if (now >= sr->m_expireTime) {
          PTRACE(3, "PeerElement\tRenewing service relationship " << sr->m_serviceID << "before expiry");
          ServiceRequestByID(sr->m_serviceID);
        }

        // get minimum sleep time for next refresh or retry
        if (sr->m_expireTime < nextExpireTime)
          nextExpireTime = sr->m_expireTime;
      }
    }

    // expire local service relationships we have not received ServiceRequests for
    {
      for (PSafePtr<H323PeerElementServiceRelationship> sr = GetFirstLocalServiceRelationship(PSafeReadOnly); sr != NULL; sr++) {

        // check to see if expired or needs refresh scheduled
        PTime expireTime = sr->m_expireTime + 1000 * ServiceRequestGracePeriod;
        if (now >= expireTime) {
          PTRACE(2, "PeerElement\tService relationship " << sr->m_serviceID << "expired");
          localServiceRelationships.Remove(sr);
          {
            PWaitAndSignal m(localPeerListMutex);
            localServiceOrdinals -= sr->m_ordinal;
          }
        }
        else if (expireTime < nextExpireTime)
          nextExpireTime = sr->m_expireTime;
      }
    }

    // if any descriptor needs updating, then spawn a thread to do it
    {
      for (PSafePtr<H323PeerElementDescriptor> descriptor = GetFirstDescriptor(PSafeReadOnly); descriptor != NULL; descriptor++) {
        PWaitAndSignal m(localPeerListMutex);
        if (
            (descriptor->state != H323PeerElementDescriptor::Clean) || 
            (
             (descriptor->creator >= RemoteServiceRelationshipOrdinal) && 
              !localServiceOrdinals.Contains(descriptor->creator)
             )
            ) {
          PThread::Create(PCREATE_NOTIFIER(UpdateAllDescriptors), 0, PThread::AutoDeleteThread, PThread::NormalPriority, "UpdateDescriptors");
          break;
        }
      }
    }

    // wait until just before the next expire time;
    PTimeInterval timeToWait = nextExpireTime - PTime();
    if (timeToWait > 60*1000)
      timeToWait = 60*1000;
    monitorTickle.Wait(timeToWait);

    if (monitorStop)
      break;
  }

  PTRACE(4, "PeerElement\tBackground thread ended");
}

void H323PeerElement::UpdateAllDescriptors(PThread &, P_INT_PTR)
{
  PTRACE(4, "PeerElement\tDescriptor update thread started");

  for (PSafePtr<H323PeerElementDescriptor> descriptor = GetFirstDescriptor(PSafeReadWrite); descriptor != NULL; descriptor++) {
    PWaitAndSignal m(localPeerListMutex);

    // delete any descriptors which belong to service relationships that are now gone
    if (
        (descriptor->state != H323PeerElementDescriptor::Deleted) &&
        (descriptor->creator >= RemoteServiceRelationshipOrdinal) && 
        !localServiceOrdinals.Contains(descriptor->creator)
       )
      descriptor->state = H323PeerElementDescriptor::Deleted;

    UpdateDescriptor(descriptor);
  }

  monitorTickle.Signal();

  PTRACE(4, "PeerElement\tDescriptor update thread ended");
}

void H323PeerElement::TickleMonitor(PTimer &, P_INT_PTR)
{
  monitorTickle.Signal();
}

///////////////////////////////////////////////////////////
//
// service relationship functions
//

H323PeerElementServiceRelationship * H323PeerElement::CreateServiceRelationship()
{
  return new H323PeerElementServiceRelationship();
}

PBoolean H323PeerElement::SetOnlyServiceRelationship(const PString & peer, PBoolean keepTrying)
{
  if (peer.IsEmpty()) {
    RemoveAllServiceRelationships();
    return true;
  }

  for (PSafePtr<H323PeerElementServiceRelationship> sr = GetFirstRemoteServiceRelationship(PSafeReadOnly); sr != NULL; sr++)
    if (sr->m_peer != peer)
      RemoveServiceRelationship(sr->m_peer);

  return AddServiceRelationship(peer, keepTrying);
}

PBoolean H323PeerElement::AddServiceRelationship(const H323TransportAddress & addr, PBoolean keepTrying)
{
  OpalGloballyUniqueID serviceID;
  return AddServiceRelationship(addr, serviceID, keepTrying);
}

PBoolean H323PeerElement::AddServiceRelationship(const H323TransportAddress & addr, OpalGloballyUniqueID & serviceID, PBoolean keepTrying)

{
  switch (ServiceRequestByAddr(addr, serviceID)) {
    case Confirmed:
    case ServiceRelationshipReestablished:
      return true;

    case NoResponse:
      if (!keepTrying)
        return false;
      break;    
    
    case Rejected:
    case NoServiceRelationship:
    default:
      return false;
  }

  PTRACE(2, "PeerElement\tRetrying ServiceRequest to " << addr << " in " << ServiceRequestRetryTime);

  // this will cause the polling routines to keep trying to establish a new service relationship
  H323PeerElementServiceRelationship * sr = CreateServiceRelationship();
  sr->m_peer = addr;
  sr->m_expireTime = PTime() + (ServiceRequestRetryTime * 1000);
  {
    PWaitAndSignal m(basePeerOrdinalMutex);
    sr->m_ordinal = basePeerOrdinal++;
  }
  {
    PWaitAndSignal m(remotePeerListMutex);
    remotePeerAddrToServiceID.SetAt(addr, sr->m_serviceID.AsString());
    remotePeerAddrToOrdinalKey.SetAt(addr, new POrdinalKey(sr->m_ordinal));
  }
  remoteServiceRelationships.Append(sr);

  monitorTickle.Signal();

  return true;
}

PBoolean H323PeerElement::RemoveServiceRelationship(const OpalGloballyUniqueID & serviceID, int reason)
{
  {
    PWaitAndSignal m(remotePeerListMutex);

    // if no service relationship exists for this peer, then nothing to do
    PSafePtr<H323PeerElementServiceRelationship> sr = remoteServiceRelationships.FindWithLock(H323PeerElementServiceRelationship(serviceID), PSafeReadOnly);
    if (sr == NULL) {
      return false;
    }
  }

  return ServiceRelease(serviceID, reason);
}


PBoolean H323PeerElement::RemoveServiceRelationship(const H323TransportAddress & peer, int reason)
{
  OpalGloballyUniqueID serviceID;

  // if no service relationship exists for this peer, then nothing to do
  {
    PWaitAndSignal m(remotePeerListMutex);
    if (!remotePeerAddrToServiceID.Contains(peer))
      return false;
    serviceID = remotePeerAddrToServiceID[peer];
  }

  return ServiceRelease(serviceID, reason);
}

PBoolean H323PeerElement::RemoveAllServiceRelationships()
{
  // if a service relationship exists for this peer, then reconfirm it
  for (PSafePtr<H323PeerElementServiceRelationship> sr = GetFirstRemoteServiceRelationship(PSafeReadOnly); sr != NULL; sr++)
    RemoveServiceRelationship(sr->m_peer);

  return true;
}

H323PeerElement::Error H323PeerElement::ServiceRequestByAddr(const H323TransportAddress & peer, OpalGloballyUniqueID & serviceID)
{
  if (PAssertNULL(m_transport) == NULL)
    return NoResponse;

  // if a service relationship exists for this peer, then reconfirm it
  remotePeerListMutex.Wait();
  if (remotePeerAddrToServiceID.Contains(peer)) {
    serviceID = remotePeerAddrToServiceID[peer];
    remotePeerListMutex.Signal();
    return ServiceRequestByID(serviceID);
  }
  remotePeerListMutex.Signal();

  // create a new service relationship
  H323PeerElementServiceRelationship * sr = CreateServiceRelationship();

  // build the service request
  H501PDU pdu;
  H323TransportAddressArray interfaces = m_endpoint.GetInterfaceAddresses(m_transport);
  H501_ServiceRequest & body = pdu.BuildServiceRequest(GetNextSequenceNumber(), interfaces);

  // include the element indentifier
  body.IncludeOptionalField(H501_ServiceRequest::e_elementIdentifier);
  body.m_elementIdentifier = localIdentifier;

  // send the request
  Request request(pdu.GetSequenceNumber(), pdu, peer);
  H501PDU reply;
  request.m_responseInfo = &reply;
  if (!MakeRequest(request))  {
    delete sr;
    switch (request.m_responseResult) {
      case Request::NoResponseReceived :
        PTRACE(2, "PeerElement\tServiceRequest to " << peer << " failed due to no response");
        return NoResponse;

      case Request::RejectReceived:
        PTRACE(2, "PeerElement\tServiceRequest to " << peer << " rejected for reason " << request.m_rejectReason);
        break;

      default:
        PTRACE(2, "PeerElement\tServiceRequest to " << peer << " refused with unknown response " << (int)request.m_responseResult);
        break;
    }
    return Rejected;
  }

  // reply must contain a service ID
  if (!reply.m_common.HasOptionalField(H501_MessageCommonInfo::e_serviceID)) {
    PTRACE(1, "PeerElement\tServiceConfirmation contains no serviceID");
    delete sr;
    return Rejected;
  }

  // create the service relationship
  H501_ServiceConfirmation & replyBody = reply.m_body;
  sr->m_peer = peer;
  sr->m_serviceID = reply.m_common.m_serviceID;
  sr->m_expireTime = PTime() + 1000 * ((replyBody.m_timeToLive < ServiceRequestRetryTime) ? (int)replyBody.m_timeToLive : ServiceRequestRetryTime);
  sr->m_lastUpdateTime = PTime();
  serviceID = sr->m_serviceID;

  {
    if (sr->m_ordinal == LocalServiceRelationshipOrdinal) {
      {
        PWaitAndSignal m(basePeerOrdinalMutex);
        sr->m_ordinal = basePeerOrdinal++;
      }
      {
        PWaitAndSignal m(remotePeerListMutex);
        remotePeerAddrToServiceID.SetAt(peer, sr->m_serviceID.AsString());
        remotePeerAddrToOrdinalKey.SetAt(peer, new POrdinalKey(sr->m_ordinal));
      }
    }
  }

  remoteServiceRelationships.Append(sr);

  PTRACE(3, "PeerElement\tNew service relationship established with " << peer << " - next update in " << replyBody.m_timeToLive);
  OnAddServiceRelationship(peer);

  // mark all descriptors as needing an update
  for (PSafePtr<H323PeerElementDescriptor> descriptor = GetFirstDescriptor(PSafeReadWrite); descriptor != NULL; descriptor++) {
    if (descriptor->state == H323PeerElementDescriptor::Clean)
      descriptor->state = H323PeerElementDescriptor::Dirty;
  }

  monitorTickle.Signal();
  return Confirmed;
}


H323PeerElement::Error H323PeerElement::ServiceRequestByID(OpalGloballyUniqueID & serviceID)
{
  if (PAssertNULL(m_transport) == NULL)
    return NoResponse;

  // build the service request
  H501PDU pdu;
  H501_ServiceRequest & body = pdu.BuildServiceRequest(GetNextSequenceNumber(), m_transport->GetLastReceivedAddress());

  // include the element indentifier
  body.IncludeOptionalField(H501_ServiceRequest::e_elementIdentifier);
  body.m_elementIdentifier = localIdentifier;

  // check to see if we have a service relationship with the peer already
  PSafePtr<H323PeerElementServiceRelationship> sr = remoteServiceRelationships.FindWithLock(H323PeerElementServiceRelationship(serviceID), PSafeReadWrite);
  if (sr == NULL)
    return NoServiceRelationship;

  // setup to update the old service relationship
  pdu.m_common.IncludeOptionalField(H501_MessageCommonInfo::e_serviceID);
  pdu.m_common.m_serviceID = sr->m_serviceID;
  Request request(pdu.GetSequenceNumber(), pdu, sr->m_peer);
  H501PDU reply;
  request.m_responseInfo = &reply;

  if (MakeRequest(request)) {
    H501_ServiceConfirmation & replyBody = reply.m_body;
    sr->m_expireTime = PTime() + 1000 * ((replyBody.m_timeToLive < ServiceRequestRetryTime) ? (int)replyBody.m_timeToLive : ServiceRequestRetryTime);
    sr->m_lastUpdateTime = PTime();
    PTRACE(3, "PeerElement\tConfirmed service relationship with " << sr->m_peer << " - next update in " << replyBody.m_timeToLive);
    return Confirmed;
  }

  // if cannot update, then try again after 60 seconds
  switch (request.m_responseResult) {
    case Request::NoResponseReceived :
      PTRACE(2, "PeerElement\tNo response to ServiceRequest - trying again in " << ServiceRequestRetryTime);
      sr->m_expireTime = PTime() + (ServiceRequestRetryTime * 1000);
      monitorTickle.Signal();
      return NoResponse;

    case Request::RejectReceived:
      switch (request.m_rejectReason) {
        case H501_ServiceRejectionReason::e_unknownServiceID:
          if (OnRemoteServiceRelationshipDisappeared(serviceID, sr->m_peer))
            return Confirmed;
          break;

        default:
          PTRACE(2, "PeerElement\tServiceRequest to " << sr->m_peer << " rejected with unknown reason " << request.m_rejectReason);
          break;
      }
      break;

    default:
      PTRACE(2, "PeerElement\tServiceRequest to " << sr->m_peer << " failed with unknown response " << (int)request.m_responseResult);
      break;
  }

  return Rejected;
}

H323Transaction::Response H323PeerElement::OnServiceRequest(H501ServiceRequest & info)
{
  info.SetRejectReason(H501_ServiceRejectionReason::e_serviceUnavailable);
  return H323Transaction::Reject;
}

H323Transaction::Response H323PeerElement::HandleServiceRequest(H501ServiceRequest & info)
{
  // if a serviceID is specified, this is should be an existing service relationship
  if (info.requestCommon.HasOptionalField(H501_MessageCommonInfo::e_serviceID)) {

    // check to see if we have a service relationship with the peer already
    OpalGloballyUniqueID serviceID(info.requestCommon.m_serviceID);
    PSafePtr<H323PeerElementServiceRelationship> sr = localServiceRelationships.FindWithLock(H323PeerElementServiceRelationship(serviceID), PSafeReadWrite);
    if (sr == NULL) {
      PTRACE(2, "PeerElement\tRejecting unknown service ID " << serviceID << " received from peer " << info.GetReplyAddress());
      info.SetRejectReason(H501_ServiceRejectionReason::e_unknownServiceID);
      return H323Transaction::Reject;
    }

    // include service ID, local and domain identifiers
    info.confirmCommon.IncludeOptionalField(H501_MessageCommonInfo::e_serviceID);
    info.confirmCommon.m_serviceID = sr->m_serviceID;
    info.scf.m_elementIdentifier = GetLocalName();
    H323SetAliasAddress(GetDomainName(), info.scf.m_domainIdentifier);

    // include time to live
    info.scf.IncludeOptionalField(H501_ServiceConfirmation::e_timeToLive);
    info.scf.m_timeToLive = ServiceRelationshipTimeToLive;
    sr->m_lastUpdateTime = PTime();
    sr->m_expireTime = PTime() + (info.scf.m_timeToLive * 1000);

    PTRACE(2, "PeerElement\tService relationship with " << sr->m_name << " at " << info.GetReplyAddress() << " updated - next update in " << info.scf.m_timeToLive);
    return H323Transaction::Confirm;
  }

  H323PeerElementServiceRelationship * sr = CreateServiceRelationship();

  // get the name of the remote element
  if (info.srq.HasOptionalField(H501_ServiceRequest::e_elementIdentifier))
    sr->m_name = info.srq.m_elementIdentifier;

  // include service ID, local and domain identifiers
  info.confirmCommon.IncludeOptionalField(H501_MessageCommonInfo::e_serviceID);
  info.confirmCommon.m_serviceID = sr->m_serviceID;
  info.scf.m_elementIdentifier = GetLocalName();
  H323SetAliasAddress(GetDomainName(), info.scf.m_domainIdentifier);

  // include time to live
  info.scf.IncludeOptionalField(H501_ServiceConfirmation::e_timeToLive);
  info.scf.m_timeToLive = ServiceRelationshipTimeToLive;
  if (info.requestCommon.HasOptionalField(H501_MessageCommonInfo::e_replyAddress) && info.requestCommon.m_replyAddress.GetSize() > 0)
    sr->m_peer = info.requestCommon.m_replyAddress[0];
  else
    sr->m_peer = m_transport->GetLastReceivedAddress();
  sr->m_lastUpdateTime = PTime();
  sr->m_expireTime = PTime() + (info.scf.m_timeToLive * 1000);
  {
    H323TransportAddress addr = m_transport->GetLastReceivedAddress();
    {
      PWaitAndSignal m(basePeerOrdinalMutex);
      sr->m_ordinal = basePeerOrdinal++;
    }
    {
      PWaitAndSignal m(localPeerListMutex);
      localServiceOrdinals += sr->m_ordinal;
    }
  }

  // add to the list of known relationships
  localServiceRelationships.Append(sr);
  monitorTickle.Signal();

  // send the response
  PTRACE(3, "PeerElement\tNew service relationship with " << sr->m_name << " at " << info.GetReplyAddress() << " created - next update in " << info.scf.m_timeToLive);
  return H323Transaction::Confirm;
}

PBoolean H323PeerElement::OnReceiveServiceRequest(const H501PDU & pdu, const H501_ServiceRequest & /*pduBody*/)
{
  H501ServiceRequest * info = new H501ServiceRequest(*this, pdu);
  if (!info->HandlePDU())
    delete info;

  return false;
}

PBoolean H323PeerElement::OnReceiveServiceConfirmation(const H501PDU & pdu, const H501_ServiceConfirmation & pduBody)
{
  if (!H323_AnnexG::OnReceiveServiceConfirmation(pdu, pduBody))
    return false;

  if (m_lastRequest->m_responseInfo != NULL)
    dynamic_cast<H501PDU &>(*m_lastRequest->m_responseInfo) = pdu;

  return true;
}

PBoolean H323PeerElement::ServiceRelease(const OpalGloballyUniqueID & serviceID, unsigned reason)
{
  // remove any previous check to see if we have a service relationship with the peer already
  PSafePtr<H323PeerElementServiceRelationship> sr = remoteServiceRelationships.FindWithLock(H323PeerElementServiceRelationship(serviceID), PSafeReadWrite);
  if (sr == NULL)
    return false;

  // send the request - no response
  H501PDU pdu;
  H501_ServiceRelease & body = pdu.BuildServiceRelease(GetNextSequenceNumber());
  pdu.m_common.m_serviceID = sr->m_serviceID;
  body.m_reason = reason;
  WriteTo(pdu, sr->m_peer);

  OnRemoveServiceRelationship(sr->m_peer);
  InternalRemoveServiceRelationship(sr->m_peer);
  remoteServiceRelationships.Remove(sr);

  return true;
}

PBoolean H323PeerElement::OnRemoteServiceRelationshipDisappeared(OpalGloballyUniqueID & serviceID, const H323TransportAddress & peer)
{
  OpalGloballyUniqueID oldServiceID = serviceID;

  // the service ID specified is now gone
  PSafePtr<H323PeerElementServiceRelationship> sr = remoteServiceRelationships.FindWithLock(H323PeerElementServiceRelationship(serviceID), PSafeReadOnly);
  if (sr != NULL)
    remoteServiceRelationships.Remove(sr);
  InternalRemoveServiceRelationship(peer);

  // attempt to create a new service relationship
  if (ServiceRequestByAddr(peer, serviceID) != Confirmed) { 
    PTRACE(2, "PeerElement\tService relationship with " << peer << " disappeared and refused new relationship");
    OnRemoveServiceRelationship(peer);
    return false;
  }

  // we have a new service ID
  PTRACE(2, "PeerElement\tService relationship with " << peer << " disappeared and new relationship established");
  serviceID = remotePeerAddrToServiceID(peer);

  return true;
}

void H323PeerElement::InternalRemoveServiceRelationship(const H323TransportAddress & peer)
{
  {
    PWaitAndSignal m(remotePeerListMutex);
    remotePeerAddrToServiceID.RemoveAt(peer);
    remotePeerAddrToOrdinalKey.RemoveAt(peer);
  }
  monitorTickle.Signal();
}



///////////////////////////////////////////////////////////
//
// descriptor table functions
//

H323PeerElementDescriptor * H323PeerElement::CreateDescriptor(const OpalGloballyUniqueID & descriptorID)
{
  return new H323PeerElementDescriptor(descriptorID);
}

PBoolean H323PeerElement::AddDescriptor(const OpalGloballyUniqueID & descriptorID,
                                            const PStringArray & aliasStrings, 
                               const H323TransportAddressArray & transportAddresses, 
                                                        unsigned options, 
                                                            PBoolean now)
{
  // convert transport addresses to aliases
  H225_ArrayOf_AliasAddress aliases;
  H323SetAliasAddresses(aliasStrings, aliases);
  return AddDescriptor(descriptorID,
                       aliases, transportAddresses, options, now);
}


PBoolean H323PeerElement::AddDescriptor(const OpalGloballyUniqueID & descriptorID,
                               const H225_ArrayOf_AliasAddress & aliases, 
                               const H323TransportAddressArray & transportAddresses, 
                                                        unsigned options, 
                                                            PBoolean now)
{
  H225_ArrayOf_AliasAddress addresses;
  H323SetAliasAddresses(transportAddresses, addresses);
  return AddDescriptor(descriptorID,
                       LocalServiceRelationshipOrdinal,
                       aliases, addresses, options, now);
}


PBoolean H323PeerElement::AddDescriptor(const OpalGloballyUniqueID & descriptorID,
                               const H225_ArrayOf_AliasAddress & aliases, 
                               const H225_ArrayOf_AliasAddress & transportAddress, 
                                                        unsigned options, 
                                                            PBoolean now)
{
  // create a new descriptor
  return AddDescriptor(descriptorID,
                       LocalServiceRelationshipOrdinal,
                       aliases, transportAddress, options, now);
}

PBoolean H323PeerElement::AddDescriptor(const OpalGloballyUniqueID & descriptorID,
                                             const POrdinalKey & creator,
                               const H225_ArrayOf_AliasAddress & aliases, 
                               const H225_ArrayOf_AliasAddress & transportAddresses, 
                                                        unsigned options, 
                                                            PBoolean now)
{
  // create an const H501_ArrayOf_AddressTemplate with the template information
  H501_ArrayOf_AddressTemplate addressTemplates;

  // copy data into the descriptor
  addressTemplates.SetSize(1);
  H225_EndpointType epType;
  m_endpoint.SetEndpointTypeInfo(epType);
  H323PeerElementDescriptor::CopyToAddressTemplate(addressTemplates[0], epType, aliases, transportAddresses, options);

  return AddDescriptor(descriptorID, creator, addressTemplates, now);
}

PBoolean H323PeerElement::AddDescriptor(const OpalGloballyUniqueID & descriptorID,
                                             const POrdinalKey & creator,
                            const H501_ArrayOf_AddressTemplate & addressTemplates,
                                                   const PTime & updateTime,
                                                            PBoolean now)
{
  // see if there is actually a descriptor with this ID
  PSafePtr<H323PeerElementDescriptor> descriptor = descriptors.FindWithLock(H323PeerElementDescriptor(descriptorID), PSafeReadWrite);
  H501_UpdateInformation_updateType::Choices updateType = H501_UpdateInformation_updateType::e_changed;
  PBoolean add = false;
  {
    PWaitAndSignal m(aliasMutex);
    if (descriptor != NULL) {
      RemoveDescriptorInformation(descriptor->addressTemplates);

      // only update if the update time is later than what we already have
      if (updateTime < descriptor->lastChanged)
        return true;

    } else {
      add = true;
      descriptor                   = CreateDescriptor(descriptorID);
      descriptor->creator          = creator;
      descriptor->addressTemplates = addressTemplates;
      updateType                   = H501_UpdateInformation_updateType::e_added;
    }
    descriptor->lastChanged = PTime();

    // add all patterns and transport addresses to secondary lookup tables
    PINDEX i, j, k;
    for (i = 0; i < descriptor->addressTemplates.GetSize(); i++) {
      H501_AddressTemplate & addressTemplate = addressTemplates[i];

      // add patterns for this descriptor
      for (j = 0; j < addressTemplate.m_pattern.GetSize(); j++) {
        H501_Pattern & pattern = addressTemplate.m_pattern[j];
        switch (pattern.GetTag()) {
          case H501_Pattern::e_specific:
            specificAliasToDescriptorID.Append(CreateAliasKey((H225_AliasAddress &)pattern, descriptorID, i, false));
            break;
          case H501_Pattern::e_wildcard:
            wildcardAliasToDescriptorID.Append(CreateAliasKey((H225_AliasAddress &)pattern, descriptorID, i, true));
            break;
          case H501_Pattern::e_range:
            break;
        }
      }

      // add transport addresses for this descriptor
      H501_ArrayOf_RouteInformation & routeInfos = addressTemplate.m_routeInfo;
      for (j = 0; j < routeInfos.GetSize(); j++) {
        H501_ArrayOf_ContactInformation & contacts = routeInfos[j].m_contacts;
        for (k = 0; k < contacts.GetSize(); k++) {
          H501_ContactInformation & contact = contacts[k];
          H225_AliasAddress & transportAddress = contact.m_transportAddress;
          transportAddressToDescriptorID.Append(CreateAliasKey((H225_AliasAddress &)transportAddress, descriptorID, i));
        }
      }
    }
  }

  if (!add)
    OnUpdateDescriptor(*descriptor);
  else {
    descriptors.Append(descriptor);
    OnNewDescriptor(*descriptor);
  }

  // do the update now, or later
  if (now) {
    PTRACE(3, "PeerElement\tDescriptor " << descriptorID << " added/updated");
    UpdateDescriptor(descriptor, updateType);
  } else if (descriptor->state != H323PeerElementDescriptor::Deleted) {
    PTRACE(3, "PeerElement\tDescriptor " << descriptorID << " queued to be added");
    descriptor->state = H323PeerElementDescriptor::Dirty;
    monitorTickle.Signal();
  }

  return true;
}
  
H323PeerElement::AliasKey * H323PeerElement::CreateAliasKey(const H225_AliasAddress & alias, const OpalGloballyUniqueID & id, PINDEX pos, PBoolean wild)
{
  return new AliasKey(alias, id, pos, wild);
}

void H323PeerElement::RemoveDescriptorInformation(const H501_ArrayOf_AddressTemplate & addressTemplates)
{
  PWaitAndSignal m(aliasMutex);
  PINDEX i, j, k, idx;

  // remove all patterns and transport addresses for this descriptor
  for (i = 0; i < addressTemplates.GetSize(); i++) {
    H501_AddressTemplate & addressTemplate = addressTemplates[i];

    // remove patterns for this descriptor
    for (j = 0; j < addressTemplate.m_pattern.GetSize(); j++) {
      H501_Pattern & pattern = addressTemplate.m_pattern[j];
      switch (pattern.GetTag()) {
        case H501_Pattern::e_specific:
          idx = specificAliasToDescriptorID.GetValuesIndex((H225_AliasAddress &)pattern);
          if (idx != P_MAX_INDEX)
            specificAliasToDescriptorID.RemoveAt(idx);
          break;
        case H501_Pattern::e_wildcard:
          idx = wildcardAliasToDescriptorID.GetValuesIndex((H225_AliasAddress &)pattern);
          if (idx != P_MAX_INDEX)
            wildcardAliasToDescriptorID.RemoveAt(idx);
          break;
        case H501_Pattern::e_range:
          break;
      }
    }

    // remove transport addresses for this descriptor
    H501_ArrayOf_RouteInformation & routeInfos = addressTemplate.m_routeInfo;
    for (j = 0; j < routeInfos.GetSize(); j++) {
      H501_ArrayOf_ContactInformation & contacts = routeInfos[i].m_contacts;
      for (k = 0; k < contacts.GetSize(); k++) {
        H501_ContactInformation & contact = contacts[k];
        H225_AliasAddress & transportAddress = contact.m_transportAddress;
        idx = transportAddressToDescriptorID.GetValuesIndex(transportAddress);
        if (idx != P_MAX_INDEX)
          transportAddressToDescriptorID.RemoveAt(idx);
      }
    }
  }
}

PBoolean H323PeerElement::DeleteDescriptor(const PString & str, PBoolean now)
{
  H225_AliasAddress alias;
  H323SetAliasAddress(str, alias);
  return DeleteDescriptor(alias, now);
}

PBoolean H323PeerElement::DeleteDescriptor(const H225_AliasAddress & alias, PBoolean now)
{
  OpalGloballyUniqueID descriptorID("");

  // find the descriptor ID for the descriptor
  {
    PWaitAndSignal m(aliasMutex);
    PINDEX idx = specificAliasToDescriptorID.GetValuesIndex(alias);
    if (idx == P_MAX_INDEX)
      return false;
    descriptorID = ((AliasKey &)specificAliasToDescriptorID[idx]).id;
  }

  return DeleteDescriptor(descriptorID, now);
}

PBoolean H323PeerElement::DeleteDescriptor(const OpalGloballyUniqueID & descriptorID, PBoolean now)
{
  // see if there is a descriptor with this ID
  PSafePtr<H323PeerElementDescriptor> descriptor = descriptors.FindWithLock(H323PeerElementDescriptor(descriptorID), PSafeReadWrite);
  if (descriptor == NULL)
    return false;

  OnRemoveDescriptor(*descriptor);

  RemoveDescriptorInformation(descriptor->addressTemplates);

  // delete the descriptor, or mark it as to be deleted
  if (now) {
    PTRACE(3, "PeerElement\tDescriptor " << descriptorID << " deleted");
    UpdateDescriptor(descriptor, H501_UpdateInformation_updateType::e_deleted);
  } else {
    PTRACE(3, "PeerElement\tDescriptor for " << descriptorID << " queued to be deleted");
    descriptor->state = H323PeerElementDescriptor::Deleted;
    monitorTickle.Signal();
  }

  return true;
}

PBoolean H323PeerElement::UpdateDescriptor(H323PeerElementDescriptor * descriptor)
{
  H501_UpdateInformation_updateType::Choices updateType = H501_UpdateInformation_updateType::e_changed;
  switch (descriptor->state) {
    case H323PeerElementDescriptor::Clean:
      return true;
    
    case H323PeerElementDescriptor::Dirty:
      break;

    case H323PeerElementDescriptor::Deleted:
      updateType = H501_UpdateInformation_updateType::e_deleted;
      break;
  }

  return UpdateDescriptor(descriptor, updateType);
}

PBoolean H323PeerElement::UpdateDescriptor(H323PeerElementDescriptor * descriptor, H501_UpdateInformation_updateType::Choices updateType)
{
  if (updateType == H501_UpdateInformation_updateType::e_deleted)
    descriptor->state = H323PeerElementDescriptor::Deleted;
  else if (descriptor->state == H323PeerElementDescriptor::Deleted)
    updateType = H501_UpdateInformation_updateType::e_deleted;
  else if (descriptor->state == H323PeerElementDescriptor::Clean)
    return true;
  else
    descriptor->state = H323PeerElementDescriptor::Clean;

  for (PSafePtr<H323PeerElementServiceRelationship> sr = GetFirstRemoteServiceRelationship(PSafeReadOnly); sr != NULL; sr++) {
    SendUpdateDescriptorByID(sr->m_serviceID, descriptor, updateType);
  }

  if (descriptor->state == H323PeerElementDescriptor::Deleted)
    descriptors.Remove(descriptor);

  return true;
}

///////////////////////////////////////////////////////////
//
// descriptor peer element functions
//

H323PeerElement::Error H323PeerElement::SendUpdateDescriptorByID(const OpalGloballyUniqueID & serviceID, 
                                                              H323PeerElementDescriptor * descriptor, 
                                               H501_UpdateInformation_updateType::Choices updateType)
{
  if (PAssertNULL(m_transport) == NULL)
    return NoResponse;

  H501PDU pdu;
  pdu.BuildDescriptorUpdate(GetNextSequenceNumber(), m_transport->GetLastReceivedAddress());
  H323TransportAddress peer;

  // put correct service descriptor into the common data
  {
    // check to see if we have a service relationship with the peer already
    PSafePtr<H323PeerElementServiceRelationship> sr = remoteServiceRelationships.FindWithLock(H323PeerElementServiceRelationship(serviceID), PSafeReadOnly);

    // if there is no service relationship, then nothing to do
    if (sr == NULL)
      return NoServiceRelationship;

    pdu.m_common.IncludeOptionalField(H501_MessageCommonInfo::e_serviceID);
    pdu.m_common.m_serviceID = sr->m_serviceID;
    peer = sr->m_peer;
  }

  return SendUpdateDescriptor(pdu, peer, descriptor, updateType);
}

H323PeerElement::Error H323PeerElement::SendUpdateDescriptorByAddr(const H323TransportAddress & peer, 
                                                              H323PeerElementDescriptor * descriptor, 
                                               H501_UpdateInformation_updateType::Choices updateType)
{
  if (PAssertNULL(m_transport) == NULL)
    return NoResponse;

  H501PDU pdu;
  pdu.BuildDescriptorUpdate(GetNextSequenceNumber(), m_transport->GetLastReceivedAddress());
  return SendUpdateDescriptor(pdu, peer, descriptor, updateType);
}

H323PeerElement::Error H323PeerElement::SendUpdateDescriptor(H501PDU & pdu, 
                                          const H323TransportAddress & peer, 
                                           H323PeerElementDescriptor * descriptor,
                            H501_UpdateInformation_updateType::Choices updateType)
{
  if (PAssertNULL(m_transport) == NULL)
    return NoResponse;

  H501_DescriptorUpdate & body = pdu.m_body;

  // put in sender address
  H323TransportAddressArray addrs = m_endpoint.GetInterfaceAddresses(m_transport);
  PAssert(addrs.GetSize() > 0, "No interface addresses");
  H323SetAliasAddress(addrs[0], body.m_sender, H225_AliasAddress::e_transportID);

  // add information
  body.m_updateInfo.SetSize(1);
  H501_UpdateInformation & info = body.m_updateInfo[0];
  info.m_descriptorInfo.SetTag(H501_UpdateInformation_descriptorInfo::e_descriptor);
  info.m_updateType.SetTag(updateType);
  descriptor->CopyTo(info.m_descriptorInfo);

  // make the request
  Request request(pdu.GetSequenceNumber(), pdu, peer);
  if (MakeRequest(request))
    return Confirmed;

  // if error was no service relationship, then establish relationship and try again
  switch (request.m_responseResult) {
    case Request::NoResponseReceived :
      PTRACE(2, "PeerElement\tUpdateDescriptor to " << peer << " failed due to no response");
      break;

    default:
      PTRACE(2, "PeerElement\tUpdateDescriptor to " << peer << " refused with unknown response " << (int)request.m_responseResult);
      return Rejected;
  }

  return Rejected;
}

H323Transaction::Response H323PeerElement::OnDescriptorUpdate(H501DescriptorUpdate & /*info*/)
{
  return H323Transaction::Ignore;
}

PBoolean H323PeerElement::OnReceiveDescriptorUpdate(const H501PDU & pdu, const H501_DescriptorUpdate & /*pduBody*/)
{
  H501DescriptorUpdate * info = new H501DescriptorUpdate(*this, pdu);
  if (!info->HandlePDU())
    delete info;

  return false;
}

PBoolean H323PeerElement::OnReceiveDescriptorUpdateACK(const H501PDU & pdu, const H501_DescriptorUpdateAck & pduBody)
{
  if (!H323_AnnexG::OnReceiveDescriptorUpdateACK(pdu, pduBody))
    return false;

  if (m_lastRequest->m_responseInfo != NULL)
    dynamic_cast<H501_MessageCommonInfo &>(*m_lastRequest->m_responseInfo) = pdu.m_common;

  return true;
}

///////////////////////////////////////////////////////////
//
// access request functions
//

PBoolean H323PeerElement::AccessRequest(const PString & searchAlias,
                                     PStringArray & destAliases,
                            H323TransportAddress & transportAddress,
                                          unsigned options)
{
  H225_AliasAddress h225searchAlias;
  H323SetAliasAddress(searchAlias, h225searchAlias);

  H225_ArrayOf_AliasAddress h225destAliases;
  if (!AccessRequest(h225searchAlias, h225destAliases, transportAddress, options))
    return false;

  destAliases = H323GetAliasAddressStrings(h225destAliases);
  return true;
}


PBoolean H323PeerElement::AccessRequest(const PString & searchAlias, 
                        H225_ArrayOf_AliasAddress & destAliases,
                             H323TransportAddress & transportAddress, 
                                           unsigned options)
{
  H225_AliasAddress h225searchAlias;
  H323SetAliasAddress(searchAlias, h225searchAlias);
  return AccessRequest(h225searchAlias, destAliases, transportAddress, options);
}

PBoolean H323PeerElement::AccessRequest(const H225_AliasAddress & searchAlias, 
                                  H225_ArrayOf_AliasAddress & destAliases,
                                       H323TransportAddress & transportAddress, 
                                                     unsigned options)
{
  H225_AliasAddress h225Address;
  if (!AccessRequest(searchAlias, destAliases, h225Address, options))
    return false;

  transportAddress = H323GetAliasAddressString(h225Address);
  return true;
}

PBoolean H323PeerElement::AccessRequest(const H225_AliasAddress & searchAlias, 
                                  H225_ArrayOf_AliasAddress & destAliases,
                                          H225_AliasAddress & transportAddress, 
                                                     unsigned options)
{
  // try each service relationship in turn
  POrdinalSet peersTried;

  for (PSafePtr<H323PeerElementServiceRelationship> sr = GetFirstRemoteServiceRelationship(PSafeReadOnly); sr != NULL; sr++) {

    // create the request
    H501PDU request;
    {
      H501_AccessRequest & requestBody = request.BuildAccessRequest(GetNextSequenceNumber(), m_transport->GetLastReceivedAddress());

      // set dest information
      H501_PartyInformation & destInfo = requestBody.m_destinationInfo;
      destInfo.m_logicalAddresses.SetSize(1);
      destInfo.m_logicalAddresses[0] = searchAlias;

      // set protocols
      requestBody.IncludeOptionalField(H501_AccessRequest::e_desiredProtocols);
      H323PeerElementDescriptor::SetProtocolList(requestBody.m_desiredProtocols, options);
    }

    // make the request
    H501PDU reply;
    H323PeerElement::Error error = SendAccessRequestByID(sr->m_serviceID, request, reply);
    H323TransportAddress peerAddr = sr->m_peer;

    while (error == Confirmed) {

      // make sure we got at least one template
      H501_AccessConfirmation & confirm = reply.m_body;
      H501_ArrayOf_AddressTemplate & addressTemplates = confirm.m_templates;
      if (addressTemplates.GetSize() == 0) {
        PTRACE(2, "Main\tAccessRequest for " << searchAlias << " from " << peerAddr << " contains no templates");
        break;
      }
      H501_AddressTemplate & addressTemplate = addressTemplates[0];

      // make sure patterns are returned
      H501_ArrayOf_Pattern & patterns = addressTemplate.m_pattern;
      if (patterns.GetSize() == 0) {
        PTRACE(2, "Main\tAccessRequest for " << searchAlias << " from " << peerAddr << " contains no patterns");
        break;
      }

      // make sure routes are returned
      H501_ArrayOf_RouteInformation & routeInfos = addressTemplate.m_routeInfo;
      if (routeInfos.GetSize() == 0) {
        PTRACE(2, "Main\tAccessRequest for " << searchAlias << " from " << peerAddr << " contains no routes");
        break;
      }
      H501_RouteInformation & routeInfo = addressTemplate.m_routeInfo[0];

      // make sure routes contain contacts
      H501_ArrayOf_ContactInformation & contacts = routeInfo.m_contacts;
      if (contacts.GetSize() == 0) {
        PTRACE(2, "Main\tAccessRequest for " << searchAlias << " from " << peerAddr << " contains no contacts");
        break;
      }
      H501_ContactInformation & contact = routeInfo.m_contacts[0];

      // get the address
      H225_AliasAddress contactAddress = contact.m_transportAddress;
      int tag = routeInfo.m_messageType.GetTag();
      if (tag == H501_RouteInformation_messageType::e_sendAccessRequest) {
        PTRACE(2, "Main\tAccessRequest for " << searchAlias << " redirected from " << peerAddr << " to " << contactAddress);
        peerAddr = H323GetAliasAddressString(contactAddress);
      }
      else if (tag == H501_RouteInformation_messageType::e_sendSetup) {

        // get the dest aliases
        destAliases.SetSize(addressTemplate.m_pattern.GetSize());
        PINDEX count = 0;
        PINDEX i;
        for (i = 0; i < addressTemplate.m_pattern.GetSize(); i++) {  
          if (addressTemplate.m_pattern[i].GetTag() == H501_Pattern::e_specific) {  
             H225_AliasAddress & alias = addressTemplate.m_pattern[i];  
             destAliases[count++] = alias;  
          }  
        }  
        destAliases.SetSize(count);  

        transportAddress = contactAddress;
        PTRACE(3, "Main\tAccessRequest for " << searchAlias << " returned " << transportAddress << " from " << peerAddr);
        return true;
      }
      else { // H501_RouteInformation_messageType::e_nonExistent
        PTRACE(3, "Main\tAccessRequest for " << searchAlias << " from " << peerAddr << " returned nonExistent");
        break;
      }

      // this is the address to send the new request to
      H323TransportAddress addr = peerAddr;

      // create the request
      H501_AccessRequest & requestBody = request.BuildAccessRequest(GetNextSequenceNumber(), m_transport->GetLastReceivedAddress());

      // set dest information
      H501_PartyInformation & destInfo = requestBody.m_destinationInfo;
      destInfo.m_logicalAddresses.SetSize(1);
      destInfo.m_logicalAddresses[0] = searchAlias;

      // set protocols
      requestBody.IncludeOptionalField(H501_AccessRequest::e_desiredProtocols);
      H323PeerElementDescriptor::SetProtocolList(requestBody.m_desiredProtocols, options);

      // make the request
      error = SendAccessRequestByAddr(addr, request, reply);
    }
  }

  return false;
}

///////////////////////////////////////////////////////////
//
// access request functions
//

H323PeerElement::Error H323PeerElement::SendAccessRequestByID(const OpalGloballyUniqueID & origServiceID, 
                                                                                 H501PDU & pdu, 
                                                                                 H501PDU & confirmPDU)
{
  if (PAssertNULL(m_transport) == NULL)
    return NoResponse;

  OpalGloballyUniqueID serviceID = origServiceID;

  for (;;) {

    // get the peer address
    H323TransportAddress peer;
    { 
      PSafePtr<H323PeerElementServiceRelationship> sr = remoteServiceRelationships.FindWithLock(H323PeerElementServiceRelationship(serviceID), PSafeReadOnly);
      if (sr == NULL)
        return NoServiceRelationship;
      peer = sr->m_peer;
    }

    // set the service ID
    pdu.m_common.IncludeOptionalField(H501_MessageCommonInfo::e_serviceID);
    pdu.m_common.m_serviceID = serviceID;

    // make the request
    Request request(pdu.GetSequenceNumber(), pdu, peer);
    request.m_responseInfo = &confirmPDU;
    if (MakeRequest(request))
      break;

    // if error was no service relationship, then establish relationship and try again
    switch (request.m_responseResult) {
      case Request::NoResponseReceived :
        PTRACE(2, "PeerElement\tAccessRequest to " << peer << " failed due to no response");
        return Rejected;

      case Request::RejectReceived:
        switch (request.m_rejectReason) {
          case H501_ServiceRejectionReason::e_unknownServiceID:
            if (!OnRemoteServiceRelationshipDisappeared(serviceID, peer))
              return Rejected;
            break;
          default:
            return Rejected;
        }
        break;

      default:
        PTRACE(2, "PeerElement\tAccessRequest to " << peer << " refused with unknown response " << (int)request.m_responseResult);
        return Rejected;
    }
  }

  return Confirmed;
}

H323PeerElement::Error H323PeerElement::SendAccessRequestByAddr(const H323TransportAddress & peerAddr, 
                                                                                   H501PDU & pdu, 
                                                                                   H501PDU & confirmPDU)
{
  if (PAssertNULL(m_transport) == NULL)
    return NoResponse;

  pdu.m_common.RemoveOptionalField(H501_MessageCommonInfo::e_serviceID);

  // make the request
  Request request(pdu.GetSequenceNumber(), pdu, peerAddr);
  request.m_responseInfo = &confirmPDU;
  if (MakeRequest(request))
    return Confirmed;

  // if error was no service relationship, then establish relationship and try again
  switch (request.m_responseResult) {
    case Request::NoResponseReceived :
      PTRACE(2, "PeerElement\tAccessRequest to " << peerAddr << " failed due to no response");
      break;

    case Request::RejectReceived:
      PTRACE(2, "PeerElement\tAccessRequest failed due to " << request.m_rejectReason);
      break;

    default:
      PTRACE(2, "PeerElement\tAccessRequest to " << peerAddr << " refused with unknown response " << (int)request.m_responseResult);
      break;
  }

  return Rejected;
}

H323Transaction::Response H323PeerElement::OnAccessRequest(H501AccessRequest & info)
{
  info.SetRejectReason(H501_AccessRejectionReason::e_noServiceRelationship);
  return H323Transaction::Reject;
}

PBoolean H323PeerElement::OnReceiveAccessRequest(const H501PDU & pdu, const H501_AccessRequest & /*pduBody*/)
{
  H501AccessRequest * info = new H501AccessRequest(*this, pdu);
  if (!info->HandlePDU())
    delete info;

  return false;
}

PBoolean H323PeerElement::OnReceiveAccessConfirmation(const H501PDU & pdu, const H501_AccessConfirmation & pduBody)
{
  if (!H323_AnnexG::OnReceiveAccessConfirmation(pdu, pduBody))
    return false;

  if (m_lastRequest->m_responseInfo != NULL)
    dynamic_cast<H501PDU &>(*m_lastRequest->m_responseInfo) = pdu;

  return true;
}

PBoolean H323PeerElement::OnReceiveAccessRejection(const H501PDU & pdu, const H501_AccessRejection & pduBody)
{
  if (!H323_AnnexG::OnReceiveAccessRejection(pdu, pduBody))
    return false;

  return true;
}

PBoolean H323PeerElement::MakeRequest(Request & request)
{
  PWaitAndSignal muyex(m_requestMutex);
  return H323_AnnexG::MakeRequest(request);
}

//////////////////////////////////////////////////////////////////////////////

PObject::Comparison H323PeerElementDescriptor::Compare(const PObject & obj) const
{ 
  H323PeerElementDescriptor & other = (H323PeerElementDescriptor &)obj;
  return descriptorID.Compare(other.descriptorID); 
}

void H323PeerElementDescriptor::CopyTo(H501_Descriptor & descriptor)
{
  descriptor.m_descriptorInfo.m_descriptorID = descriptorID;
  descriptor.m_descriptorInfo.m_lastChanged  = lastChanged.AsString("yyyyMMddhhmmss", PTime::GMT);
  descriptor.m_templates                     = addressTemplates;

  if (!gatekeeperID.IsEmpty()) {
    descriptor.IncludeOptionalField(H501_Descriptor::e_gatekeeperID);
    descriptor.m_gatekeeperID = gatekeeperID;
  }
}


PBoolean H323PeerElementDescriptor::ContainsNonexistent()
{
  PBoolean blocked = false;

  // look for any nonexistent routes, which means this descriptor does NOT match
  PINDEX k, j;
  for (k = 0; !blocked && (k < addressTemplates.GetSize()); k++) {
	  H501_ArrayOf_RouteInformation & routeInfo = addressTemplates[k].m_routeInfo;
    for (j = 0; !blocked && (j < routeInfo.GetSize()); j++) {
      if (routeInfo[j].m_messageType.GetTag() == H501_RouteInformation_messageType::e_nonExistent)
        blocked = true;
    }
  }

  return blocked;
}


PBoolean H323PeerElementDescriptor::CopyToAddressTemplate(H501_AddressTemplate & addressTemplate,
                                                   const H225_EndpointType & epInfo,
                                           const H225_ArrayOf_AliasAddress & aliases, 
                                           const H225_ArrayOf_AliasAddress & transportAddresses, 
                                                                    unsigned options)
{
  // add patterns for this descriptor
  addressTemplate.m_pattern.SetSize(aliases.GetSize());
  PINDEX j;
  for (j = 0; j < aliases.GetSize(); j++) {
    H501_Pattern & pattern = addressTemplate.m_pattern[j];
    if ((options & Option_WildCard) != 0)
      pattern.SetTag(H501_Pattern::e_wildcard);
    else 
      pattern.SetTag(H501_Pattern::e_specific);
    (H225_AliasAddress &)pattern = aliases[j];
  }

  // add transport addresses for this descriptor
  H501_ArrayOf_RouteInformation & routeInfos = addressTemplate.m_routeInfo;
  routeInfos.SetSize(1);
  H501_RouteInformation & routeInfo = routeInfos[0];

  if ((options & Option_NotAvailable) != 0)
    routeInfo.m_messageType.SetTag(H501_RouteInformation_messageType::e_nonExistent);

  else if ((options & Option_SendAccessRequest) != 0)
    routeInfo.m_messageType.SetTag(H501_RouteInformation_messageType::e_sendAccessRequest);

  else {
    routeInfo.m_messageType.SetTag(H501_RouteInformation_messageType::e_sendSetup);
    routeInfo.m_callSpecific = false;
    routeInfo.IncludeOptionalField(H501_RouteInformation::e_type);
    routeInfo.m_type = epInfo;
  }

  routeInfo.m_callSpecific = false;
  H501_ArrayOf_ContactInformation & contacts = routeInfos[0].m_contacts;
  contacts.SetSize(transportAddresses.GetSize());
  PINDEX i;
  for (i = 0; i < transportAddresses.GetSize(); i++) {
    H501_ContactInformation & contact = contacts[i];
    contact.m_transportAddress = transportAddresses[i];
    contact.m_priority         = H323PeerElementDescriptor::GetPriorityOption(options);
  }

  // add protocols
  addressTemplate.IncludeOptionalField(H501_AddressTemplate::e_supportedProtocols);
  SetProtocolList(addressTemplate.m_supportedProtocols, options);

  return true;
}

/*
PBoolean H323PeerElementDescriptor::CopyFrom(const H501_Descriptor & descriptor)
{
  descriptorID                           = descriptor.m_descriptorInfo.m_descriptorID;
  //lastChanged.AsString("yyyyMMddhhmmss") = descriptor.m_descriptorInfo.m_lastChanged;
  addressTemplates                       = descriptor.m_templates;

  if (descriptor.HasOptionalField(H501_Descriptor::e_gatekeeperID))
    gatekeeperID = descriptor.m_gatekeeperID;
  else
    gatekeeperID = PString::Empty();

  return true;
}
*/

void H323PeerElementDescriptor::SetProtocolList(H501_ArrayOf_SupportedProtocols & h501Protocols, unsigned options)
{
  h501Protocols.SetSize(0);
  int mask =1;
  do {
    if (options & mask) {
      int pos = h501Protocols.GetSize();
      switch (mask) {
        case H323PeerElementDescriptor::Protocol_H323:
          h501Protocols.SetSize(pos+1);
          h501Protocols[pos].SetTag(H225_SupportedProtocols::e_h323);
          break;

        case H323PeerElementDescriptor::Protocol_Voice:
          h501Protocols.SetSize(pos+1);
          h501Protocols[pos].SetTag(H225_SupportedProtocols::e_voice);
          break;

        default:
          break;
      }
    }
    mask *= 2;
  } while (mask != Protocol_Max);
}

unsigned H323PeerElementDescriptor::GetProtocolList(const H501_ArrayOf_SupportedProtocols & h501Protocols)
{
  unsigned options = 0;
  PINDEX i;
  for (i = 0; i < h501Protocols.GetSize(); i++) {
    switch (h501Protocols[i].GetTag()) {
      case H225_SupportedProtocols::e_h323:
        options += Protocol_H323;
        break;

      case H225_SupportedProtocols::e_voice:
        options += Protocol_Voice;
        break;

      default:
        break;
    }
  }
  return options;
}


#endif // OPAL_H501

// End of file ////////////////////////////////////////////////////////////////
