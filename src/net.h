#if WIN
	#define EAGAIN WSAEWOULDBLOCK
	#define EISCONN WSAEISCONN
	#define EINPROGRESS WSAEALREADY
	#define EALREADY WSAEINVAL
	#define EPIPE 0
	#undef errno
	#define errno WSAGetLastError()
	#define ioctl ioctlsocket
	#define socklen_t int
	#define gai_strerrorF gai_strerrorA
#else
	#include <sys/socket.h>
	#include <sys/ioctl.h>
	#include <sys/un.h>
	#include <arpa/inet.h>
	#include <netinet/tcp.h>
	#include <netdb.h>
	#include <signal.h>
	#define closesocket close
	#define SOCKET int
	#define gai_strerrorF gai_strerror
#endif

#include <setjmp.h>

#define NETEPOLL LINUX && SERVER
#if NETEPOLL
	#include <sys/epoll.h>
	#define BACKLOG 1000
#else
	#define BACKLOG 100
#endif
#ifndef MSG_NOSIGNAL
	#define MSG_NOSIGNAL 0
#endif

#define SUPPORT_IPV6
#define NETIDLE 0
#define NETRECV 2 // receive full or +1 separator
#define NETRECV1ST 4
#define NETCONN 13
#define NETLISN 14
#define NETSHARE 15
#define NETCLOSE 16
#define NETBUFLEN (1024*1024)

// meta = { __metatable='net', __index=methods }
// nets = { net=true, 0=epoll, 1=lasttime, 2=buf }.meta{ __mode='k' }
#pragma pack(push)
#pragma pack(4)
typedef struct
{
	short	type;
	short	mode;
	SOCKET	sock;
	int		len;
	int		time; // seconds
	uintptr	onClose; // head
	uintptr onLisnConn; // head
	uintptr onReceive; // head
	uintptr seps[4]; // head
} Net;
#pragma pack(pop)

// +1 net
static Net *netNew(lua_State *lua, int nets, short mode, SOCKET sock)
{
	int L = gettop(lua)+1;
	Net *net = (Net*)lua_createbody(lua, 4, 4, sizeof(Net), 7); // L net
	getmetai(lua, global, M_netmeta), setmeta(lua, L); // net.meta=meta
	net->type = M_nets, net->mode = mode, net->sock = sock, net->len = 0;
#if NETEPOLL
	struct epoll_event e = { mode==NETCONN ? EPOLLOUT
		: mode==NETLISN || mode==NETSHARE ? EPOLLIN : 0, net };
	epoll_ctl((rawgeti(lua, nets, 0), popint(lua)), EPOLL_CTL_ADD, sock, &e);
#else
	int n = -2;
	for (pushz(lua); lua_next(lua, nets); pop(lua, 1))
		++n;
	if (n > BACKLOG)
		logErr("ERROR too many net peers");
#endif
	pushb(lua, true), rawsetk(lua, nets, L); // nets[net]=true
	return net;
}

static void netClose(lua_State *lua, int nets, Net *net, const char *cause)
{
	int mode = net->mode;
	if (mode == NETCLOSE)
		return;
	int err = errno;
#if NETEPOLL
	epoll_ctl((rawgeti(lua, nets, 0), popint(lua)), EPOLL_CTL_DEL, net->sock, NULL); // must del shared socket
#endif
	net->mode = NETCLOSE, closesocket(net->sock), net->sock = 0;
	lua_reftable(lua, net), pushz(lua), rawset(lua, nets); // nets[net]=nil
	if (mode <= NETCONN)
	{
		int L = gettop(lua)+1;
		getmetai(lua, global, M_onerror); // L onerror
		lua_refhead(lua, (const void*) net->onClose), lua_reftable(lua, net); // L+1 onClose L+2 net
		pushb(lua, cause && !cause[0]), pushb(lua, mode >= NETCONN); // L+3 timeout L+4 notconn
		pushs(lua, cause ? cause[0] ? cause : "timeout" : strerror(err)); // L+5 err
		if (pcall(lua, 4, 0, L))
			logErr(isstr(lua, -1) ? tostr(lua, -1) : tonamex(lua, -1));
		settop(lua, L-1);
	}
}

static int netFree(lua_State *lua, const void *nets, long long net, long long v, unsigned char closing)
{
	Net *n = (Net*)lua_headtobody((void*)((uintptr)net & 0x0000ffffffffffffL), NULL, NULL);
	if (closing)
		closesocket(n->sock);
	else
	{
		int L = gettop(lua)+1;
		lua_refhead(lua, nets); // L nets
		netClose(lua, L, n, "free");
		settop(lua, L-1);
	}
	return true;
}

#if MAC

static struct sockaddr_in6 checkAddr(lua_State *lua, char *addr, bool listen)
{
	char *addrPort = strrchr(addr, ':');
	if ( !addrPort)
		error(lua, "invalid port %s", addr);

	unsigned port = (unsigned) strtol(addrPort+1, NULL, 10);
	if (port == 0 || port >> 16)
		error(lua, "invalid port %s", addr);

	*addrPort = 0;

