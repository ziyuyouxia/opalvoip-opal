/*
 *
 * Inter Asterisk Exchange 2
 * 
 * Extension of the Opal Connection class. There is one instance of this
 * class per current call.
 * 
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (c) 2005 Indranet Technologies Ltd.
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
 * The Original Code is Open Phone Abstraction Library.
 *
 * The Initial Developer of the Original Code is Indranet Technologies Ltd.
 *
 * The author of this code is Derek J Smithies
 *
 *
 *
 * $Log: iax2con.cxx,v $
 * Revision 1.4  2005/08/24 01:38:38  dereksmithies
 * Add encryption, iax2 style. Numerous tidy ups. Use the label iax2, not iax
 *
 * Revision 1.3  2005/08/13 07:19:17  rjongbloed
 * Fixed MSVC6 compiler issues
 *
 * Revision 1.2  2005/08/04 08:14:17  rjongbloed
 * Fixed Windows build under DevStudio 2003 of IAX2 code
 *
 * Revision 1.1  2005/07/30 07:01:33  csoutheren
 * Added implementation of IAX2 (Inter Asterisk Exchange 2) protocol
 * Thanks to Derek Smithies of Indranet Technologies Ltd. for
 * writing and contributing this code
 *
 *
 *
 */

#include <ptlib.h>
#include <typeinfo>


#ifdef P_USE_PRAGMA
#pragma implementation "iax2con.h"
#endif

#include <iax2/causecode.h>
#include <iax2/frame.h>
#include <iax2/iax2con.h>
#include <iax2/iax2ep.h>
#include <iax2/iax2medstrm.h>
#include <iax2/ies.h>
#include <iax2/sound.h>
#include <iax2/transmit.h>


#define new PNEW



////////////////////////////////////////////////////////////////////////////////


IAX2Connection::IAX2Connection(OpalCall & call,               /* Owner call for connection        */
			       IAX2EndPoint &ep, 
			     const PString & token,         /* Token to identify the connection */
			     void * /*userData */,
			     const PString & remoteParty)
  : OpalConnection(call, ep, token), 
     endpoint(ep)
{  
  remotePartyName = remoteParty;
  
  iaxProcessor = new IAX2Processor(ep);
  iaxProcessor->AssignConnection(this);
  SetCallToken(token);
  originating = FALSE;

  PTRACE(3, "IAX2Connection class has been initialised, and is ready to run");

  ep.CopyLocalMediaFormats(localMediaFormats);
  AdjustMediaFormats(localMediaFormats);
  for (PINDEX i = 0; i < localMediaFormats.GetSize(); i++) {
    PTRACE(3, "Local ordered codecs are " << localMediaFormats[i]);
  }

  phase = SetUpPhase;
}

IAX2Connection::~IAX2Connection()
{
  iaxProcessor->Terminate();
  iaxProcessor->WaitForTermination(1000);
  if (!iaxProcessor->IsTerminated()) {
    PAssertAlways("List rpocessor failed to terminate");
  }
  PTRACE(3, "connection has terminated");

  delete iaxProcessor;
  iaxProcessor = NULL;
}

void IAX2Connection::ClearCall(CallEndReason reason)
{
  PTRACE(3, "IAX2Con\tClearCall(reason);");

  callEndReason = reason;
  iaxProcessor->Hangup(reason);

  OpalConnection::ClearCall(reason);
}


void IAX2Connection::Release( CallEndReason reason)		        
{ 
  PTRACE(3, "IAX2Con\tRelease( CallEndReason reason)		        ");

  iaxProcessor->Release(reason); 
  OpalConnection::Release(reason);
}

void IAX2Connection::OnReleased()
{
  PTRACE(3, "IAX2Con\tOnReleased()");
  PTRACE(3, "IAX2\t***************************************************OnReleased:from IAX connection " 
	 << *this);

  iaxProcessor->OnReleased();
  OpalConnection::OnReleased();
  
}

