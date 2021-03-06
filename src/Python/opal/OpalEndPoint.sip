/*
 * Python bindings.
 *
 * Open Phone Abstraction Library (OPAL)
 *
 * Copyright (c) 2011 Demetrius Cassidy
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
 * The Original Code is Open Phone Abstraction Library (OPAL)
 *
 * The Initial Developer of the Original Code is Demetrius Cassidy
 *
 * Contributor(s): ______________________________________.
 *
 */

%ModuleHeaderCode
#include <ptlib.h>
#include <opal/endpoint.h>
%End


/**This class describes an endpoint base class.
   Each protocol (or psuedo-protocol) would create a descendant off this
   class to manage its particular subsystem. Typically this would involve
   listening for incoming connections and being able to set up outgoing
   connections. Depending on exact semantics it may need to spawn many threads
   to achieve this.

   An endpoint will also have a default set of media data formats that it can
   support. Connections created by it would initially have the same set, but
   according to the semantics of the underlying protocol may end up using a
   different set.

   Various call backs are provided for points in the connection management. As
   a rule these are passed straight on to the OpalManager for processing. An
   application may create descendants off this class' subclasses, eg
   H323EndPoint, to modify or monitor its behaviour but it does not have to do
   so as all basic operations are passed to the OpalManager so only that class
   need be subclassed.
 */
class OpalEndPoint : PObject /Abstract/
{
%ConvertToSubClassCode
	const PString & connType = sipCpp->GetPrefixName();
	
	if(connType == "h323")
		sipType = sipType_H323EndPoint;
		
	else if(connType == "sip")
		sipType = sipType_SIPEndPoint;
		
	else if (connType == "fax")
		sipType = sipType_OpalFaxEndPoint;
		
	else if (connType == "pc")
		sipType = sipType_OpalPCSSEndPoint;
		
	else
		sipType = sipType_OpalRTPEndPoint;
%End
	
  public:
    enum Attributes {
      CanTerminateCall = 1,
      SupportsE164 = 2
    };

  /**@name Construction */
  //@{
    /**Create a new endpoint.
     */
    OpalEndPoint(
      OpalManager & manager,          ///<  Manager of all endpoints.
      const PCaselessString & prefix, ///<  Prefix for URL style address strings
      unsigned attributes             ///<  Bit mask of attributes endpoint has
    );

    /**Destroy the endpoint.
     */
    ~OpalEndPoint();

    /**Shut down the endpoint, this is called by the OpalManager just before
       destroying the object and can be handy to make sure some things are
       stopped before the vtable gets clobbered.
      */
    virtual void ShutDown();
  //@}


  /**@name Listeners management */
  //@{
    /**Add a set of listeners to the endoint.
       This allows for the automatic creating of incoming call connections. 
       If the list is empty then GetDefaultListeners() is used.
      */
    PBoolean StartListeners(
      const PStringArray & interfaces ///<  Address of interface to listen on.
    );

    /**Add a listener to the endoint.
       This allows for the automatic creating of incoming call connections. /
       If the address is empty then the first entry of GetDefaultListeners() is used.
      */
    PBoolean StartListener(
      const OpalTransportAddress & iface ///<  Address of interface to listen on.
    );

    /**Add a listener to the endoint.
       This allows for the automatic creating of incoming call connections. An
       application should use OnConnectionEstablished() to monitor when calls
       have arrived and been successfully negotiated.
      */
    PBoolean StartListener(
      OpalListener * listener ///<  Transport dependent listener.
    );

    /**Get the default listeners for the endpoint type.
       Default behaviour returns empty list if defaultSignalPort is zero, else
       one entry using tcp and INADDR_ANY, eg tcp$*:1720
      */
    virtual PStringArray GetDefaultListeners() const;
	
	/**Get comma separated list of transport protocols to create if
       no explicit listeners started.
      */
    virtual PString GetDefaultTransport() const;

    /**Find a listener given the transport address.
      */
    OpalListener * FindListener(
        const OpalTransportAddress & iface ///<  Address of interface we may be listening on.
    );
	
	 /** Find a listener that is compatible with the specified protocol
     */
    bool FindListenerForProtocol(
      const char * proto,         ///< Protocol to findlistener, e.g "tcp" or "udp"
      OpalTransportAddress & addr ///< Address of listner interface
    );

