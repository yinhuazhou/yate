/**
 * ysipchan.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * Yet Another Sip Channel
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2004 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <yatephone.h>
#include <yatesip.h>

#include <string.h>


using namespace TelEngine;

/* Yate Payloads for the AV profile */
static TokenDict dict_payloads[] = {
    { "mulaw",   0 },
    { "alaw",    8 },
    { "gsm",     3 },
    { "lpc10",   7 },
    { "slin",   11 },
    { "g726",    2 },
    { "g722",    9 },
    { "g723",    4 },
    { "g728",   15 },
    { "g729",   18 },
};

/* SDP Payloads for the AV profile */
static TokenDict dict_rtpmap[] = {
    { "PCMU/8000",     0 },
    { "PCMA/8000",     8 },
    { "GSM/8000",      3 },
    { "LPC/8000",      7 },
    { "L16/8000",     11 },
    { "G726-32/8000",  2 },
    { "G722/8000",     9 },
    { "G723/8000",     4 },
    { "G728/8000",    15 },
    { "G729/8000",    18 },
};

static Configuration s_cfg;

class YateUDPParty : public SIPParty
{
public:
    YateUDPParty(Socket* sock, const SocketAddr& addr, int local);
    ~YateUDPParty();
    virtual void transmit(SIPEvent* event);
    virtual const char* getProtoName() const;
    virtual bool setParty(const URI& uri);
protected:
    Socket* m_sock;
    SocketAddr m_addr;
};

class YateSIPEndPoint;

class YateSIPEngine : public SIPEngine
{
public:
    YateSIPEngine(YateSIPEndPoint* ep);
    virtual bool buildParty(SIPMessage* message);
private:
    YateSIPEndPoint* m_ep;
};

class YateSIPEndPoint : public Thread
{
public:
    YateSIPEndPoint();
    ~YateSIPEndPoint();
    bool Init(void);
   // YateSIPConnection *findconn(int did);
  //  void terminateall(void);
    void run(void);
    bool incoming(SIPEvent* e, SIPTransaction* t);
    void invite(SIPEvent* e, SIPTransaction* t);
    void regreq(SIPEvent* e, SIPTransaction* t);
    bool buildParty(SIPMessage* message, const char* host = 0, int port = 0);
    inline YateSIPEngine* engine() const
	{ return m_engine; }
    inline int port() const
	{ return m_port; }
    inline Socket* socket() const
	{ return m_sock; }
private:
    int m_port;
    Socket* m_sock;
    YateSIPEngine *m_engine;

};

class YateSIPConnection : public Channel
{
public:
    enum {
	Incoming,
	Outgoing,
	Ringing,
	Established,
	Cleared,
    };
    YateSIPConnection(Message& msg, SIPTransaction* tr);
    YateSIPConnection(Message& msg, const String& uri, const char* target = 0);
    ~YateSIPConnection();
    virtual void disconnected(bool final, const char *reason);
    bool process(SIPEvent* ev);
    void ringing(Message* msg = 0);
    void answered(Message* msg = 0);
    void doBye(SIPTransaction* t);
    void doCancel(SIPTransaction* t);
    void reInvite(SIPTransaction* t);
    void hangup();
    inline const SIPDialog& dialog() const
	{ return m_dialog; }
    inline void setStatus(const char *status, int state = -1)
	{ m_status = status; if (state >= 0) m_state = state; }
    inline void setReason(const char* str = "Request Terminated", int code = 487)
	{ m_reason = str; m_reasonCode = code; }
    inline SIPTransaction* getTransaction() const
	{ return m_tr; }
    inline const String& getHost() const
	{ return m_host; }
    inline int getPort() const
	{ return m_port; }
    static YateSIPConnection* find(const String& id);
    static YateSIPConnection* find(const SIPDialog& dialog);
private:
    void clearTransaction();
    SDPBody* createSDP(const char* addr, const char* port, const char* formats, const char* format = 0);
    SDPBody* createPasstroughSDP(Message &msg);
    SDPBody* createRtpSDP(SIPMessage* msg, const char* formats);
    SDPBody* createRtpSDP(bool start = false);
    bool startRtp();
    SIPTransaction* m_tr;
    bool m_hungup;
    bool m_byebye;
    int m_state;
    String m_reason;
    int m_reasonCode;
    SIPDialog m_dialog;
    URI m_uri;
    String m_rtpid;
    String m_rtpAddr;
    String m_rtpPort;
    String m_rtpFormat;
    String m_rtpLocal;
    int m_rtpSession;
    int m_rtpVersion;
    String m_formats;
    String m_host;
    int m_port;
};

class SipMsgThread : public Thread
{
public:
    SipMsgThread(SIPTransaction* tr, Message* msg)
	: Thread("SipMsgThread"), m_tr(tr), m_msg(msg)
	{ m_tr->ref(); m_id = m_tr->getCallID(); }
    virtual void run();
    virtual void cleanup();
    bool route();
    inline static int count()
	{ return s_count; }
    inline static int routed()
	{ return s_routed; }
private:
    SIPTransaction* m_tr;
    Message* m_msg;
    String m_id;
    static int s_count;
    static int s_routed;
};