void IAX2Connection::IncomingEthernetFrame(Frame *frame)
{
  PTRACE(3, "IAX2Con\tIncomingEthernetFrame(Frame *frame)" << frame->IdString());
  
  iaxProcessor->IncomingEthernetFrame(frame);
} 

void IAX2Connection::TransmitFrameToRemoteEndpoint(Frame *src)
{
  endpoint.transmitter->SendFrame(src);
}

void IAX2Connection::OnSetUp()
{
  PTRACE(3, "IAX2Con\tOnSetUp - we are proceeding with this call.");
  ownerCall.OnSetUp(*this); 
}

BOOL IAX2Connection::OnIncomingConnection()
{
  PTRACE(3, "IAX2Con\tOnIncomingConnection()");
  phase = SetUpPhase;
  originating = FALSE;
  PTRACE(3, "IAX2Con\tWe are receiving an incoming IAX call");
  PTRACE(3, "IAX2Con\tOnIncomingConnection  - we have received a cmdNew packet");
  return OpalConnection::OnIncomingConnection();
}

void IAX2Connection::OnAlerting()
{
  PTRACE(3, "IAX2Con\tOnAlerting()");
  PTRACE(3, "IAX2Con\t ON ALERTING " 
	 << PString(IsOriginating() ? " Originating" : "Receiving"));
  phase = AlertingPhase;
  PTRACE(3, "IAX2Con\tOn Alerting. Phone is ringing at  " << GetRemotePartyName());
  OpalConnection::OnAlerting();
}

BOOL IAX2Connection::SetAlerting(const PString & /*calleeName*/, BOOL /*withMedia*/) 
{ 
 PTRACE(3, "IAX2Con\tSetAlerting " << *this); 
 return TRUE;
}


BOOL IAX2Connection::SetConnected()
{
  PTRACE(3, "IAX2Con\tSetConnected " << *this);
  PTRACE(3, "IAX2Con\tSETCONNECTED " 
	 << PString(IsOriginating() ? " Originating" : "Receiving"));
  if (!originating)
    iaxProcessor->SetConnected();
 
  connectedTime = PTime ();

 if (mediaStreams.IsEmpty())
    phase = ConnectedPhase;
  else {
    phase = EstablishedPhase;
    OnEstablished();
  }

  return TRUE;
}


void IAX2Connection::OnConnected()
{
  PTRACE(3, "IAX2Con\tOnConnected()");
  PTRACE(3, "IAX2Con\t ON CONNECTED " 
	 << PString(IsOriginating() ? " Originating" : "Receiving"));
  phase = ConnectedPhase;
  PTRACE(3, "IAX2Con\tThis call has been connected");
  OpalConnection::OnConnected();
}


void IAX2Connection::OnEstablished()
{
  phase = EstablishedPhase;
  PTRACE(3, "IAX2Con\t ON ESTABLISHED " 
	 << PString(IsOriginating() ? " Originating" : "Receiving"));
  OpalConnection::OnEstablished();
  iaxProcessor->SetEstablished(originating);
}

OpalMediaStream * IAX2Connection::CreateMediaStream(const OpalMediaFormat & mediaFormat,
                                                   unsigned sessionID,
                                                   BOOL isDataSource)
{
  PTRACE(3, "IAX2Con\tCreateMediaStream");
  if (ownerCall.IsMediaBypassPossible(*this, sessionID)) {
    PTRACE(3, "connection\t  create a null media stream ");
    return new OpalNullMediaStream(mediaFormat, sessionID, isDataSource);
  }

  PTRACE(3, "IAX2con\tCreate an OpalIAXMediaStream");
  return new OpalIAX2MediaStream(mediaFormat, sessionID, isDataSource,
                                 endpoint.GetManager().GetMinAudioJitterDelay(),
                                 endpoint.GetManager().GetMaxAudioJitterDelay(),
                                 *this);
}


void IAX2Connection::PutSoundPacketToNetwork(PBYTEArray *sound)
{
  iaxProcessor->PutSoundPacketToNetwork(sound);
} 