	struct addrinfo *info;
	int ret = getaddrinfo(addr, addrPort+1, NULL, &info);
	*addrPort = ':';
	if ( ret != 0 )
		error(lua, "getaddrinfo: %s", gai_strerror(ret));

	struct sockaddr_in6 ad;
	memset(&ad, 0, sizeof(ad));
	if ( info->ai_addr->sa_family == AF_INET )
	{
		struct sockaddr_in *adin = (struct sockaddr_in *) &ad;
		struct sockaddr_in *t = (struct sockaddr_in *) info->ai_addr;
		#if !WIN && !ANDROID
		adin->sin_len = t->sin_len;
		#endif
		adin->sin_family = t->sin_family;
		adin->sin_port = htons(port);
		adin->sin_addr = t->sin_addr;
	}
	else if ( info->ai_addr->sa_family == AF_INET6 )
	{
		#ifdef SUPPORT_IPV6

		struct sockaddr_in6 *adin = (struct sockaddr_in6 *) &ad;
		struct sockaddr_in6 *t = (struct sockaddr_in6 *) info->ai_addr;
		#if !WIN && !ANDROID
		adin->sin6_len = t->sin6_len;
		#endif
		adin->sin6_family = t->sin6_family;
		adin->sin6_port = htons(port);
		adin->sin6_addr = t->sin6_addr;
		adin->sin6_flowinfo = t->sin6_flowinfo;
		adin->sin6_scope_id = t->sin6_scope_id;

		#endif
	}

	freeaddrinfo(info);

	return ad;
}

#else

static struct sockaddr_in6 checkAddr(lua_State *lua, char *addr, bool listen)
{
	char *addrPort = strchr(addr, ':');
	if ( !addrPort)
		error(lua, "invalid port %s", addr);
	*addrPort = 0;
	unsigned host = *addr ? ntohl(inet_addr(addr)) : 0, port = (unsigned) strtol(addrPort+1, NULL, 10);
	struct hostent *dns = NULL;
	if (host == (unsigned)-1) // not ip
	{
		if ( !strcmp(addr, "localhost"))
			host = 0x7f000001;
		else if ( !strcmp(addr, "*"))
			host = 0;
		else
			dns = gethostbyname(addr); // resolve
	}
	*addrPort = ':';
	if (dns)
		host = ntohl(*(int*)dns->h_addr_list[0]);
	else if (host == (unsigned)-1)
		error(lua, "invalid host %s : %s", addr, hstrerror(h_errno));
#if WIN
	if (host == 0)
	{
		char local[101]; gethostname(local, 100), local[100] = 0;
		struct hostent *h = gethostbyname(local);
		if ( !h)
			error(lua, "invalid host %s : %s", addr, hstrerror(h_errno));
		host = ntohl(*(int*)h->h_addr_list[0]);
	}
//	if (listen &&
//		(host>>24 ^ host>>25) != (0x7F000000>>24 ^ 0x7F000000>>25) &&
//		(host>>24 ^ host>>25) != (0x0A000000>>24 ^ 0x0A000000>>25) &&
//		(host>>20 ^ host>>21) != (0xAC100000>>20 ^ 0xAC100000>>21) &&
//		(host>>16 ^ host>>17) != (0xC0A80000>>16 ^ 0xC0A80000>>17) &&
//		(host>>12 ^ host>>13) != (0x01010000>>12 ^ 0x01010000>>13))
//		error(lua, "invalid host %s", addr);
#endif
	if (port == 0 || port >> 16)
		error(lua, "invalid port %s", addr);
	struct sockaddr_in6 ad6;
	memset(&ad6, 0, sizeof(ad6));
	struct sockaddr_in *ad = (struct sockaddr_in*)&ad6;
	ad->sin_family = AF_INET, ad->sin_addr.s_addr = htonl(host), ad->sin_port = htons(port);
	return ad6;
}

#endif

///////////////////////////////// io /////////////////////////////

// -3* net,sep...,len,...,timeout +0 *1 nets
static Net *net_recv(lua_State *lua, int lenx, int timex)
{
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1)), (Net*)NULL;
	if (net->mode >= NETCONN)
		return error(lua, net->mode == NETCLOSE ? "net closed" : "net unconnected"),(Net*) NULL;
	if (lenx - 2 > 4)
		return error(lua, "too many separators"), (Net*)NULL;
	int len = mustint(lua, lenx);
	if (len <= 0 || len > NETBUFLEN)
		return netClose(lua, upx(1), net, "invalid length"),
			error(lua, "bad argument %d (length %d out of range)", lenx, len), (Net*)NULL;
	// save separators
	memset(net->seps, 0, sizeof(net->seps));
	for (int i = 2; i < lenx; i++)
		if ( !isstr(lua, i) && totype(lua, i) != LUA_TUSERDATA)
			return error(lua, "bad argument #%d (bytes expected, got %s)", i,
				luaL_typename(lua, i)), (Net*)NULL;
		else if ( !tolen(lua, i))
			return error(lua, "bad argument #%d (bytes expected, got empty)", i), (Net*)NULL;
		else
			lua_markbody(lua, net), net->seps[i-2] = (uintptr) tohead(lua, i);
	net->len = len;
	net->time = (int)timeNow(1, true) + range(roundint(lua, timex), 1, 86400);