class SIPDriver : public Driver
{
public:
    SIPDriver();
    ~SIPDriver();
    virtual void initialize();
    inline YateSIPEndPoint* ep() const
	{ return m_endpoint; }
private:
    YateSIPEndPoint *m_endpoint;
};

static void parseSDP(SDPBody* sdp, String& addr, String& port, String& formats)
{
    const NamedString* c = sdp->getLine("c");
    if (c) {
	String tmp(*c);
	if (tmp.startSkip("IN IP4")) {
	    tmp.trimBlanks();
	    // Handle the case media is muted
	    if (tmp == "0.0.0.0")
		tmp.clear();
	    addr = tmp;
	}
    }
    c = sdp->getLine("m");
    if (c) {
	String tmp(*c);
	if (tmp.startSkip("audio")) {
	    int var = 0;
	    tmp >> var >> " RTP/AVP";
	    if (var > 0)
		port = var;
	    String fmt;
	    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
	    while (tmp[0] == ' ') {
		var = -1;
		tmp >> " " >> var;
		const char* payload = lookup(var,dict_payloads);
		if (payload && s_cfg.getBoolValue("codecs",payload,defcodecs)) {
		    if (fmt)
			fmt << ",";
		    fmt << payload;
		}
	    }
	    formats = fmt;
	}
    }
}

static SIPDriver plugin;
static ObjList s_calls;
static Mutex s_mutex;