    /**Stop a listener given the transport address.
       Returns PTrue if a listener was on that interface, and was stopped.
      */
    PBoolean StopListener(
        const OpalTransportAddress & iface ///<  Address of interface we may be listening on.
    );

    /**Remove a listener from the endoint.
       If the listener parameter is NULL then all listeners are removed.
      */
    PBoolean RemoveListener(
      OpalListener * listener ///<  Transport dependent listener.
    );

    /**Return a list of the transport addresses for all listeners on this endpoint
      */
    OpalTransportAddressArray GetInterfaceAddresses(
      PBoolean excludeLocalHost = PTrue,       ///<  Flag to exclude 127.0.0.1
      const OpalTransport * associatedTransport = NULL
                          ///<  Associated transport for precedence and translation
    );

    /**Handle new incoming connection from listener.

       A return value of PTrue indicates that the transport object should be
       deleted by the caller. PFalse indicates that something else (eg the
       connection) has taken over responsibility for deleting the transport.

       The default behaviour just returns PTrue.
      */
    virtual PBoolean NewIncomingConnection(
      OpalTransport * transport  ///<  Transport connection came in on
    );
  //@}
  
  /**Call back for a new connection has been constructed.
       This is called after CreateConnection has returned a new connection.
       It allows an application to make any custom adjustments to the
       connection before it begins to process the protocol. behind it.
      */
    virtual void OnNewConnection(
      OpalCall & call,              ///< Call that owns the newly created connection.
      OpalConnection & connection   ///< New connection just created
    );
  //@}

    /**Callback for outgoing connection, it is invoked after OpalLineConnection::SetUpConnection
       This function allows the application to set up some parameters or to log some messages
       */
    virtual PBoolean OnSetUpConnection(OpalConnection &connection);
    
    /**Call back for answering an incoming call.
       This function is used for an application to control the answering of
       incoming calls.

       If PTrue is returned then the connection continues. If PFalse then the
       connection is aborted.

       Note this function should not block for any length of time. If the
       decision to answer the call may take some time eg waiting for a user to
       pick up the phone, then AnswerCallPending or AnswerCallDeferred should
       be returned.

       If an application overrides this function, it should generally call the
       ancestor version to complete calls. Unless the application completely
       takes over that responsibility. Generally, an application would only
       intercept this function if it wishes to do some form of logging. For
       this you can obtain the name of the caller by using the function
       OpalConnection::GetRemotePartyName().

       The default behaviour calls the OpalManager function of the same name.
     */
    virtual PBoolean OnIncomingConnection(
      OpalConnection & connection,  ///<  Connection that is calling
      unsigned options,             ///<  options for new connection (can't use default value as overrides will fail)
      OpalConnection::StringOptions * stringOptions
    );

    /**Call back for remote party is now responsible for completing the call.
       This function is called when the remote system has been contacted and it
       has accepted responsibility for completing, or failing, the call. This
       is distinct from OnAlerting() in that it is not known at this time if
       anything is ringing. This indication may be used to distinguish between
       "transport" level error, in which case another host may be tried, and
       that finalising the call has moved "upstream" and the local system has
       no more to do but await a result.

       If an application overrides this function, it should generally call the
       ancestor version for correct operation.

       The default behaviour calls the OpalManager function of the same name.
     */
    virtual void OnProceeding(
      OpalConnection & connection   ///<  Connection that is proceeeding
    );

    /**Call back for remote party being alerted.
       This function is called after the connection is informed that the
       remote endpoint is "ringing". Generally some time after the
       MakeConnection() function was called, this is function is called.

       If PFalse is returned the connection is aborted.

       If an application overrides this function, it should generally call the
       ancestor version for correct operation. An application would typically
       only intercept this function if it wishes to do some form of logging.
       For this you can obtain the name of the caller by using the function
       OpalConnection::GetRemotePartyName().

       The default behaviour calls the OpalManager function of the same name.
     */
    virtual void OnAlerting(
      OpalConnection & connection   ///<  Connection that is alerting
    );