#if !WIN
	if (len > 4096)
	{
		int b, z = sizeof(b);
		getsockopt(net->sock, SOL_SOCKET, SO_RCVBUF, (char*)&b, (socklen_t*)&z);
#if LINUX
		b = b+1>>1;
#endif
		if (b < len+1024)
			b = len+1024,
			setsockopt(net->sock, SOL_SOCKET, SO_RCVBUF, (char*)&b, sizeof(b));
	}
#endif
	return net;
}

// -4* net,len,...,onReceive(net,data),timeout +0 *1 nets
static int net_receive(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	checkArg(lua, -3, "-3", LUA_TNUMBER);
	checkArg(lua, -2, "-2", LUA_TFUNCTION);
	checkArg(lua, -1, "-1", LUA_TNUMBER);
	int L = gettop(lua)+1;
	Net *net = net_recv(lua, L-3, L-1);
	net->mode = 2 < L-3 ? NETRECV1ST+1 : NETRECV1ST;
	lua_markbody(lua, net), net->onReceive = (uintptr) tohead(lua, L-2);
#if NETEPOLL
//	setsockopt(net->sock, SOL_SOCKET, SO_RCVLOWAT, &len, sizeof(len)); // consider out of band data ?
	struct epoll_event e = { EPOLLIN|EPOLLRDHUP, net };
	epoll_ctl((rawgeti(lua, upx(1), 0), popint(lua)), EPOLL_CTL_MOD, net->sock, &e);
#endif
	return 0;
}

static int net_recvLen(lua_State *lua, int nets, Net *net, char *buf, bool nocall, int epoll)
{
	int n;
#if !NETPOLL
	n = (int)recv(net->sock, (char*)&n, 1, MSG_PEEK);
	if (n==0)
		return netClose(lua, nets, net, "peer reset"), -1;
	if (n < 0 && errno != EAGAIN)
		return netClose(lua, nets, net, NULL), -1;
#endif
	if (ioctl(net->sock, FIONREAD, (unsigned long*)&n))
		return netClose(lua, nets, net, NULL), -1; // may not detect peer reset
	if (n < net->len)
	{
		if (net->mode == NETRECV1ST)
		{
			net->mode = NETRECV;
#if NETEPOLL
			struct epoll_event e = { EPOLLIN|EPOLLRDHUP|EPOLLET, net };
			epoll_ctl(epoll, EPOLL_CTL_MOD, net->sock, &e);
#endif
		}
		return -1;
	}
	int L = gettop(lua)+1;
	getmetai(lua, global, M_onerror); // L onerror
	lua_refhead(lua, (const void*) net->onReceive), lua_reftable(lua, net); // L+1 onReceive L+2 net
	lua_refudata(lua, buf); // L+3 bytes
	lua_userdatalen(buf, net->len);
	recv(net->sock, buf, net->len, 0);
	net->time = 0x7FFFffff;
	if ( !nocall)
		if (net->mode = NETIDLE, pcall(lua, 2, 0, L))
			logErr(isstr(lua, -1) ? tostr(lua, -1) : tonamex(lua, -1));
#if NETEPOLL
	if (net->mode == NETIDLE)
	{
		struct epoll_event e = { 0, net };
		epoll_ctl(epoll, EPOLL_CTL_MOD, net->sock, &e);
	}
#endif
	return nocall ? 1 : (settop(lua, L-1), 0);
}

static int net_recvSep(lua_State *lua, int nets, Net *net, char *buf, bool nocall, int epoll)
{
	int n = (int)recv(net->sock, buf, net->len, MSG_PEEK);
	if (n == 0)
		return netClose(lua, nets, net, "peer reset"), -1;
	if (n < 0 && errno != EAGAIN)
		return netClose(lua, nets, net, NULL), -1;

	int L = gettop(lua)+1;
	if (n > 0)
	{
		getmetai(lua, global, M_onerror); // L onerror
		lua_refhead(lua, (const void*) net->onReceive), lua_reftable(lua, net); // L+1 onReceive L+2 net
		lua_refudata(lua, buf); // L+3 bytes
		for (int i = 0; i < 4; i++)
		{
			size_t sn; const char *sep = (char*)lua_headtobody((const void*) net->seps[i], NULL, &sn);
			if (sep && (sep = (char*)memfind(buf, n, sep, sn)))
			{
				int n = (int)(sep - buf);
				lua_refhead(lua, (const void*) net->seps[i]); // L+4 sep
				lua_userdatalen(buf, n);
				recv(net->sock, buf, n+(int)sn, 0);
				net->time = 0x7FFFffff;
				if ( !nocall)
					if (net->mode = NETIDLE, pcall(lua, 3, 0, L))
						logErr(isstr(lua, -1) ? tostr(lua, -1) : tonamex(lua, -1));
				goto end;
			}
		}
		if (n == net->len)
		{
			pushz(lua); // L+4 nil
			lua_userdatalen(buf, n);
			recv(net->sock, buf, n, 0);
			net->time = 0x7FFFffff;
			if ( !nocall)
				if (net->mode = NETIDLE, pcall(lua, 3, 0, L))
					logErr(isstr(lua, -1) ? tostr(lua, -1) : tonamex(lua, -1));

			goto end;
		}
	}

	if (net->mode == NETRECV1ST+1)
	{
		net->mode = NETRECV+1;
#if NETEPOLL
		struct epoll_event e = { EPOLLIN|EPOLLRDHUP|EPOLLET, net };
		epoll_ctl(epoll, EPOLL_CTL_MOD, net->sock, &e);
#endif
	}

	return -1;

	end:
#if NETEPOLL
	if (net->mode == NETIDLE)
	{
		struct epoll_event e = { 0, net };
		epoll_ctl(epoll, EPOLL_CTL_MOD, net->sock, &e);
	}
#endif
	return nocall ? 2 : (settop(lua, L-1), 0);
}