YateUDPParty::YateUDPParty(Socket* sock, const SocketAddr& addr, int local)
    : m_sock(sock), m_addr(addr)
{
    m_local = "localhost";
    m_localPort = local;
    m_party = m_addr.host();
    m_partyPort = m_addr.port();
    Socket s(PF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (s.valid()) {
	if (s.connect(m_addr)) {
	    SocketAddr laddr;
	    if (s.getSockName(laddr))
		m_local = laddr.host();
	}
    }
    Debug(DebugAll,"YateUDPParty local %s:%d party %s:%d",
	m_local.c_str(),m_localPort,
	m_party.c_str(),m_partyPort);
}

YateUDPParty::~YateUDPParty()
{
    m_sock = 0;
}

void YateUDPParty::transmit(SIPEvent* event)
{
    Debug(DebugAll,"Sending to %s:%d",m_addr.host().c_str(),m_addr.port());
    m_sock->sendTo(
	event->getMessage()->getBuffer().data(),
	event->getMessage()->getBuffer().length(),
	m_addr
    );
}

const char* YateUDPParty::getProtoName() const
{
    return "UDP";
}

bool YateUDPParty::setParty(const URI& uri)
{
    if (m_partyPort && m_party && s_cfg.getBoolValue("general","ignorevia"))
	return true;
    if (uri.getHost().null())
	return false;
    int port = uri.getPort();
    if (port <= 0)
	port = 5060;
    if (!m_addr.host(uri.getHost())) {
	Debug("YateUDPParty",DebugWarn,"Could not resolve name '%s' [%p]",
	    uri.getHost().safe(),this);
	return false;
    }
    m_addr.port(port);
    m_party = uri.getHost();
    m_partyPort = port;
    Debug("YateUDPParty",DebugInfo,"New party is %s:%d (%s:%d) [%p]",
	m_party.c_str(),m_partyPort,
	m_addr.host().c_str(),m_addr.port(),
	this);
    return true;
}

YateSIPEngine::YateSIPEngine(YateSIPEndPoint* ep)
    : m_ep(ep)
{
    addAllowed("INVITE");
    addAllowed("BYE");
    addAllowed("CANCEL");
    if (s_cfg.getBoolValue("general","registrar"))
	addAllowed("REGISTER");
}

bool YateSIPEngine::buildParty(SIPMessage* message)
{
    return m_ep->buildParty(message);
}

YateSIPEndPoint::YateSIPEndPoint()
    : Thread("YSIP EndPoint"), m_sock(0), m_engine(0)
{
    Debug(DebugAll,"YateSIPEndPoint::YateSIPEndPoint() [%p]",this);
}

YateSIPEndPoint::~YateSIPEndPoint()
{
    Debug(DebugAll,"YateSIPEndPoint::~YateSIPEndPoint() [%p]",this);
    plugin.channels().clear();
    if (m_engine) {
	// send any pending events
	while (m_engine->process())
	    ;
	delete m_engine;
	m_engine = 0;
    }
    if (m_sock) {
	delete m_sock;
	m_sock = 0;
    }
}

bool YateSIPEndPoint::buildParty(SIPMessage* message, const char* host, int port)
{
    if (message->isAnswer())
	return false;
    URI uri(message->uri);
    if (!host) {
	host = uri.getHost().safe();
	if (port <= 0)
	    port = uri.getPort();
    }
    if (port <= 0)
	port = 5060;
    SocketAddr addr(AF_INET);
    if (!addr.host(host)) {
	Debug(DebugWarn,"Error resolving name '%s'",host);
	return false;
    }
    addr.port(port);
    Debug(DebugAll,"built addr: %s:%d",
	addr.host().c_str(),addr.port());
    message->setParty(new YateUDPParty(m_sock,addr,m_port));
    return true;
}

bool YateSIPEndPoint::Init()
{
    /*
     * This part have been taken from libiax after i have lost my sip driver for bayonne
     */
    if (m_sock) {
	Debug(DebugInfo,"Already initialized.");
	return true;
    }

    m_sock = new Socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (!m_sock->valid()) {
	Debug(DebugGoOn,"Unable to allocate UDP socket\n");
	return false;
    }
    
    SocketAddr addr(AF_INET);
    addr.port(s_cfg.getIntValue("general","port",5060));
    addr.host(s_cfg.getValue("general","addr","0.0.0.0"));

    if (!m_sock->bind(addr)) {
	Debug(DebugWarn,"Unable to bind to preferred port - using random one instead");
	addr.port(0);
	if (!m_sock->bind(addr)) {
	    Debug(DebugGoOn,"Unable to bind to any port");
	    return false;
	}
    }
    
    if (!m_sock->getSockName(addr)) {
	Debug(DebugGoOn,"Unable to figure out what I'm bound to");
	return false;
    }
    if (!m_sock->setBlocking(false)) {
	Debug(DebugGoOn,"Unable to set non-blocking mode");
	return false;
    }
    Debug(DebugInfo,"SIP Started on %s:%d\n", addr.host().safe(), addr.port());
    m_port = addr.port();
    m_engine = new YateSIPEngine(this);
    return true;
}

void YateSIPEndPoint::run()
{
    struct timeval tv;
    char buf[1500];
    SocketAddr addr;
    /* Watch stdin (fd 0) to see when it has input. */
    for (;;)
    {
	/* Wait up to 20000 microseconds. */
	tv.tv_sec = 0;
	tv.tv_usec = 20000;
	bool ok = false;
	m_sock->select(&ok,0,0,&tv);
	if (ok)
	{
	    // we can read the data
	    int res = m_sock->recvFrom(buf,sizeof(buf)-1,addr);
	    if (res <= 0) {
		if (!m_sock->canRetry()) {
		    Debug(DebugGoOn,"SIP error on read: %d\n", m_sock->error());
		}
	    } else {
		// we got already the buffer and here we start to do "good" stuff
		buf[res]=0;
		m_engine->addMessage(new YateUDPParty(m_sock,addr,m_port),buf,res);
	    //	Output("res %d buf %s",res,buf);
	    }
	}
//	m_engine->process();
	SIPEvent* e = m_engine->getEvent();
	// hack: use a loop so we can use break and continue
	for (; e; m_engine->processEvent(e),e = 0) {
	    if (!e->getTransaction())
		continue;
	    YateSIPConnection* conn = static_cast<YateSIPConnection*>(e->getTransaction()->getUserData());
	    if (conn) {
		if (conn->process(e)) {
		    delete e;
		    break;
		}
		else
		    continue;
	    }
	    if ((e->getState() == SIPTransaction::Trying) &&
		!e->isOutgoing() && incoming(e,e->getTransaction())) {
		delete e;
		break;
	    }
	}
    }
}

bool YateSIPEndPoint::incoming(SIPEvent* e, SIPTransaction* t)
{
    if (e->getTransaction()->isInvite())
	invite(e,t);
    else if (t->getMethod() == "BYE") {
	YateSIPConnection* conn = YateSIPConnection::find(t->getCallID());
	if (conn)
	    conn->doBye(t);
	else
	    t->setResponse(481,"Call/Transaction Does Not Exist");
    }
    else if (t->getMethod() == "CANCEL") {
	YateSIPConnection* conn = YateSIPConnection::find(t->getCallID());
	if (conn)
	    conn->doCancel(t);
	else
	    t->setResponse(481,"Call/Transaction Does Not Exist");
    }
    else if (t->getMethod() == "REGISTER")
	regreq(e,t);
    else
	return false;
    return true;
}

static int s_maxqueue = 5;

void YateSIPEndPoint::invite(SIPEvent* e, SIPTransaction* t)
{
    if (Engine::exiting()) {
	Debug(DebugWarn,"Dropping call, engine is exiting");
	e->getTransaction()->setResponse(500, "Server Shutting Down");
	return;
    }

    if (e->getMessage()->getParam("To","tag")) {
	SIPDialog dlg(*e->getMessage());
	YateSIPConnection* conn = YateSIPConnection::find(dlg);
	if (conn)
	    conn->reInvite(t);
	else {
	    Debug(DebugWarn,"Got re-INVITE for missing dialog");
	    e->getTransaction()->setResponse(481, "Call/Transaction Does Not Exist");
	}
	return;
    }

    int cnt = SipMsgThread::count();
    if (cnt > s_maxqueue) {
	Debug(DebugWarn,"Dropping call, there are already %d waiting",cnt);
	e->getTransaction()->setResponse(503, "Service Unavailable");
	return;
    }

    String callid(t->getCallID());
    URI uri(t->getURI());
    const HeaderLine* hl = e->getMessage()->getHeader("From");
    URI from(hl ? *hl : String::empty());
    Message *m = new Message("call.route");
    m->addParam("driver","sip");
    m->addParam("id","sip/" + callid);
    m->addParam("caller",from.getUser());
    m->addParam("called",uri.getUser());
    m->addParam("sip_uri",uri);
    m->addParam("sip_from",from);
    m->addParam("sip_callid",callid);
    m->addParam("sip_contact",e->getMessage()->getHeaderValue("Contact"));
    m->addParam("sip_user-agent",e->getMessage()->getHeaderValue("User-Agent"));
    SIPParty* party = e->getParty();
    if (party) {
	m->addParam("xsip_received",party->getPartyAddr());
	m->addParam("xsip_rport",String(party->getPartyPort()));
    }
    if (e->getMessage()->body && e->getMessage()->body->isSDP()) {
	String addr,port,formats;
	parseSDP(static_cast<SDPBody*>(e->getMessage()->body),addr,port,formats);
	if (addr) {
	    m->addParam("rtp_forward","possible");
	    m->addParam("rtp_addr",addr);
	    m->addParam("rtp_port",port);
	    m->addParam("formats",formats);
	}
    }
    SipMsgThread *thr = new SipMsgThread(t,m);
    if (!thr->startup()) {
	Debug(DebugWarn,"Error starting routing thread %p ! [%p]",thr,this);
	delete thr;
	t->setResponse(500, "Server Internal Error");
    }
}

void YateSIPEndPoint::regreq(SIPEvent* e, SIPTransaction* t)
{
    if (Engine::exiting()) {
	Debug(DebugWarn,"Dropping request, engine is exiting");
	e->getTransaction()->setResponse(500, "Server Shutting Down");
	return;
    }
    const HeaderLine* hl = e->getMessage()->getHeader("Contact");
    if (!hl) {
	e->getTransaction()->setResponse(400, "Bad Request");
	return;
    }
    URI addr(*hl);
    Message *m = new Message("user.register");
    m->addParam("username",addr.getUser());
    m->addParam("driver","sip");
    m->addParam("data","sip/" + addr);
    bool dereg = false;
    hl = e->getMessage()->getHeader("Expires");
    if (hl) {
	m->addParam("expires",*hl);
	if (*hl == "0") {
	    *m = "user.unregister";
	    dereg = true;
	}
    }
    // Always OK deregistration attempts
    if (Engine::dispatch(m) || dereg)
	e->getTransaction()->setResponse(200, "OK");
    else
	e->getTransaction()->setResponse(404, "Not Found");
    m->destruct();
}

static Mutex s_route;
int SipMsgThread::s_count = 0;
int SipMsgThread::s_routed = 0;

YateSIPConnection* YateSIPConnection::find(const String& id)
{
    Debug("YateSIPConnection",DebugAll,"finding '%s'",id.c_str());
    ObjList* l = s_calls.find(id);
    return l ? static_cast<YateSIPConnection*>(l->get()) : 0;
}

YateSIPConnection* YateSIPConnection::find(const SIPDialog& dialog)
{
    Debug("YateSIPConnection",DebugAll,"finding dialog '%s'",dialog.c_str());
    ObjList* l = &s_calls;
    for (; l; l = l->next()) {
	YateSIPConnection* c = static_cast<YateSIPConnection*>(l->get());
	if (c && (c->dialog() == dialog))
	    return c;
    }
    return 0;
}

// Incoming call constructor - after call.route but before call.execute
YateSIPConnection::YateSIPConnection(Message& msg, SIPTransaction* tr)
    : Channel(plugin,0,false),
      m_tr(tr), m_hungup(false), m_byebye(true), m_state(Incoming),
      m_rtpSession(0), m_rtpVersion(0), m_port(0)
{
    Debug(DebugAll,"YateSIPConnection::YateSIPConnection(%p) [%p]",tr,this);
    setReason();
    s_mutex.lock();
    m_tr->ref();
    m_id = *m_tr->initialMessage();
    m_host = m_tr->initialMessage()->getParty()->getPartyAddr();
    m_port = m_tr->initialMessage()->getParty()->getPartyPort();
    m_uri = m_tr->initialMessage()->getHeader("From");
    m_uri.parse();
    m_tr->setUserData(this);
    s_calls.append(this);
    s_mutex.unlock();
    m_rtpAddr = msg.getValue("rtp_addr");
    m_rtpPort = msg.getValue("rtp_port");
    m_formats = msg.getValue("formats");
    int q = m_formats.find(',');
    m_rtpFormat = m_formats.substr(0,q);
    Debug(DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
    Message *ms = new Message("chan.startup");
    ms->addParam("driver","sip");
    ms->addParam("id",id());
    String addr(m_host);
    addr << ":" << m_port;
    ms->addParam("address",addr);
    ms->addParam("direction","incoming");
    Engine::enqueue(ms);
}

// Outgoing call constructor - in call.execute handler
YateSIPConnection::YateSIPConnection(Message& msg, const String& uri, const char* target)
    : Channel(plugin,0,true),
      m_tr(0), m_hungup(false), m_byebye(true), m_state(Outgoing), m_uri(uri),
      m_rtpSession(0), m_rtpVersion(0), m_port(0)
{
    Debug(DebugAll,"YateSIPConnection::YateSIPConnection(%p,'%s') [%p]",
	&msg,uri.c_str(),this);
    targetid(target);
    setReason();
    m_uri.parse();
    SIPMessage* m = new SIPMessage("INVITE",uri);
    String p(msg.getValue("port"));
    plugin.ep()->buildParty(m,msg.getValue("host"),p.toInteger());
    m->complete(plugin.ep()->engine(),msg.getValue("caller"),msg.getValue("domain"));
    m_host = m->getParty()->getPartyAddr();
    m_port = m->getParty()->getPartyPort();
    m_id = *m;
    SDPBody* sdp = createPasstroughSDP(msg);
    if (!sdp)
	sdp = createRtpSDP(m,msg.getValue("formats"));
    m->setBody(sdp);
    m_tr = plugin.ep()->engine()->addMessage(m);
    m->deref();
    if (m_tr) {
	m_tr->ref();
	m_tr->setUserData(this);
    }
    s_mutex.lock();
    s_calls.append(this);
    s_mutex.unlock();
    Message *ms = new Message("chan.startup");
    ms->addParam("driver","sip");
    ms->addParam("id",id());
    String addr(m_host);
    addr << ":" << m_port;
    ms->addParam("address",addr);
    ms->addParam("direction","outgoing");
    Engine::enqueue(ms);
}

YateSIPConnection::~YateSIPConnection()
{
    Debug(DebugAll,"YateSIPConnection::~YateSIPConnection() [%p]",this);
    Lock lock(s_mutex);
    s_calls.remove(this,false);
    hangup();
    clearTransaction();
}

void YateSIPConnection::clearTransaction()
{
    if (m_tr) {
	m_tr->setUserData(0);
	if (m_tr->isIncoming()) {
	    m_byebye = false;
	    m_tr->setResponse(m_reasonCode,m_reason.null() ? "Request Terminated" : m_reason.c_str());
	}
	m_tr->deref();
	m_tr = 0;
    }
}

void YateSIPConnection::hangup()
{
    if (m_hungup)
	return;
    m_hungup = true;
    Debug(DebugAll,"YateSIPConnection::hangup() state=%d trans=%p code=%d reason='%s' [%p]",
	m_state,m_tr,m_reasonCode,m_reason.c_str(),this);
    Message *msg = new Message("chan.hangup");
    msg->addParam("driver","sip");
    msg->addParam("id",id());
    if (m_target)
	msg->addParam("targetid",m_target);
    Engine::enqueue(msg);
    msg = 0;
    switch (m_state) {
	case Cleared:
	    clearTransaction();
	    return;
	case Incoming:
	    if (m_tr) {
		clearTransaction();
		return;
	    }
	    break;
	case Outgoing:
	    if (m_tr) {
		SIPMessage* m = new SIPMessage("CANCEL",m_uri);
		plugin.ep()->buildParty(m,m_host,m_port);
		const SIPMessage* i = m_tr->initialMessage();
		m->copyHeader(i,"Via");
		m->copyHeader(i,"From");
		m->copyHeader(i,"To");
		m->copyHeader(i,"Call-ID");
		String tmp;
		tmp << i->getCSeq() << " CANCEL";
		m->addHeader("CSeq",tmp);
		plugin.ep()->engine()->addMessage(m);
		m->deref();
	    }
	    break;
    }
    clearTransaction();
    m_state = Cleared;

    if (m_byebye) {
	m_byebye = false;
	SIPMessage* m = new SIPMessage("BYE",m_uri);
	plugin.ep()->buildParty(m,m_host,m_port);
	m->addHeader("Call-ID",m_id);
	String tmp;
	tmp << "<" << m_id.localURI << ">";
	HeaderLine* hl = new HeaderLine("From",tmp);
	hl->setParam("tag",m_id.localTag);
	m->addHeader(hl);
	tmp.clear();
	tmp << "<" << m_id.remoteURI << ">";
	hl = new HeaderLine("To",tmp);
	hl->setParam("tag",m_id.remoteTag);
	m->addHeader(hl);
	plugin.ep()->engine()->addMessage(m);
	m->deref();
    }
    disconnect();
}

// Creates a SDP from RTP address data present in message
SDPBody* YateSIPConnection::createPasstroughSDP(Message &msg)
{
    String tmp = msg.getValue("rtp_forward");
    msg.clearParam("rtp_forward");
    if (!tmp.toBoolean())
	return 0;
    tmp = msg.getValue("rtp_port");
    int port = tmp.toInteger();
    String addr(msg.getValue("rtp_addr"));
    if (port && addr) {
	SDPBody* sdp = createSDP(addr,tmp,msg.getValue("formats"));
	if (sdp)
	    msg.setParam("rtp_forward","accepted");
	return sdp;
    }
    return 0;
}

// Creates an unstarted external RTP channel from remote addr and builds SDP from it
SDPBody* YateSIPConnection::createRtpSDP(SIPMessage* msg, const char* formats)
{
    Message m("chan.rtp");
    m.addParam("driver","sip");
    m.addParam("id",id());
    m.addParam("direction","bidir");
    m.addParam("remoteip",msg->getParty()->getPartyAddr());
    m.userData(static_cast<DataEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	m_rtpLocal = m.getValue("localip",m_rtpLocal);
	return createSDP(m_rtpLocal,m.getValue("localport"),formats);
    }
    return 0;
}

// Creates a started external RTP channel from remote addr and builds SDP from it
SDPBody* YateSIPConnection::createRtpSDP(bool start)
{
    if (m_rtpAddr.null()) {
	m_rtpid = "-";
	return createSDP(m_rtpLocal,0,m_formats);
    }
    Message m("chan.rtp");
    m.addParam("driver","sip");
    m.addParam("id",id());
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    if (start) {
	m.addParam("remoteport",m_rtpPort);
	m.addParam("format",m_rtpFormat);
    }
    m.userData(static_cast<DataEndpoint *>(this));
    if (Engine::dispatch(m)) {
	m_rtpid = m.getValue("rtpid");
	m_rtpLocal = m.getValue("localip",m_rtpLocal);
	if (start)
	    m_rtpFormat = m.getValue("format");
	return createSDP(m_rtpLocal,m.getValue("localport"),m_formats,m_rtpFormat);
    }
    return 0;
}

// Starts an already created external RTP channel
bool YateSIPConnection::startRtp()
{
    if (m_rtpid.null() || m_rtpid == "-")
	return false;
    Debug(DebugAll,"YateSIPConnection::startRtp() [%p]",this);
    Message m("chan.rtp");
    m.addParam("driver","sip");
    m.addParam("id",id());
    m.addParam("rtpid",m_rtpid);
    m.addParam("direction","bidir");
    m.addParam("remoteip",m_rtpAddr);
    m.addParam("remoteport",m_rtpPort);
    m.addParam("format",m_rtpFormat);
    m.userData(static_cast<DataEndpoint *>(this));
    return Engine::dispatch(m);
}

SDPBody* YateSIPConnection::createSDP(const char* addr, const char* port, const char* formats, const char* format)
{
    Debug(DebugAll,"YateSIPConnection::createSDP('%s','%s','%s') [%p]",
	addr,port,formats,this);
    if (!addr)
	return 0;
    if (m_rtpSession)
	++m_rtpVersion;
    else
	m_rtpVersion = m_rtpSession = Time::now() / 10000000000ULL;
    String owner;
    owner << "yate " << m_rtpSession << " " << m_rtpVersion << " IN IP4 " << addr;
    if (!port) {
	port = "1";
	addr = "0.0.0.0";
    }
    String tmp;
    tmp << "IN IP4 " << addr;
    String frm(format ? format : formats);
    if (frm.null())
	frm = "alaw,mulaw";
    ObjList* l = frm.split(',',false);
    frm = "audio ";
    frm << port << " RTP/AVP";
    ObjList rtpmap;
    ObjList* f = l;
    bool defcodecs = s_cfg.getBoolValue("codecs","default",true);
    for (; f; f = f->next()) {
	String* s = static_cast<String*>(f->get());
	if (s) {
	    int payload = s->toInteger(dict_payloads,-1);
	    if (payload >= 0) {
		const char* map = lookup(payload,dict_rtpmap);
		if (map && s_cfg.getBoolValue("codecs",*s,defcodecs)) {
		    frm << " " << payload;
		    String* tmp = new String("rtpmap:");
		    *tmp << payload << " " << map;
		    rtpmap.append(tmp);
		}
	    }
	}
    }
    delete l;

    // always claim to support telephone events
    frm << " 101";
    rtpmap.append(new String("rtpmap:101 telephone-event/8000"));

    SDPBody* sdp = new SDPBody;
    sdp->addLine("v","0");
    sdp->addLine("o",owner);
    sdp->addLine("s","Session");
    sdp->addLine("c",tmp);
    sdp->addLine("t","0 0");
    sdp->addLine("m",frm);
    for (f = &rtpmap; f; f = f->next()) {
	String* s = static_cast<String*>(f->get());
	if (s)
	    sdp->addLine("a",*s);
    }
    rtpmap.clear();
    return sdp;
}

void YateSIPConnection::disconnected(bool final, const char *reason)
{
    Debug(DebugAll,"YateSIPConnection::disconnected() '%s' [%p]",reason,this);
    if (reason)
	setReason(reason);
    Channel::disconnected(final,reason);
}

bool YateSIPConnection::process(SIPEvent* ev)
{
    Debug(DebugInfo,"YateSIPConnection::process(%p) %s [%p]",
	ev,SIPTransaction::stateName(ev->getState()),this);
    m_id = *ev->getTransaction()->recentMessage();
    if (ev->getState() == SIPTransaction::Cleared) {
	if (m_tr) {
	    Lock lock(s_mutex);
	    Debug(DebugInfo,"YateSIPConnection clearing transaction %p [%p]",
		m_tr,this);
	    m_tr->setUserData(0);
	    m_tr->deref();
	    m_tr = 0;
	}
	if (m_state != Established)
	    hangup();
	return false;
    }
    if (!ev->getMessage() || ev->getMessage()->isOutgoing())
	return false;
    if (ev->getMessage()->body && ev->getMessage()->body->isSDP()) {
	Debug(DebugInfo,"YateSIPConnection got SDP [%p]",this);
	parseSDP(static_cast<SDPBody*>(ev->getMessage()->body),
	    m_rtpAddr,m_rtpPort,m_formats);
	int q = m_formats.find(',');
	m_rtpFormat = m_formats.substr(0,q);
	Debug(DebugAll,"RTP addr '%s' port %s formats '%s' format '%s'",
	    m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());
    }
    if (ev->getMessage()->isAnswer() && ((ev->getMessage()->code / 100) == 2)) {
	setStatus("answered",Established);
	Message *m = new Message("call.answered");
	m->addParam("driver","sip");
	m->addParam("id",id());
	if (m_target)
	    m->addParam("targetid",m_target);
	m->addParam("status","answered");
	if (m_rtpPort && m_rtpAddr && !startRtp()) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_rtpAddr);
	    m->addParam("rtp_port",m_rtpPort);
	    m->addParam("formats",m_formats);
	}
	Engine::enqueue(m);
    }
    if ((m_state < Ringing) && ev->getMessage()->isAnswer() && (ev->getMessage()->code == 180)) {
	setStatus("ringing",Ringing);
	Message *m = new Message("call.ringing");
	m->addParam("driver","sip");
	m->addParam("id",id());
	if (m_target)
	    m->addParam("targetid",m_target);
	m->addParam("status","ringing");
	if (m_rtpPort && m_rtpAddr && !startRtp()) {
	    m->addParam("rtp_forward","yes");
	    m->addParam("rtp_addr",m_rtpAddr);
	    m->addParam("rtp_port",m_rtpPort);
	    m->addParam("formats",m_formats);
	}
	Engine::enqueue(m);
    }
    if (ev->getMessage()->isACK()) {
	Debug(DebugInfo,"YateSIPConnection got ACK [%p]",this);
	startRtp();
    }
    return false;
}