    /**Call back for answering an incoming call.
       This function is called after the connection has been acknowledged
       but before the connection is established

       This gives the application time to wait for some event before
       signalling to the endpoint that the connection is to proceed. For
       example the user pressing an "Answer call" button.

       If AnswerCallDenied is returned the connection is aborted and the
       connetion specific end call PDU is sent. If AnswerCallNow is returned 
       then the connection proceeding, Finally if AnswerCallPending is returned then the
       protocol negotiations are paused until the AnsweringCall() function is
       called.

       The default behaviour simply returns AnswerNow.
     */
    virtual OpalConnection::AnswerCallResponse OnAnswerCall(
      OpalConnection & connection,    ///<  connection that is being answered
       const PString & caller         ///<  caller
    );

    /**A call back function whenever a connection is "connected".
       This indicates that a connection to an endpoint was connected. That
       is the endpoint received acknowledgement via whatever protocol it uses
       that the connection may now start media streams.

       In the context of H.323 this means that the CONNECT pdu has been
       received.

       The default behaviour calls the OpalManager function of the same name.
      */
    virtual void OnConnected(
      OpalConnection & connection   ///<  Connection that was established
    );

    /**A call back function whenever a connection is established.
       This indicates that a connection to an endpoint was established. This
       usually occurs after OnConnected() and indicates that the connection
       is both connected and has media flowing.

       In the context of H.323 this means that the signalling and control
       channels are open and the TerminalCapabilitySet and MasterSlave
       negotiations are complete.

       The default behaviour does nothing.
      */
    virtual void OnEstablished(
      OpalConnection & connection   ///<  Connection that was established
    );

    /**A call back function whenever a connection is broken.
       This function can do any internal cleaning up and waiting on background
       threads that may be using the connection object.

       Note that there is not a one to one relationship with the
       OnEstablishedConnection() function. This function may be called without
       that function being called. For example if MakeConnection() was used
       but the call never completed.

       Classes that override this function should make sure they call the
       ancestor version for correct operation.

       An application will not typically call this function as it is used by
       the OpalManager during a release of the connection.

       The default behaviour removes the connection from the internal database
       and calls the OpalManager function of the same name.
      */
    virtual void OnReleased(
      OpalConnection & connection   ///<  Connection that was established
    );

    /**A call back function whenever a connection is "held" or "retrieved".
       This indicates that a connection to an endpoint was held, or
       retrieved, either locally or by the remote endpoint.

       The default behaviour calls the OpalManager function of the same name.
      */
    virtual void OnHold(
      OpalConnection & connection,   ///<  Connection that was held/retrieved
      bool fromRemote,               ///<  Indicates remote has held local connection
      bool onHold                    ///<  Indicates have just been held/retrieved.
    );

    /**A call back function whenever a connection is forwarded.

       The default behaviour does nothing.
      */
    virtual PBoolean OnForwarded(
      OpalConnection & connection,  ///<  Connection that was held
      const PString & remoteParty         ///<  The new remote party
    );

    /**Clear a call.
       This finds the call by using the token then calls the OpalCall::Clear()
       function on it. All connections are released, and the conenctions and
       call disposed of. Note that this function returns quickly and the
       disposal happens at some later time by a background thread. This it is
       safe to call this function from anywhere.
      */
    virtual PBoolean ClearCall(
      const PString & token,    ///<  Token for identifying connection
      OpalConnection::CallEndReason reason = OpalConnection::EndedByLocalUser, ///<  Reason for call clearing
      PSyncPoint * sync = NULL  ///<  Sync point to wait on.
    );

    /**Clear a current connection.
       This hangs up the connection to a remote endpoint. Note that these functions
       are synchronous
      */
    virtual PBoolean ClearCallSynchronous(
      const PString & token,    ///<  Token for identifying connection
      OpalConnection::CallEndReason reason = OpalConnection::EndedByLocalUser, ///<  Reason for call clearing
      PSyncPoint * sync = NULL  ///<  Sync point to wait on.
    );