// -3* net,sep...,len,timeout +* *1 nets
static int net_receiving(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	checkArg(lua, -2, "-2", LUA_TNUMBER);
	checkArg(lua, -1, "-1", LUA_TNUMBER);
	int L = gettop(lua)+1;
	Net *net = net_recv(lua, L-2, L-1);
	char *buf = (rawgeti(lua, upx(1), 2), (char*)popudata(lua));
#if NETEPOLL
	int epoll = (rawgeti(lua, upx(1), 0), popint(lua));
#else
	int epoll = 0;
#endif
	for (int n;;)
	{
		n = 2 < L-2 ? net_recvSep(lua, upx(1), net, buf, true, epoll)
			: net_recvLen(lua, upx(1), net, buf, true, epoll);
		if (n >= 0)
			return n;
		if ((int)timeNow(1, true) > net->time)
			netClose(lua, upx(1), net, "");
		if (net->mode == NETCLOSE)
			error(lua, "net closed");
	}
}

// -2|5 net,bytes,begin,end,sent +0|1 -1closed|sent *1 nets
static int net_send(lua_State *lua)
{
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1));
	if (net->mode >= NETCONN)
		return net->mode == NETCLOSE ? pushi(lua, -1), 1 : error(lua, "net unconnected");
	size_t n, m = 0, b, i; socklen_t z = sizeof(b);
	const char *s = tobytes(lua, 2, &n, "#2");
	const char *S = s + indexn(luaL_optint(lua, 4, -1), n);
	s += indexn0(touint(lua, 3), n);
	if (s > S)
		return error(lua, "invalid length");
	for (i = 1; ; i++)
	{
		n = send(net->sock, s+m, (int)(S-s-m), MSG_NOSIGNAL);
		if ((int)n == -1 && errno != EAGAIN && errno != EPIPE)
			return netClose(lua, upx(1), net, NULL), pushi(lua, -1), 1;
		(int)n == -1 && (n = 0), m += n;
		if (i==1 && tobool(lua, 5))
			return pushi(lua, m), 1; // sent bytes
		if (m >= (unsigned int) (S-s) )
			return 0;
		if (i==4)
			return netClose(lua, upx(1), net, "buffer overflow"), pushi(lua, m), 1;
		b = 0, getsockopt(net->sock, SOL_SOCKET, SO_SNDBUF, (char*)&b, &z);
#if LINUX
		b = b+1>>1;
#endif
		b += S-s-m + i*16384;
		setsockopt(net->sock, SOL_SOCKET, SO_SNDBUF, (char*)&b, sizeof(b));
	}
}

// -3|5 net,name,bytes,begin,end +0 *1 nets
static int net_share(lua_State *lua)
{
#if LINUX
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1));
	if (net->mode >= NETCONN)
		return error(lua, "net unconnected");
	size_t n; const char *name = tobytes(lua, 2, &n, "#2");
	if (n < 1 || n > 100)
		return error(lua, "invalid name");
	struct sockaddr_un ad;
	ad.sun_family = AF_UNIX, ad.sun_path[0] = 0, memcpy(ad.sun_path+1, name, n);
	memset(ad.sun_path+n+1, 0, sizeof(ad.sun_path)-n-1);

	const char *s = tobytes(lua, 3, &n, "#3");
	const char *S = s + indexn(luaL_optint(lua, 5, -1), n);
	s += indexn0(touint(lua, 4), n);
	if (s >= S)
		return error(lua, "invalid length");
	struct iovec e = { (void*)s, (unsigned int)( S-s ) };

	char cmsg[CMSG_SPACE(4)];
	struct msghdr m = { (void*)&ad, sizeof(ad), &e, 1, cmsg, CMSG_LEN(4), 0 };
	struct cmsghdr *c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET, c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = m.msg_controllen, *(int*)CMSG_DATA(c) = net->sock;
	getmetai(lua, global, M_netlaunch);
	if (sendmsg(popint(lua), &m, 0) <= 0)
		return pushs(lua, strerror(errno)), 1;
	return 0;
#else
	return error(lua, "unsupported");
#endif
}

// -2 net bool +0 *1 nets
static int net_nagle(lua_State *lua)
{
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1));
	if (net->mode >= NETCONN)
		return error(lua, net->mode == NETCLOSE ? "net closed" : "net unconnected");
	int on = !tobool(lua, 2);
	setsockopt(net->sock, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
	return 0;
}