void YateSIPConnection::reInvite(SIPTransaction* t)
{
    Debug(DebugAll,"YateSIPConnection::reInvite(%p) [%p]",t,this);
    // hack: use a while instead of if so we can return or break out of it
    while (t->initialMessage()->body && t->initialMessage()->body->isSDP()) {
	// accept re-INVITE only for local RTP, not for pass-trough
	if (m_rtpid.null())
	    break;
	String addr,port,formats;
	parseSDP(static_cast<SDPBody*>(t->initialMessage()->body),addr,port,formats);
	int q = formats.find(',');
	String frm = formats.substr(0,q);
	if (port.null() || frm.null())
	    break;
	m_rtpAddr = addr;
	m_rtpPort = port;
	m_rtpFormat = frm;
	m_formats = formats;
	Debug(DebugAll,"New RTP addr '%s' port %s formats '%s' format '%s'",
	    m_rtpAddr.c_str(),m_rtpPort.c_str(),m_formats.c_str(),m_rtpFormat.c_str());

	m_rtpid.clear();
	setSource();
	setConsumer();

	SIPMessage* m = new SIPMessage(t->initialMessage(), 200, "OK");
	SDPBody* sdp = createRtpSDP(true);
	m->setBody(sdp);
	t->setResponse(m);
	m->deref();
	return;
    }
    t->setResponse(488, "Not Acceptable Here");
}