BOOL IAX2Connection::SetUpConnection() 
{
  PTRACE(3, "IAX2Con\tSetUpConnection() ");
  PTRACE(3, "IAX2Con\tWe are making a call");
  originating = TRUE;
  return iaxProcessor->SetUpConnection(); 
}

void IAX2Connection::SetCallToken(PString newToken)
{
  PTRACE(3, "IAX2Con\tSetCallToken(PString newToken)" << newToken);

  callToken = newToken;
  iaxProcessor->SetCallToken(newToken);
}



PINDEX IAX2Connection::GetSupportedCodecs() 
{ 
  return endpoint.GetSupportedCodecs(localMediaFormats);
}
  
PINDEX IAX2Connection::GetPreferredCodec()
{ 
  return endpoint.GetPreferredCodec(localMediaFormats);
}

void IAX2Connection::BuildRemoteCapabilityTable(unsigned int remoteCapability, unsigned int format)
{
  PTRACE(3, "Connection\tBuildRemote Capability table for codecs");
  
  if (remoteCapability == 0)
    remoteCapability = format;

  PINDEX i;
  if (remoteCapability != 0) {
    for (i = 0; i < FullFrameVoice::supportedCodecs; i++) {
      if ((remoteCapability & (1 << i)) == 0)
	continue;

      PString wildcard = FullFrameVoice::GetSubClassName(1 << i);
      if (!remoteMediaFormats.HasFormat(wildcard)) {
	PTRACE(2, "Connection\tRemote capability says add codec " << wildcard);
	remoteMediaFormats += *(new OpalMediaFormat(wildcard));
      }
    }
  }

  if (format != 0) {
    PString wildcard = FullFrameVoice::GetSubClassName(format);
    remoteMediaFormats.Reorder(PStringArray(wildcard));
  }

  for (i = 0; i < remoteMediaFormats.GetSize(); i++) {
    PTRACE(3, "Connection\tRemote codec is " << remoteMediaFormats[i]);
  }    

  PTRACE(3, "REMOTE Codecs are " << remoteMediaFormats);
  AdjustMediaFormats(remoteMediaFormats);
  PTRACE(3, "REMOTE Codecs are " << remoteMediaFormats);
}


unsigned int IAX2Connection::ChooseCodec()
{
  int res;
  
  PTRACE(3, "Local capabilities are  " << localMediaFormats);
  PTRACE(3, "remote capabilities are " << remoteMediaFormats);
  
  if (remoteMediaFormats.GetSize() == 0) {
    PTRACE(3, "No remote media formats supported. Exit now ");
    return 0;
  }

  if (localMediaFormats.GetSize() == 0) {
    PTRACE(3, "No local media formats supported. Exit now ");
    return 0;
  }

  {
    PINDEX local;
    for (local = 0; local < localMediaFormats.GetSize(); local++) {
      if (localMediaFormats[local].GetPayloadType() == remoteMediaFormats[0].GetPayloadType()) {
	res = local;
	goto selectCodec;
      }
    }

    for (local = 0; local < localMediaFormats.GetSize(); local++) 
      for (PINDEX remote = 0; local < remoteMediaFormats.GetSize(); remote++) {
	if (localMediaFormats[local].GetPayloadType() == remoteMediaFormats[remote].GetPayloadType()) {
	  res = local;
	  goto selectCodec;
	}
      }
  }
  PTRACE(0, "Connection. Failed to select a codec " );
  return 0;

 selectCodec:
  PStringStream strm;
  strm << localMediaFormats[res];
  PTRACE(3, "Connection\t have selected the codec " << strm);

  return FullFrameVoice::OpalNameToIax2Value(strm);
}





/* The comment below is magic for those who use emacs to edit this file. */
/* With the comment below, the tab key does auto indent to 2 spaces.     */


/*
 * Local Variables:
 * mode:c
 * c-file-style:linux
 * c-basic-offset:2
 * End:
 */