// -1 net +0 *1 nets
static int net_close(lua_State *lua)
{
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1));
	netClose(lua, upx(1), net, "close");
	return 0;
}

// -1 net +1 isclosed *1 nets
static int net_closed(lua_State *lua)
{
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1));
	return pushb(lua, net->mode >= NETCLOSE), 1;
}

static int net_state(lua_State *lua)
{
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1));
	return pushi(lua, net->mode), 1;
}

static int net_connecting(lua_State *lua)
{
	Net *net = (Net*)totab(lua, 1);
	if ( !net || net->type != M_nets)
		return errArgT(lua, "#1", "net", totype(lua, 1));
	return pushb(lua, net->mode == NETCONN), 1;
}

///////////////////////////////// connect /////////////////////////////

// -4 addr,onConnect(net,ip,port,myip,myport),onClose(net,timeout,unconn),timeout
// +1 net *1 nets
static int Connect(lua_State *lua)
{
	checkArg(lua, 2, "#2", LUA_TFUNCTION);
	checkArg(lua, 3, "#3", LUA_TFUNCTION);
	checkArg(lua, 4, "#4", LUA_TNUMBER);
	struct sockaddr_in6 ad = checkAddr(lua, (char*)tobytes(lua, 1, NULL, "#1"), false);
	SOCKET sock = socket(ad.sin6_family, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1)
		return error(lua, "connect init error: %s", strerror(errno));
	int on = 1; ioctl(sock, FIONBIO, (unsigned long*)&on);
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
#if MAC
	setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&on, sizeof(on));
#elif WIN
	on = NETBUFLEN, setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&on, sizeof(on));
#endif

#if 0
#if !SERVER
#if WIN
	on = NETBUFLEN;
#else
	on = NETBUFLEN>>2;
#endif
	setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&on, sizeof(on));
	setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&on, sizeof(on));
	int On = 0; socklen_t n = sizeof(On);
	if (getsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&On, &n) || On != on)
		On ? lua_pushfstring(lua, "WARN sock recv buf %d", On) :
			lua_pushfstring(lua, "WARN sock recv buf %s", strerror(errno)), logErr(pops(lua));
	On = 0;
	if (getsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&On, &n) || On != on)
		On ? lua_pushfstring(lua, "WARN sock send buf %d", On) :
			lua_pushfstring(lua, "WARN sock send buf %s", strerror(errno)), logErr(pops(lua));
#endif
#endif

	if (connect(sock, (struct sockaddr*)&ad, ad.sin6_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6))
		&& errno != EINPROGRESS && errno != EAGAIN)
		return closesocket(sock), error(lua, "connect error: %s", strerror(errno));

	Net *net = netNew(lua, upx(1), NETCONN, sock); // net
	net->time = (int)timeNow(1, true) + range(roundint(lua, 4), 1, 86400);
	net->onLisnConn = (uintptr) tohead(lua, 2), net->onClose = (uintptr) tohead(lua, 3);
	return 1;
}

static void Connect_check(lua_State *lua, int nets, Net *net, int epoll) // keep lua top unchanged
{
	struct sockaddr_in6 ad; int adz = sizeof(struct sockaddr_in6);
#if NETEPOLL
	int err = 0; socklen_t errz = sizeof(err);
	getsockopt(net->sock, SOL_SOCKET, SO_ERROR, &err, &errz);
	if (err)
		return netClose(lua, nets, net, strerror(err));
	struct epoll_event e = { 0, net };
	epoll_ctl(epoll, EPOLL_CTL_MOD, net->sock, &e);
#elif ANDROID
	fd_set ws;
	FD_ZERO(&ws); FD_SET(net->sock, &ws);
	struct timeval t = { 0, 0 };
	if (select(net->sock + 1, NULL, &ws, NULL, &t) <= 0)
		return;
	// can not detect error on some androids
#else
	memset(&ad, 0, sizeof(ad));
	connect(net->sock, (struct sockaddr*)&ad, sizeof(struct sockaddr_in6));
	if (errno == EINPROGRESS || errno == EALREADY || errno == EAGAIN)
		return;
	if (errno != EISCONN)
		return netClose(lua, nets, net, NULL);
#endif
	net->mode = NETIDLE;
	net->time = 0x7FFFffff;
	int L = gettop(lua)+1;
	getmetai(lua, global, M_onerror); // L onerror
	lua_refhead(lua, (const void*) net->onLisnConn), lua_reftable(lua, net); // L+1 onConnect L+2 net

	getpeername(net->sock, (struct sockaddr*)&ad, (socklen_t*)&adz);
	if ( ad.sin6_family == AF_INET6 )
	{
		#ifdef SUPPORT_IPV6

		char str[INET6_ADDRSTRLEN] = {0};
#if WIN
		DWORD len = INET6_ADDRSTRLEN;
		::WSAAddressToStringA((LPSOCKADDR)&ad.sin6_addr, sizeof(ad.sin6_addr), NULL, str, &len);
#else
		inet_ntop(AF_INET6, &ad.sin6_addr, str, INET6_ADDRSTRLEN);
#endif
		pushs(lua, str), pushi(lua, ntohs(ad.sin6_port)); // L+3 ip L+4 port

		#endif
	}
	else
	{
		pushn(lua, ntohl(((struct sockaddr_in*)&ad)->sin_addr.s_addr)), pushi(lua, ntohs(((struct sockaddr_in*)&ad)->sin_port)); // L+3 ip L+4 port
	}

	getsockname(net->sock, (struct sockaddr*)&ad, (socklen_t*)&adz);
	if ( ad.sin6_family == AF_INET6 )
	{
		#ifdef SUPPORT_IPV6

		char str[INET6_ADDRSTRLEN] = {0};
#if WIN
		DWORD len = INET6_ADDRSTRLEN;
		::WSAAddressToStringA((LPSOCKADDR)&ad.sin6_addr, sizeof(ad.sin6_addr), NULL, str, &len);
#else
		inet_ntop(AF_INET6, &ad.sin6_addr, str, INET6_ADDRSTRLEN);
#endif
		pushs(lua, str), pushi(lua, ntohs(ad.sin6_port));// L+5 myip L+6 myport

		#endif
	}
	else
	{
		pushn(lua, ntohl(((struct sockaddr_in*)&ad)->sin_addr.s_addr)), pushi(lua, ntohs(((struct sockaddr_in*)&ad)->sin_port)); // L+5 myip L+6 myport
	}

	if (pcall(lua, 5, 0, L))
		logErr(isstr(lua, -1) ? tostr(lua, -1) : tonamex(lua, -1));
	settop(lua, L-1);
}