void YateSIPConnection::doBye(SIPTransaction* t)
{
    Debug(DebugAll,"YateSIPConnection::doBye(%p) [%p]",t,this);
    t->setResponse(200,"OK");
    m_byebye = false;
    hangup();
}

void YateSIPConnection::doCancel(SIPTransaction* t)
{
    Debug(DebugAll,"YateSIPConnection::doCancel(%p) [%p]",t,this);
    if (m_tr) {
	t->setResponse(200,"OK");
	m_byebye = false;
	clearTransaction();
	disconnect("Cancelled");
    }
    else
	t->setResponse(481,"Call/Transaction Does Not Exist");
}

void YateSIPConnection::ringing(Message* msg)
{
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 180, "Ringing");
	SDPBody* sdp = msg ? createPasstroughSDP(*msg) : 0;
	m->setBody(sdp);
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("ringing");
}

void YateSIPConnection::answered(Message* msg)
{
    if (m_tr && (m_tr->getState() == SIPTransaction::Process)) {
	SIPMessage* m = new SIPMessage(m_tr->initialMessage(), 200, "OK");
	SDPBody* sdp = msg ? createPasstroughSDP(*msg) : 0;
	if (!sdp)
	    sdp = createRtpSDP();
	m->setBody(sdp);
	m_tr->setResponse(m);
	m->deref();
    }
    setStatus("answered",Established);
}