    /**Clear all current connections.
       This hangs up all the connections to remote endpoints. The wait
       parameter is used to wait for all the calls to be cleared and their
       memory usage cleaned up before returning. This is typically used in
       the destructor for your descendant of H323EndPoint.
      */
    virtual void ClearAllCalls(
      OpalConnection::CallEndReason reason = OpalConnection::EndedByLocalUser, ///<  Reason for call clearing
      PBoolean wait = PTrue   ///<  Flag for wait for calls to e cleared.
    );

    /**Get all calls current on the endpoint.
      */
    PStringList GetAllConnections();
    
    /** Get calls count on the endpoint
      */
    PINDEX GetConnectionCount() const;

    /**Determine if a connection is active.
      */
    virtual PBoolean HasConnection(
      const PString & token   ///<  Token for identifying connection
    );

    /**Destroy the connection.
      */
    virtual void DestroyConnection(
      OpalConnection * connection  ///<  Connection to destroy
    );
  //@}

  /**@name Media Streams management */
  //@{
    /**Get the data formats this endpoint is capable of operating.
       This provides a list of media data format names that may be used by an
       OpalMediaStream may be created by a connection from this endpoint.

       Note that a specific connection may not actually support all of the
       media formats returned here, but should return no more.

       The default behaviour is pure.
      */
    virtual OpalMediaFormatList GetMediaFormats() const = 0;

    /**Adjust media formats available on a connection.
       This is called by a connection after it has called
       OpalCall::GetMediaFormats() to get all media formats that it can use so
       that an application may remove or reorder the media formats before they
       are used to open media streams.

       The default behaviour calls the OpalManager function of the same name.
      */
    virtual void AdjustMediaFormats(
      bool local,                         ///<  Media formats a local ones to be presented to remote
      const OpalConnection & connection,  ///<  Connection that is about to use formats
      OpalMediaFormatList & mediaFormats  ///<  Media formats to use
    ) const;

    /**Call back when opening a media stream.
       This function is called when a connection has created a new media
       stream according to the logic of its underlying protocol.

       The usual requirement is that media streams are created on all other
       connections participating in the call and all of the media streams are
       attached to an instance of an OpalMediaPatch object that will read from
       one of the media streams passing data to the other media streams.

       The default behaviour calls the OpalManager function of the same name.
      */
    virtual PBoolean OnOpenMediaStream(
      OpalConnection & connection,  ///<  Connection that owns the media stream
      OpalMediaStream & stream      ///<  New media stream being opened
    );

    /**Call back for closed a media stream.

       The default behaviour calls the OpalManager function of the same name.
      */
    virtual void OnClosedMediaStream(
      const OpalMediaStream & stream     ///<  Media stream being closed
    );

    /**Create an PVideoInputDevice for a source media stream.
      */
    virtual PBoolean CreateVideoInputDevice(
      const OpalConnection & connection,    ///<  Connection needing created video device
      const OpalMediaFormat & mediaFormat,  ///<  Media format for stream
      PVideoInputDevice * & device,         ///<  Created device
      PBoolean & autoDelete                     ///<  Flag for auto delete device
    );

    /**Create an PVideoOutputDevice for a sink media stream or the preview
       display for a source media stream.
      */
    virtual PBoolean CreateVideoOutputDevice(
      const OpalConnection & connection,    ///<  Connection needing created video device
      const OpalMediaFormat & mediaFormat,  ///<  Media format for stream
      PBoolean preview,                         ///<  Flag indicating is a preview output
      PVideoOutputDevice * & device,        ///<  Created device
      PBoolean & autoDelete                     ///<  Flag for auto delete device
    );

  //@}

  /**@name User indications */
  //@{
    /**Call back for remote enpoint has sent user input as a string.

       The default behaviour calls the OpalManager function of the same name.
      */
    virtual void OnUserInputString(
      OpalConnection & connection,  ///<  Connection input has come from
      const PString & value   ///<  String value of indication
    );

    /**Call back for remote enpoint has sent user input.
       If duration is zero then this indicates the beginning of the tone. If
       duration is non-zero then it indicates the end of the tone output.

       The default behaviour calls the OpalManager function of the same name.
      */
    virtual void OnUserInputTone(
      OpalConnection & connection,  ///<  Connection input has come from
      char tone,                    ///<  Tone received
      int duration                  ///<  Duration of tone
    );