///////////////////////////////// listen /////////////////////////////

// -3 addr,onListen(net,lisn,ip,port,myip,myport),onClose(net) +1 lisn *1 nets
static int Listen(lua_State *lua)
{
	checkArg(lua, 2, "#2", LUA_TFUNCTION);
	checkArg(lua, 3, "#3", LUA_TFUNCTION);
	size_t n; const char *addr = tobytes(lua, 1, &n, "#1");
	Net *lisn;
	if (addr[0] == '@' && n > 1)
	{
#if LINUX
		if (n > 100)
			return error(lua, "address too long");
		int sock = socket(AF_UNIX, SOCK_DGRAM, 0);
		if (sock == -1)
			return error(lua, "listen init error: %s", strerror(errno));
		int on = 1; ioctl(sock, FIONBIO, (unsigned long*)&on);
		struct sockaddr_un ad;
		ad.sun_family = AF_UNIX, memcpy(ad.sun_path, addr, n), ad.sun_path[0] = 0;
		memset(ad.sun_path+n, 0, sizeof(ad.sun_path)-n);
		if (bind(sock, (struct sockaddr*)&ad, sizeof(ad)))
			return error(lua, "listen bind error: %s", strerror(errno));
		lisn = netNew(lua, upx(1), NETSHARE, sock); // lisn
#else
		return error(lua, "unsupported");
#endif
	}
	else
	{
		struct sockaddr_in6 ad = checkAddr(lua, (char*)addr, true);
		SOCKET sock = socket(ad.sin6_family, SOCK_STREAM, IPPROTO_TCP);
		if (sock == -1)
			return error(lua, "listen init error: %s", strerror(errno));
		int on = 1; ioctl(sock, FIONBIO, (unsigned long*)&on);
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
#if LINUX && SERVER
		setreuid(geteuid(), 0);
#endif
		int err = bind(sock, (struct sockaddr*)&ad, sizeof(ad));
#if LINUX && SERVER
		setreuid(0, getuid());
#endif
		if (err)
			return error(lua, "listen bind %s error: %s", addr, strerror(errno));
		if (listen(sock, BACKLOG))
			return error(lua, "listen error: %s", strerror(errno));
		lisn = netNew(lua, upx(1), NETLISN, sock); // lisn
	}
	lisn->time = 0x7FFFffff;
	lisn->onLisnConn = (uintptr) tohead(lua, 2), lisn->onClose = (uintptr) tohead(lua, 3);
	return 1;
}

static void Listen_check(lua_State *lua, int nets, Net *lisn, char *buf) // keep lua top unchanged
{
	struct sockaddr_in ad; int adz = sizeof(ad);
	SOCKET sock = 0; bool extra = false;
#if LINUX
	if (lisn->mode == NETSHARE)
	{
		struct iovec e = { buf, NETBUFLEN };
		char cmsg[CMSG_SPACE(4)];
		struct msghdr m = { NULL, 0, &e, 1, cmsg, CMSG_LEN(4), 0 };
		int n = recvmsg(lisn->sock, &m, 0);
		if (n <= 0)
			return;
		struct cmsghdr *c = CMSG_FIRSTHDR(&m);
		if (!c || c->cmsg_type != SCM_RIGHTS)
			return;
		sock = *(int*)CMSG_DATA(c), extra = true;
		lua_userdatalen(buf, n);
		getpeername(sock, (struct sockaddr*)&ad, (socklen_t*)&adz);
	}
	else
#endif
	{
		sock = accept(lisn->sock, (struct sockaddr*)&ad, (socklen_t*)&adz);
		if (sock == -1)
			return;
	}
	int on = 1; ioctl(sock, FIONBIO, (unsigned long*)&on);
#if MAC
	setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&on, sizeof(on));