bool SipMsgThread::route()
{
    Debug(DebugAll,"Routing thread for %s [%p]",m_id.c_str(),this);
    m_msg->retValue().clear();
    bool ok = Engine::dispatch(m_msg) && !m_msg->retValue().null();
    if (m_tr->getState() != SIPTransaction::Process) {
	Debug(DebugInfo,"SIP call %s (%p) vanished while routing!",m_id.c_str(),m_tr);
	return false;
    }
    if (ok) {
	*m_msg = "call.execute";
	m_msg->setParam("callto",m_msg->retValue());
	m_msg->retValue().clear();
	YateSIPConnection* conn = new YateSIPConnection(*m_msg,m_tr);
	m_msg->userData(conn);
	if (Engine::dispatch(m_msg)) {
	    Debug(DebugInfo,"Routing SIP call %s (%p) to '%s' [%p]",
		m_id.c_str(),m_tr,m_msg->getValue("callto"),this);
	    conn->setStatus("routed");
	    conn->setTarget(m_msg->getValue("targetid"));
	    if (conn->getTarget().null()) {
		Debug(DebugInfo,"Answering now SIP call %s [%p] because we have no targetid",
		    conn->id().c_str(),conn);
		conn->answered();
	    }
	    else
		m_tr->setResponse(183, "Session Progress");
	    conn->deref();
	}
	else {
	    Debug(DebugInfo,"Rejecting unconnected SIP call %s (%p) [%p]",
		m_id.c_str(),m_tr,this);
	    m_tr->setResponse(500, "Server Internal Error");
	    conn->setStatus("rejected");
	    conn->destruct();
	}
    }
    else {
	Debug(DebugInfo,"Rejecting unrouted SIP call %s (%p) [%p]",
	    m_id.c_str(),m_tr,this);
	m_tr->setResponse(404, "Not Found");
    }
    return ok;
}