    /**Read a sequence of user indications from connection with timeouts.
      */
    virtual PString ReadUserInput(
      OpalConnection & connection,        ///<  Connection to read input from
      const char * terminators = "#\r\n", ///<  Characters that can terminte input
      unsigned lastDigitTimeout = 4,      ///<  Timeout on last digit in string
      unsigned firstDigitTimeout = 30     ///<  Timeout on receiving any digits
    );
  //@}

  /**@name Other services */
  //@{
    /**Callback called when Message Waiting Indication (MWI) is received
     */
    virtual void OnMWIReceived (
      const PString & party,                ///< Name of party MWI is for
      OpalManager::MessageWaitingType type, ///< Type of message that is waiting
      const PString & extraInfo             ///< Addition information on the MWI
    );

    /** Execute garbage collection for endpoint.
        Returns PTrue if all garbage has been collected.
        Default behaviour deletes the objects in the connectionsActive list.
      */
    virtual PBoolean GarbageCollection();
  //@}

  /**@name Member variable access */
  //@{
    /**Get the manager for this endpoint.
     */
    OpalManager & GetManager() const;

    /**Get the protocol prefix name for the endpoint.
      */
    const PString & GetPrefixName() const;

    /**Get an indication of if this endpoint has particular option.
      */
    PBoolean HasAttribute(Attributes opt) const;

    /**Get the default signal port for this endpoint.
     */
    WORD GetDefaultSignalPort() const;

    /**Get the product info for all endpoints.
      */
    const OpalProductInfo & GetProductInfo() const;

    /**Set the product info for all endpoints.
      */
    void SetProductInfo(
      const OpalProductInfo & info
    );

    /**Get the default local party name for all connections on this endpoint.
      */
    const PString & GetDefaultLocalPartyName() const;

    /**Set the default local party name for all connections on this endpoint.
      */
    virtual void SetDefaultLocalPartyName(
      const PString & name  /// Name for local party
    );

    /**Get the default local display name for all connections on this endpoint.
      */
    const PString & GetDefaultDisplayName() const;

    /**Set the default local display name for all connections on this endpoint.
      */
    void SetDefaultDisplayName(const PString & name);

    /**Get the initial bandwidth parameter.
     */
    unsigned GetInitialBandwidth() const;

    /**Get the initial bandwidth parameter.
     */
    void SetInitialBandwidth(unsigned bandwidth);

    /**Get the list of listeners (incoming call transports) for this endpoint.
     */
	const OpalListenerList & GetListeners() const;
/*     SIP_PYLIST GetListeners() const;
%MethodCode
	PyObject *l;
	PyObject *pobj;
	SIP_SSIZE_T i=0;
	const OpalListenerList & lst = sipCpp->GetListeners();

	if((l = PyList_New(lst.GetSize())) == NULL)
		return NULL;

	for(OpalListenerList::const_iterator iter = lst.begin(); iter != lst.end(); iter++)
	{
		const OpalListener& listener = *iter;

		// Wrap the OpalListener object - the wrapped object will be owned by C++
		if ((pobj = sipConvertFromType((void*)&listener, sipType_OpalListener, NULL)) == NULL)
		{
            // There was an error so garbage collect the Python list.
			Py_DECREF(l);
			return NULL;
		}
		// Add the wrapper to the list.
		PyList_SET_ITEM(l, i++, pobj);
	}

	sipRes = l;
%End */
		

    /**Get the default options for created connections.
      */
    const OpalConnection::StringOptions & GetDefaultStringOptions() const;

    /**Set the default options for created connections.
      */
    void SetDefaultStringOptions(const OpalConnection::StringOptions & opts);

    /**Set the default option for created connections.
      */
    void SetDefaultStringOption(const PCaselessString & key, const PString & data);

    /**Get the default mode for sending User Input Indications.
      */
    OpalConnection::SendUserInputModes GetSendUserInputMode() const;

    /**Set the default mode for sending User Input Indications.
      */
    void SetSendUserInputMode(OpalConnection::SendUserInputModes mode);
  //@}
		
};

/// Test for if string is a valid E.164 number
bool OpalIsE164(
  const PString & number,   ///< Number to inspect
  bool strict = false     ///< Strict interpretation, or allow leading '+'
);