#elif WIN
	on = NETBUFLEN, setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&on, sizeof(on));
#endif
	int L = gettop(lua)+1;
	getmetai(lua, global, M_onerror); // L onerror
	lua_refhead(lua, (const void*) lisn->onLisnConn); // L+1 onListen
	Net *net = netNew(lua, nets, NETIDLE, sock); // L+2 net
	net->time = 0x7FFFffff, net->onClose = lisn->onClose;
	lua_reftable(lua, lisn); // L+3 lisn
	pushn(lua, ntohl(ad.sin_addr.s_addr)), pushi(lua, ntohs(ad.sin_port)); // L+4 ip L+5 port
	getsockname(sock, (struct sockaddr*)&ad, (socklen_t*)&adz);
	pushn(lua, ntohl(ad.sin_addr.s_addr)), pushi(lua, ntohs(ad.sin_port)); // L+6 myip L+7 myport
	extra ? lua_refudata(lua, buf) : pushz(lua); // L+8 extra
	if (pcall(lua, 7, 0, L))
		logErr(isstr(lua, -1) ? tostr(lua, -1) : tonamex(lua, -1));
	settop(lua, L-1);
}

////////////////////////////////// main //////////////////////////////////

static void net_loop(lua_State *lua, bool timeout)
{
	int L = gettop(lua)+1;
	getmetai(lua, global, M_nets); // L nets
	char *buf = (rawgeti(lua, L, 2), (char*)popudata(lua));
	Net *net = NULL;
#if NETEPOLL
	int epoll = (rawgeti(lua, L, 0), popint(lua));
	struct epoll_event es[2000];
	for (int i = epoll_wait(epoll, es, 2000, 1); --i >= 0; )
	{
		bool closable = true;
		if (net = (Net*)es[i].data.ptr, es[i].events & (EPOLLIN|EPOLLOUT))
			switch (net->mode)
#else
	int epoll = 0; bool closable = true;
	for (pushz(lua); (!net || (rawgetk(lua, L, -1), !popz(lua))) && lua_next(lua, L); )
		switch (pop(lua, 1), net = (Net*)totab(lua, -1), net ? net->mode : 0)
#endif
		{
		case NETRECV: case NETRECV1ST:
			closable = net_recvLen(lua, L, net, buf, false, epoll) < 0
				|| net->mode == NETIDLE; break;
		case NETRECV+1: case NETRECV1ST+1:
			closable = net_recvSep(lua, L, net, buf, false, epoll) < 0
				|| net->mode == NETIDLE; break;
		case NETCONN: Connect_check(lua, L, net, epoll); break;
		case NETLISN: case NETSHARE: Listen_check(lua, L, net, buf); break;
		}
#if NETEPOLL
		int err = 0; socklen_t errz = sizeof(err);
		if (closable && (es[i].events & (EPOLLERR|EPOLLHUP|EPOLLRDHUP)))
			netClose(lua, L, net, es[i].events & EPOLLRDHUP ? "peer reset" :
				(getsockopt(net->sock, SOL_SOCKET, SO_ERROR, &err, &errz), strerror(err)));
	}
#endif
	int time = (int)timeNow(1, true);
	if (rawgeti(lua, L, 1), timeout && time > popint(lua))
	{
		pushi(lua, time), rawseti(lua, L, 1);
		for (bool n = (pushz(lua), lua_next(lua, L)); n; )
		{
			pop(lua, 1), net = (Net*)totab(lua, -1), n = lua_next(lua, L);
			if (net && time > net->time)
			{
				netClose(lua, L, net, "");
				if (n && (rawgetk(lua, L, -2), popz(lua)))
					pop(lua, 2), n = false;
			}
		}
	}
	settop(lua, L-1);
}

#if WIN
/*
static jmp_buf jmpbuf;
static int jmptimer = -1;
static void CALLBACK timeout_func(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
	logErr("timeout");
	longjmp(jmpbuf, 1);
}
*/
static int myhostips( const char* addr, struct addrinfo **info, int timeout)
{
/*
	logErr("myhostips");
	if (timeout != 0)
	{
		if (setjmp(jmpbuf) != 0)
		{
			::KillTimer(NULL, jmptimer);
			logErr("timeout back");
			return 0;
		}

		jmptimer = ::SetTimer(NULL, 0, timeout*100, timeout_func);
	}
*/
	int ret = getaddrinfo(addr, NULL, NULL, info);
/*	if (timeout != 0)
	{
		logErr("no timeout");
		::KillTimer(NULL, jmptimer);
		jmptimer = -1;
	}
*/
	return ret;
}

#else

static sigjmp_buf jmpbuf;
static void timeout_func(int sig)
{
	siglongjmp(jmpbuf, 1);
}
static int myhostips( const char* addr, struct addrinfo **info, int timeout)
{
	if ( timeout != 0 )
	{
		signal(SIGALRM, timeout_func);
		if (sigsetjmp(jmpbuf, 1) != 0)
		{
			alarm(0);
			signal(SIGALRM, SIG_IGN);
			return 0;
		}

		alarm(timeout);
	}

	int ret = getaddrinfo(addr, NULL, NULL, info);
	signal(SIGALRM, SIG_IGN);
	return ret;
}