void SipMsgThread::run()
{
    s_route.lock();
    s_count++;
    s_route.unlock();
    Debug(DebugAll,"Started routing thread for %s (%p) [%p]",
	m_id.c_str(),m_tr,this);
    m_tr->ref();
    bool ok = route();
    m_tr->deref();
    s_route.lock();
    s_count--;
    if (ok)
	s_routed++;
    s_route.unlock();
}

void SipMsgThread::cleanup()
{
    Debug(DebugAll,"Cleaning up routing thread for %s (%p) [%p]",
	m_id.c_str(),m_tr,this);
    delete m_msg;
    m_tr->deref();
}

bool SIPHandler::received(Message &msg)
{
    String dest(msg.getValue("callto"));
    if (!dest.startSkip("sip/",false))
	return false;
    if (!msg.userData()) {
	Debug(DebugWarn,"SIP call found but no data channel!");
	return false;
    }
    YateSIPConnection* conn = new YateSIPConnection(msg,dest,msg.getValue("id"));
    if (conn->getTransaction()) {
	DataEndpoint *dd = static_cast<DataEndpoint *>(msg.userData());
	if (dd && conn->connect(dd)) {
	    msg.setParam("targetid",conn->id());
	    conn->deref();
	    return true;
	}
    }
    conn->destruct();
    return false;
}

bool SIPConnHandler::received(Message &msg, int id)
{
    String callid;
    switch (id) {
	case Answered:
	case Ringing:
	    callid = msg.getValue("targetid");
	    break;
	case Drop:
	case Masquerade:
	    callid = msg.getValue("id");
	    break;
	default:
	    return false;
    }
    if (!callid.startSkip("sip/",false) || callid.null()) {
	if (id == Drop) {
	    Debug("SIP",DebugInfo,"Dropping all calls");
	    s_calls.clear();
	}
	return false;
    }
    Lock lock(s_mutex);
    YateSIPConnection* conn = YateSIPConnection::find(callid);
    Debug("SIP",DebugInfo,"Connhandler lookup '%s' returned %p",
	callid.c_str(),conn);
    if (!conn)
	return false;
    switch (id) {
	case Drop:
	    lock.drop();
	    conn->disconnect();
	    break;
	case Masquerade:
	    msg.setParam("targetid",conn->getTarget());
	    msg = msg.getValue("message");
	    msg.clearParam("message");
	    msg.userData(conn);
	    return false;
	case Ringing:
	    conn->ringing(&msg);
	    break;
	case Answered:
	    conn->answered(&msg);
	    break;
	default:
	    return false;
    }
    return true;
}

SIPDriver::SIPDriver()
    : Driver("sip","varchans"), m_endpoint(0)
{
    Output("Loaded module SIP Channel");
}

SIPDriver::~SIPDriver()
{
    Output("Unloading module SIP Channel");
}

void SIPDriver::initialize()
{
    Output("Initializing module SIP Channel");
    s_cfg = Engine::configFile("ysipchan");
    s_cfg.load();
    if (!m_endpoint) {
	m_endpoint = new YateSIPEndPoint();
	if (!(m_endpoint->Init())) {
	    delete m_endpoint;
	    m_endpoint = 0;
	    return;
	}
	m_endpoint->startup();
	setup();
	installRelay(Halt);
    }
}

/* vi: set ts=8 sw=4 sts=4 noet: */