#endif

// -1 addr +* ipnumber...
static int hostips(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TSTRING);
	const char *addr = tostr(lua, 1);
	int timeout = toint(lua, 2);
	if ( !strcmp(addr, "localhost"))
		return pushn(lua, 0x7f000001), 1;

	char str[INET6_ADDRSTRLEN] = {0};
	unsigned ip;

	if ( !strcmp(addr, "*"))
	{
#if WIN || ANDROID
		char local[101]; gethostname(local, 100), local[100] = 0;
		addr = local;
#else
		settop(lua, 0);
		struct ifaddrs *as = NULL, *a;
		getifaddrs(&as);
		for (a = as; a; a = a->ifa_next)
		{
			if ( a->ifa_addr->sa_family == AF_INET )
			{
				if ((ip = ntohl(((struct sockaddr_in*)a->ifa_addr)->sin_addr.s_addr)) >> 24 != 127)
					pushn(lua, ip);
			}
			else if ( a->ifa_addr->sa_family == AF_INET6 )
			{
				#ifdef SUPPORT_IPV6

				inet_ntop(AF_INET6, &((struct sockaddr_in6*)a->ifa_addr)->sin6_addr, str, INET6_ADDRSTRLEN);
				pushs(lua, str);

				#endif
			}
		}

		freeifaddrs(as);

		return gettop(lua);
#endif
	}

	struct addrinfo *info = NULL;
	int ret = myhostips(addr, &info, timeout);
	if ( ret != 0 )
		error(lua, "hostips: getaddrinfo %s", gai_strerrorF(ret));

	if (info == NULL)
		error(lua, "hostips: getaddrinfo timeout");

	settop(lua, 0);
	for (struct addrinfo* i = info; i; i = i->ai_next)
	{
		if ( i->ai_addr->sa_family == AF_INET )
		{
			if ((ip = ntohl(((struct sockaddr_in*)i->ai_addr)->sin_addr.s_addr)) >> 24 != 127)
				pushn(lua, ip);
		}
		else if ( i->ai_addr->sa_family == AF_INET6 )
		{
			#ifdef SUPPORT_IPV6

#if WIN
			DWORD len = INET6_ADDRSTRLEN;
			::WSAAddressToStringA((LPSOCKADDR)&((struct sockaddr_in6*)i->ai_addr)->sin6_addr, sizeof(((struct sockaddr_in6*)i->ai_addr)->sin6_addr), NULL, str, &len);
#else
			inet_ntop(AF_INET6, &((struct sockaddr_in6*)i->ai_addr)->sin6_addr, str, INET6_ADDRSTRLEN);
#endif
			pushs(lua, str);

			#endif
		}
	}

	freeaddrinfo(info);

	return gettop(lua);
}

static void network(lua_State *lua, int M, int G)
{
	int L = gettop(lua)+1;
	newtablen(lua, 4, 0), newmetaweak(lua, 0, 0, 100, "k"), setmeta(lua, L); // L nets
	lua_setweaking(lua, L, netFree), rawsetiv(lua, M, M_nets, L); // M_nets=nets
	pushi(lua, (int)timeNow(1, true)), rawseti(lua, L, 1); // nets[1]=utc
	newbdata(lua, NETBUFLEN), rawseti(lua, L, 2); // nets[2]=buf
#if NETEPOLL
	pushi(lua, epoll_create(5000)), rawseti(lua, L, 0); // nets[0]=epoll
#elif WIN
	WSAData wsa; WSAStartup(2, &wsa);
#endif
	newmetameta(lua, 0, 2, 0, "net"); // L+1 meta
	rawsetiv(lua, M, M_netmeta, L+1); // M_netmeta=meta
	newtable(lua), rawsetnv(lua, L+1, "__index", L+2); // L+2 __index

	push(lua, L), pushcc(lua, Connect, 1), rawsetn(lua, G, "_connect");
	push(lua, L), pushcc(lua, Listen, 1), rawsetn(lua, G, "_listen");
	pushc(lua, hostips), rawsetn(lua, G, "_hostips");

	push(lua, L), pushcc(lua, net_receive, 1), rawsetn(lua, L+2, "receive");
	push(lua, L), pushcc(lua, net_receiving, 1), rawsetn(lua, L+2, "receiving");
	push(lua, L), pushcc(lua, net_send, 1), rawsetn(lua, L+2, "send");
	push(lua, L), pushcc(lua, net_share, 1), rawsetn(lua, L+2, "share");
	push(lua, L), pushcc(lua, net_nagle, 1), rawsetn(lua, L+2, "nagle");
	push(lua, L), pushcc(lua, net_close, 1), rawsetn(lua, L+2, "close");
	push(lua, L), pushcc(lua, net_closed, 1), rawsetn(lua, L+2, "closed");
	push(lua, L), pushcc(lua, net_state, 1), rawsetn(lua, L+2, "state");
	push(lua, L), pushcc(lua, net_connecting, 1), rawsetn(lua, L+2, "connecting");
}
