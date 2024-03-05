// buf[-5]==kind ...
#define SQLBUFLEN (6*1024*1024)
#if LINUX
#define SOCKET int
#endif
// meta = { __metatable='sql', __index=methods }
// sqls = { sql=true, 1=buf }.meta{ __mode='k' }

#pragma pack(push)
#pragma pack(4)
typedef struct
{
	short	 type;
	bool	 conning; // connecting
	bool	 sync; // send Sync
	SOCKET	 sock; // 0 closed
	int		 timeout; // seconds
	int		 due; // seconds
	char	 *buf; // shared
	int		 ready;
	unsigned rown;
	bool	 skip; // skip result
	bool	 parse; // parse statement
	bool	 cols; // column set
	bool	 emptys; // empty set
	uintptr  prepares; // { statement=ptype[1..pn] }, params for connecting
	uintptr	 exclude; // { column=true }
	uintptr  user; // username
	uintptr  pass; // password
} Sql;
#pragma pack(pop)

static void sqlClose(lua_State *lua, int sqls, Sql *sql, const char *reason)
{
	if ( !sql->sock)
		return;
	if (reason[0] != 0)
		lua_pushfstring(lua, "%s sql %p closed: %s", reason ? "INFO" : "WARN",
			sql, reason ? reason : strerror(errno)),
		logErr(pops(lua));
	if ( !sql->conning)
	{
		char term[5] = { 'X', 0, 0, 0, 0 }; // Terminate
		send(sql->sock, term, 5, MSG_NOSIGNAL);
	}
	closesocket(sql->sock), sql->sock = 0, sql->conning = false;
	lua_reftable(lua, sql), pushz(lua), rawset(lua, sqls); // sqls[sql]=nil
}

static int sqlFree(lua_State *lua, const void *sqls, long long sql, long long v, unsigned char closing)
{
	int L = gettop(lua)+1;
	getmetai(lua, global, M_sqls); // L sqls
	sqlClose(lua, L, (Sql*)lua_headtobody((void*)(uintptr)(int)sql, NULL, NULL), "free");
	settop(lua, L-1);
	return true;
}

inline static short sqlR16(char *buf)
{
	return buf[0] << 8 | (unsigned char)buf[1];
}
inline static int sqlR32(char *buf)
{
	char s[] = { buf[3], buf[2], buf[1], buf[0] };
	return *(int*)s;
}
inline static long long sqlR64(char *buf)
{
	char s[] = { buf[7], buf[6], buf[5], buf[4], buf[3], buf[2], buf[1], buf[0] };
	return readL(s);
}
inline static char *sqlW16(char *buf, short i)
{
	buf[0] = (char)(i>>8), buf[1] = (char)i;
	return buf+2;
}
inline static char *sqlW32(char *buf, int i)
{
	char *s = (char*)&i;
	buf[0] = s[3], buf[1] = s[2], buf[2] = s[1], buf[3] = s[0];
	return buf+4;
}
inline static char *sqlW64(char *buf, long long i)
{
	char *s = (char*)&i;
	buf[0] = s[7], buf[1] = s[6], buf[2] = s[5], buf[3] = s[4],
		buf[4] = s[3], buf[5] = s[2], buf[6] = s[1], buf[7] = s[0];
	return buf+8;
}
static char *sqlBuf(lua_State *lua, Sql *sql, char **buf, size_t n)
{
	char *b = *buf;
	if (n > SQLBUFLEN || b + n > sql->buf + SQLBUFLEN)
		sqlClose(lua, upx(1), sql, "send overflow"), error(lua, "sql send overflow");
	*buf = b + n;
	return b;
}
inline static void sqlStr(lua_State *lua, Sql *sql, char **buf, const char *str)
{
	size_t n = strlen(str);
	memcpy(sqlBuf(lua, sql, buf, n+1), str, n+1);
}

static void sqlRecvLen(lua_State *lua, Sql *sql, char *buf, int n)
{
	if (n < 0 || n >= SQLBUFLEN)
		sqlClose(lua, upx(1), sql, "recv overflow"), error(lua, "sql recv overflow");
	for (int m; n > 0; buf += m, n -= m)
	{
		if (m = (int)recv(sql->sock, buf, n, 0), m <= 0)
		{
			if (m==0 || (m = errno != EAGAIN))
			{
				sqlClose(lua, upx(1), sql, m ? strerror(errno) : "disconnected"),
				error(lua, "sql recv error");
			}
			else if ((int)timeNow(1, true) > sql->due)
			{
				sqlClose(lua, upx(1), sql, "recv timeout"), error(lua, "sql recv timeout");
			}
			else
			{
				sched_yield();
			}
		}
	}
}
static char sqlRecv(lua_State *lua, Sql *sql, char kind)
{
	char *buf = sql->buf;
	sqlRecvLen(lua, sql, buf-5, 5);
	char k = buf[-5];
	int n = sqlR32(buf-4)-4;
	sqlRecvLen(lua, sql, buf, n);
//	printf("DEBUG sql recv %c *%d\n", k, n);
	if (k=='E')
	{
		for (int i = 0; i < n-1; i++)
			buf[i] || (buf[i] = '~');
		buf[n] = 0;
		error(lua, "sql error: %s", buf);
	}
	if (k=='Z')
		lua_reftable(lua, sql), buf[1] = 0, pushs(lua, buf),
			rawsetn(lua, -2, "transaction"), pop(lua, 1);
	if (k=='C')
		if (!strncmp(buf, "INSERT", 6) || !strncmp(buf, "UPDATE", 6)
			|| !strncmp(buf, "DELETE", 6))
		{
			unsigned r = 0, d = 1;
			for (char *b = buf+n-2; *b >= '0' && *b <= '9'; b--, d *= 10)
				r += (*b-'0')*d;
			sql->rown += r;
		}
	if (kind && k != kind)
		sqlClose(lua, upx(1), sql, "data unexpected"),
		error(lua, "sql kind %d expected, got %d", kind, k);
	return k;
}
static void sqlSend(lua_State *lua, Sql *sql, char kind, char *end)
{
	char *buf = sql->buf;
	int n = (int)(end - buf);
	if (n < 0 || n >= SQLBUFLEN)
		sqlClose(lua, upx(1), sql, "send overflow"), error(lua, "sql send overflow");
	kind && (buf[-5] = kind), sqlW32(buf-4, n+4);
	buf -= kind ? 5 : 4, n += kind ? 5 : 4;
	for (int m; n > 0; buf += m, n -= m)
	{
		if (m = (int) send(sql->sock, buf, n, MSG_NOSIGNAL), m <= 0)
		{
			if (m < 0 && (m = 0, errno != EAGAIN))
			{
				sqlClose(lua, upx(1), sql, strerror(errno)), error(lua, "sql send error");
			}
			else if ((int)timeNow(1, true) > sql->due)
			{
				sqlClose(lua, upx(1), sql, "send timeout"), error(lua, "sql send timeout");
			}
		}
	}
}

static void sqlReady(lua_State *lua, Sql *sql)
{
	if (sql->sync)
		sql->sync = false, sqlSend(lua, sql, 'S', sql->buf);
	for (; sql->ready > 0; sql->ready--)
		while (sqlRecv(lua, sql, 0) != 'Z')
			;
}
static void sqlPending(lua_State *lua, Sql *sql)
{
	if (sql->sync)
		sql->sync = false, sqlSend(lua, sql, 'S', sql->buf);
	unsigned long n = 0; char b[5];
	while (ioctl(sql->sock, FIONREAD, &n), n >= 5)
	{
		recv(sql->sock, b, 5, MSG_PEEK), ioctl(sql->sock, FIONREAD, &n);
		if ((int)n >= sqlR32(b+1) && sqlRecv(lua, sql, 0) == 'Z')
			sql->ready > 0 && sql->ready--;
	}
}

static struct sockaddr_in sql_addr;

static bool sqlConnect(lua_State *lua, Sql *sql, int sqls)
{
	if ((int)timeNow(1, true) > sql->due)
		sqlClose(lua, sqls, sql, ""), error(lua, "sql init error: timeout");
	if (connect(sql->sock, (struct sockaddr*)&sql_addr, sizeof(sql_addr)) && errno != EISCONN)
	{
		if (errno != EINPROGRESS && errno != EALREADY && errno != EAGAIN)
			sqlClose(lua, sqls, sql, ""), error(lua, "sql init error: %s", strerror(errno));
		else
			return sched_yield(), false;
	}

	// StartUp
	char *buf = sql->buf, *b = buf;
	b = sqlW16(b, 3), b = sqlW16(b, 0);
	sqlStr(lua, sql, &b, "user");
	sqlStr(lua, sql, &b, (char*)lua_headtobody((const void*)sql->user, NULL, NULL));
	if (sql->prepares)
	{
		lua_refhead(lua, (const void*)sql->prepares); // params = prepares
		for (pushz(lua); lua_next(lua, -2); )
			if (isstr(lua, -2) && isstr(lua, -1))
				sqlStr(lua, sql, &b, tostr(lua, -2)), sqlStr(lua, sql, &b, pops(lua));
		pop(lua, 1);
	}
	*b++ = 0;
	sqlSend(lua, sql, 0, b);
	// Authentication
	sqlRecv(lua, sql, 'R');
	if (sqlR32(buf) == 5) // md5
	{
		int salt = *(int*)(buf+4);
		b = buf, sqlStr(lua, sql, &b, (char*)lua_headtobody((const void*)sql->pass, NULL, NULL)), b--;
		sqlStr(lua, sql, &b, (char*)lua_headtobody((const void*)sql->user, NULL, NULL)), b--;
		mdHex((unsigned char*)buf, 0, b-buf, buf);
		*(int*)(buf+32) = salt, mdHex((unsigned char*)buf, 0, 36, buf+3);
		buf[0] = 'm', buf[1] = 'd', buf[2] = '5', buf[35] = 0;
		sqlSend(lua, sql, 'p', buf+35);
		sqlRecv(lua, sql, 'R');
	}
	if (sqlR32(buf) != 0)
		sqlClose(lua, upx(1), sql, "auth failed"), error(lua, "sql auth failed");

	sql->conning = false;
	lua_markbody(lua, sql), sql->user = sql->pass = 0;
	newtablen(lua, 0, 100), sql->prepares = (uintptr) pophead(lua);
	return true;
}

// -4|6 addr,user,password,timeout,params,connectingTimeout +2 sql,params *1 sqls
static int sql_sql(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TSTRING);
	checkArg(lua, 2, "#2", LUA_TSTRING);
	checkArg(lua, 3, "#3", LUA_TSTRING);
	checkArg(lua, 4, "#4", LUA_TNUMBER);
	checkArgableZ(lua, 5, "#5", LUA_TTABLE);
	checkArgableZ(lua, 6, "#6", LUA_TNUMBER);
	struct sockaddr_in6 ad = checkAddr(lua, (char*)tostr(lua, 1), false);
	int timeout = range(roundint(lua, 4), 1, 86400);
	SOCKET sock = socket(ad.sin6_family, SOCK_STREAM, IPPROTO_TCP);
	if (sock == -1)
		return error(lua, "sql init error: %s", strerror(errno));
	int on = 1; ioctl(sock, FIONBIO, (unsigned long*)&on);
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on));
	setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&on, sizeof(on));
	on = NETBUFLEN, setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&on, sizeof(on));
	on = NETBUFLEN, setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&on, sizeof(on));
#if MAC
	setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, (void*)&on, sizeof(on));
#endif
	if (connect(sock, (struct sockaddr*)&ad, sizeof(ad))
		&& errno != EINPROGRESS && errno != EAGAIN)
		return closesocket(sock), error(lua, "sql init error: %s", strerror(errno));

	int L = gettop(lua)+1;
	Sql *sql = (Sql*)lua_createbody(lua, 4, 4, sizeof(Sql), 4); // L sql
	getmetai(lua, global, M_sqlmeta), setmeta(lua, L); // sql.meta=meta
	sql->type = M_sqls, sql->conning = !isnil(lua, 6), sql->sync = false;
	sql->sock = sock, sql->timeout = timeout;
	char *buf = (rawgeti(lua, upx(1), 1), (char*)popudata(lua)+5); // buf=sqls[1]
	sql->buf = buf, sql->ready = 0;
	sql->skip = sql->parse = sql->cols = sql->emptys = false;
	sql->prepares = (uintptr) tohead(lua, 5), sql->user = (uintptr) tohead(lua, 2), sql->pass = (uintptr) tohead(lua, 3);
	pushb(lua, true), rawsetk(lua, upx(1), L); // sqls[sql]=true
	if (sql->conning)
		return sql->due = (int)timeNow(1, true) + range(roundint(lua, 6), 1, 86400),
			push(lua, L), 1;

	sql->due = (int)timeNow(1, true) + timeout;
	while ( !sqlConnect(lua, sql, upx(1)))
		;
	newtable(lua); // L+1 params
	char kind;
	while (kind = sqlRecv(lua, sql, 0), kind != 'Z')
		if (kind=='S')
			pushs(lua, buf+strlen(buf)+1), rawsetn(lua, L+1, buf);
	pushs(lua, "I"), rawsetn(lua, L, "transaction");
	return push(lua, L), push(lua, L+1), 2;
}

// -1 sql +1 isconnected *1 sqls
static int sql_connected(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->conning && sqlConnect(lua, sql, upx(1));
	return pushb(lua, sql->sock && !sql->conning), 1;
}

// -1 sql +0 *1 sqls
static int sql_close(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sqlClose(lua, upx(1), sql, "");
	return 0;
}

// -1 sql +1 isclosed
static int sql_closed(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	return pushb(lua, !sql->sock), 1;
}

// -2 sql,statements +0 *1 sqls
static int sql_runs(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->sock || error(lua, "sql closed");
	checkArg(lua, 2, "#2", LUA_TSTRING);
	bool skip = sql->skip; sql->skip = false;
	sql->conning && !sqlConnect(lua, sql, upx(1)) && error(lua, "sql connecting");

	sql->due = (int)timeNow(1, true)+sql->timeout;
	skip ? sqlPending(lua, sql) : sqlReady(lua, sql), sql->ready++;
	// Query
	char *buf = sql->buf;
	sqlStr(lua, sql, &buf, tostr(lua, 2)); // statement
	sqlSend(lua, sql, 'Q', buf);
	skip || (sqlReady(lua, sql), 0);
	return 0;
}

static char *sqlEnc(lua_State *lua, Sql *sql, int type, int index, char *buf, int p, unsigned px)
{
	if (isnil(lua, index))
		return sqlW32(buf, -1), buf+4;
	size_t n = 0;
	switch (type)
	{
	case 16: // bool
		n = 1, buf[4] = tobool(lua, index); break;
	case 1043: case 1042: case 17: case 18: case 19: case 25: // varchar bpchar bytea char name text
		if (isnum(lua, index))
			buf = sqlBuf(lua, sql, &buf, 28),
			n = lua_number2str(buf+4, tonum(lua, index));
		else
		{
			const char *s = tobytes(lua, index, &n, NULL);
			if (!s)
				error(lua, "bad parameter $%d[%d] (%s expected, got %s)", p, px,
					type==1043 ? "varchar" : type==1042 ? "bpchar" : type==17 ? "bytea"
					: type==18 ? "char" : type==19 ? "name" : "text", tonamex(lua, index));
			buf = sqlBuf(lua, sql, &buf, 4+n);
			memcpy(buf+4, s, n);
		}
		break;
	case 21: // int2
	{
		int v = mustint(lua, index);
		if ((int)(short)v != v)
			error(lua, "bad parameter $%d[%d] %f out of 16bit range",
				p, px, tonum(lua, 1));
		n = 2, sqlW16(buf+4, (short)v); break;
	}
	case 23: // int4
		n = 4, sqlW32(buf+4, mustint(lua, index)); break;
	case 20: case 1114: // int8 timestamp
		n = 8, sqlW64(buf+4, mustlong(lua, index)); break;
	case 700: // float4
		{ float v = (float)tonum(lua, index); n = 4, sqlW32(buf+4, *(int*)&v); break; }
	case 701: // float8
		{ double v = tonum(lua, index); n = 8, sqlW64(buf+4, *(long long*)&v); break; }
	case 650: case 869: // cidr inet
	{
		n = 8, sqlW32(buf+4, type==650 ? 0x02200104 : 0x02200004);
		double V = tonum(lua, index); long long vv = (long long)V; unsigned v = (unsigned)vv;
		if (V == 0 && !lua_isnumber(lua, index))
			error(lua, "unsigned 32bit int expected, got %s", tonamex(lua, index));
		if (v != V || v != vv)
			error(lua, "number %f must be unsigned 32bit int", V);
		sqlW32(buf+8, (int)v);
		break;
	}
	case 651: case 1000: case 1001: case 1002: case 1003: case 1016: case 1005: case 1007:
	case 1009: case 1021: case 1022: case 1014: case 1015: case 1041: case 1115: // array
	{
		if (totype(lua, index) != LUA_TTABLE)
			error(lua, "bad parameter $%d (table expected, got %s)", p, tonamex(lua, index));
		sqlW32(buf+4, 1), sqlW32(buf+8, 0);
		int hi = (int)tolen(lua, index)+1, low = 1;
		int elem = 0;
		switch (type)
		{
		case 640: elem = 650; break; case 1000: elem = 16; break; case 1001: elem = 17; break;
		case 1002: elem = 18; break; case 1003: elem = 19; break; case 1016: elem = 20; break;
		case 1005: elem = 21; break; case 1007: elem = 23; break; case 1009: elem = 25; break;
		case 1021: elem = 700; break; case 1022: elem = 701; break;
		case 1014: elem = 1042; break; case 1015: elem = 1043; break;
		case 1041: elem = 1040; break; case 1115: elem = 1114; break;
		}
		sqlW32(buf+12, elem), sqlW32(buf+16, hi-low), sqlW32(buf+20, low);
		char *b = buf+24;
		for (; low < hi; low++)
			tabgeti(lua, index, low), b = sqlEnc(lua, sql, elem, -1, b, p, low), pop(lua, 1),
			b = sqlBuf(lua, sql, &b, 16);
		n = b-buf-4;
		break;
	}
	default:
		error(lua, "bad parameter $%d[%d] (%d expected, got %s)",
			p, px, type, tonamex(lua, index));
	}
	sqlW32(buf, (int)n);
	return buf+4+n;
}
static char *sqlDec(lua_State *lua, Sql *sql, int type, char *buf)
{
	size_t n = sqlR32(buf);
	if ((int)n == -1)
		n = 0, pushz(lua);
	else switch (type)
	{
	case 0: // excluded
		pushz(lua); break;
	case 16: // bool
		pushb(lua, buf[4]); break;
	case 1043: case 1042: case 18: case 19: case 25: // varchar bpchar char name text
		pushsl(lua, buf+4, n); break;
	case 21: // int2
		pushi(lua, sqlR16(buf+4)); break;
	case 23: // int4
		pushi(lua, sqlR32(buf+4)); break;
	case 20: case 1114: // int8 timestamp
		pushn(lua, sqlR64(buf+4)); break;
	case 700: // float4
		{ int v = sqlR32(buf+4); pushn(lua, *(float*)&v); break; }
	case 701: // float8
		{ long long v = sqlR64(buf+4); pushn(lua, *(double*)&v); break; }
	case 650: case 869: // cidr inet
		if (buf[4]==2 && buf[7]==4)
			pushn(lua, (unsigned)sqlR32(buf+8));
		else
			memcpy(newbdata(lua, n), buf+4, n);
		break;
	case 651: case 1000: case 1001: case 1002: case 1003: case 1016: case 1005: case 1007:
	case 1009: case 1021: case 1022: case 1014: case 1015: case 1041: case 1115: // array
		if (n==12) // 0 dimension
		{
			newtable(lua);
		}
		else if (sqlR32(buf+4)==1) // 1 dimension
		{
			unsigned elem = sqlR32(buf+12);
			unsigned len = sqlR32(buf+16), low = sqlR32(buf+20), hi = low+len;
			newtablen(lua, hi-1 <= 1024 ? hi-1 : 0, 0);
			for (char *b = buf+24; low < hi; low++)
				b = sqlDec(lua, sql, elem, b), rawseti(lua, -2, low);
		}
		else
		{
			memcpy(newbdata(lua, n), buf+4, n);
		}
		break;
	default:
		memcpy(newbdata(lua, n), buf+4, n);
	}
	return buf+4+n;
}

int runbuff_cn = -1;

// sql, cn, buffs
static int sql_runbuffs(lua_State *lua)
{
	if (!isnum(lua, 2))
		error(lua, "argument #1 number expected");
	if (!lua_istable(lua, 3))
		error(lua, "argument #2 table expected");

	runbuff_cn = tonum(lua, 2);

	getmeta(lua, global); // sql, num, buffers, global
	rawsetiv(lua, -1, M_sqlrunbuffs, 3);

	return 0;
}

static int sql_newtablen(lua_State *lua, int cn, int rn)
{
	if (rn == 0)
	{
		newtablen(lua, 0, cn);
		return 0;
	}

	if (runbuff_cn != cn)
	{
		newtablen(lua, 0, cn);
		if (runbuff_cn != -1)
			logErr("SQL buffers column number mismatch");
		return 0;
	}

	getmetai(lua, global, M_sqlrunbuffs); //buffers
	if (isnil(lua, -1) || tolen(lua, -1) == 0)
	{
		pop(lua, 1);
		newtablen(lua, 0, cn);
		logErr("SQL buffers is nil/empty");
		return 0;
	}

	if (rn == -1)
		rn = tolen(lua, -1);

	rawgeti(lua, -1, rn); //buffers, buffer[rn]
	if (isnil(lua, -1))
	{
		pop(lua, 2);
		newtablen(lua, 0, cn);
		logErr("SQL buffer is nil, create new one");
		return rn-1;
	}

	getmeta(lua, global); //buffers, buffer[rn], global
	rawsetiv(lua, -1, M_sqlrunbuff, -2);
	pop(lua, 2), pushz(lua); //buffers, nil
	rawseti(lua, -2, rn); //buffers
	pop(lua, 1);

	getmetai(lua, global, M_sqlrunbuff);
	
	return rn-1;
}

// -2* sql,statement,param... +1 nil|data|rown *1 sqls
static int sql_run(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->sock || error(lua, "sql closed");
	checkArg(lua, 2, "#2", LUA_TSTRING);
	int L = gettop(lua);
	sql->exclude ? lua_refhead(lua, (const void*)sql->exclude) : pushz(lua); // L+1 exclude
	bool skip = sql->skip, parse = sql->parse, cols = sql->cols, emptys = sql->emptys;
	sql->skip = sql->parse = sql->cols = sql->emptys = false;
	lua_markbody(lua, sql), sql->exclude = 0;
	sql->conning && !sqlConnect(lua, sql, upx(1)) && error(lua, "sql connecting");

	sql->due = (int)timeNow(1, true)+sql->timeout;
	lua_refhead(lua, (const void*)sql->prepares); // L+2 prepares
	char prepa[30] = "";
	parse || sprintf(prepa, "prepare%p", tohead(lua, 2));
	int pn = 0, *pts = NULL;
	if (prepa[0])
	{
		if (rawgetk(lua, L+2, 2), pts = (int*)toudata(lua, -1))
			pn = (int)poplen(lua)/4-1;
		else
			pop(lua, 1);
	}
	skip = pts && skip, skip ? sqlPending(lua, sql) : sqlReady(lua, sql);
	sql->sync = true, sql->ready++, sql->rown = 0;

	char *buf;
	if ( !pts)
	{
		// Close
		buf = sql->buf;
		*buf++ = 'S', sqlStr(lua, sql, &buf, prepa); // prepare name
		sqlSend(lua, sql, 'C', buf);
		// Parse
		buf = sql->buf;
		sqlStr(lua, sql, &buf, prepa); // prepare name
		sqlStr(lua, sql, &buf, tostr(lua, 2)); // statement
		sqlW16(sqlBuf(lua, sql, &buf, 2), 0); // skip parameter
		sqlSend(lua, sql, 'P', buf);
	}
	int Pts[128], p, cn = 0, cm = 0, cts[128], c;
	if ( !skip)
	{
		// Describe
		buf = sql->buf;
		*buf++ = 'S', sqlStr(lua, sql, &buf, prepa); // prepare name
		sqlSend(lua, sql, 'D', buf), sqlSend(lua, sql, 'H', sql->buf);
		if ( !pts)
		{
			// param descs
			while (sqlRecv(lua, sql, 0) != 't');
			buf = sql->buf;
			pn = sqlR16(buf), buf += 2;
			if (pn >= 128)
				error(lua, "sql error: too many parameters %d", pn);
			if (pn != L-2)
				error(lua, "bad argument (%d parameters expected, got %d)", pn, L-2);
			pts = prepa[0] ? (int*)newudata(lua, pn*4+4) : Pts;
			for (p = 1; p <= pn; p++, buf += 4)
				pts[p] = sqlR32(buf);
			prepa[0] && (rawsetk(lua, L+2, 2), 0); // prepares[statement]=ptype[1..pn]
		}
		// column descs
		for (char k; sqlW16(sql->buf, 0), k = sqlRecv(lua, sql, 0), k != 'T' && k != 'n'; );
		buf = sql->buf;
		cn = sqlR16(buf), buf += 2;
		if (cn >= 128)
			error(lua, "sql too many columns");
		cm = gettop(lua);
		for (c = 1; c <= cn; c++, buf += 4+2+4+2+4+2)
		{
			size_t n = strlen(buf); pushsl(lua, buf, n), buf += n+1; // cm+c column name
			if (isnil(lua, L+1) || (rawgetk(lua, L+1, -1), popz(lua)))
				cts[c] = sqlR32(buf+4+2);
			else
				cts[c] = 0; // excluded
//			printf("DEBUG column: %s %d\n", tostr(lua, cm+c), cts[c]);
		}
	}
	// Bind
	buf = sql->buf;
	*buf++ = 0; // portal name
	sqlStr(lua, sql, &buf, prepa); // prepare name
	buf = sqlW16(buf, 1), buf = sqlW16(buf, 1); // binary parameter
	buf = sqlW16(buf, pn);
	for (p = 1; p <= pn; p++)
		buf = sqlEnc(lua, sql, pts[p], p+2, buf, p, 0), // params[p]=args[p+2]
		buf = sqlBuf(lua, sql, &buf, 16);
	buf = sqlW16(buf, 1), buf = sqlW16(buf, 1); // binary result
	sqlSend(lua, sql, 'B', buf);

	// Execute
	buf = sql->buf;
	*buf++ = 0; // portal name
	buf = sqlW32(buf, 0); // all rows
	sqlSend(lua, sql, 'E', buf);
	if (skip)
		return 0;

	if (cn == 0)
		return sqlReady(lua, sql), pushn(lua, sql->rown), 1;
	int r = 0;
	sqlSend(lua, sql, 'H', sql->buf);
	int D = gettop(lua)+1;
	int rn = -1;
	if (!emptys)
		pushz(lua);
	else if (cols)
		for (rn = sql_newtablen(lua, cn, rn), c = 1; c <= cn; c++) // D data
			cts[c] ? newtablen(lua, 8, 0), rawsetkv(lua, D, cm+c, -1) : pushz(lua); // D+c cols
	else
		newtablen(lua, 8, 0); // D data
	for (char k; k = sqlRecv(lua, sql, 0), k != 'C'; )
	{
		if (k != 'D') continue;
		if (isnil(lua, D))
		{
			if (pop(lua, 1), cols)
			{
				for (rn = sql_newtablen(lua, cn, rn), c = 1; c <= cn; c++) // D data
					cts[c] ? newtablen(lua, 8, 0), rawsetkv(lua, D, cm+c, -1)
						: pushz(lua); // D+c cols
			}
			else
			{
				newtablen(lua, 8, 0); // D data
			}
		}
		r++;
		buf = sql->buf;
		if (sqlR16(buf) != cn)
			sqlClose(lua, upx(1), sql, "invalid DataRow"), runbuff_cn = -1,
			error(lua, "sql data error %d", sqlR16(buf));
		if (!cols)
			rn = sql_newtablen(lua, cn, rn); // row
		for (buf += 2, c = 1; c <= cn; c++)
			buf = sqlDec(lua, sql, cts[c], buf),
			cts[c] ? cols ? rawseti(lua, D+c, r) : rawsetk(lua, -2, cm+c) : pop(lua, 1);
		if (!cols)
			rawseti(lua, D, r);
	}
	runbuff_cn = -1;
	sqlReady(lua, sql);
	return push(lua, D), 1;
}

// -1 sql +1 sql
static int sql_skip(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->skip = true;
	return push(lua, 1), 1;
}
// -1 sql +1 sql
static int sql_parse(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->parse = true;
	return push(lua, 1), 1;
}
// -1 sql +1 sql
static int sql_emptys(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->emptys = true;
	return push(lua, 1), 1;
}
// -1 sql +1 sql
static int sql_columns(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->cols = true;
	return push(lua, 1), 1;
}
// -1|2 sql,cleanPrepares +1 sql
static int sql_default(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	sql->skip = sql->parse = sql->cols = sql->emptys = false;
	lua_markbody(lua, sql), sql->exclude = 0;
	if (tobool(lua, 2) && !sql->conning)
		newtablen(lua, 0, 100), sql->prepares = (uintptr) pophead(lua);
	return push(lua, 1), 1;
}
// -1* sql,colomn... +1 sql
static int sql_exclude(lua_State *lua)
{
	Sql *sql = (Sql*)totab(lua, 1);
	if ( !sql || sql->type != M_sqls)
		return errArgT(lua, "#1", "sql", totype(lua, 1));
	int L = gettop(lua);
	for (int i = 2; i <= L; i++)
		if ( !isstr(lua, i))
			error(lua, "bad argument #%d (string expected, got %s)", i, tonamex(lua, i));
	lua_markbody(lua, sql);
	newtablen(lua, 0, L-2), sql->exclude = (uintptr) tohead(lua, -1);
	for (int i = 2; i <= L; i++)
		pushb(lua, true), rawsetk(lua, -2, i);
	return push(lua, 1), 1;
}

static void sql(lua_State *lua, int M, int G)
{
	int L = gettop(lua)+1;
	newtablen(lua, 4, 0), newmetaweak(lua, 0, 0, 10, "k"), setmeta(lua, L); // L sqls
	lua_setweaking(lua, L, sqlFree), rawsetiv(lua, M, M_sqls, L); // M_sqls=sqls
	newmetameta(lua, 0, 2, 0, "sql"); // L+1 meta
	rawsetiv(lua, M, M_sqlmeta, L+1); // M_sqlmeta=meta
	newtable(lua), rawsetnv(lua, L+1, "__index", L+2); // L+2 __index
	((char*)newudata(lua, 5+SQLBUFLEN+1))[5+SQLBUFLEN] = 0, rawseti(lua, L, 1); // sqls[1]=buf
	memset(&sql_addr, 0, sizeof(sql_addr)), sql_addr.sin_family = AF_INET;
	sql_addr.sin_addr.s_addr = htonl(0x7F000001), sql_addr.sin_port = htons(65535);

	push(lua, L), pushcc(lua, sql_sql, 1), rawsetn(lua, G, "_sql");
	push(lua, L), pushcc(lua, sql_connected, 1), rawsetn(lua, L+2, "connected");
	push(lua, L), pushcc(lua, sql_close, 1), rawsetn(lua, L+2, "close");
	push(lua, L), pushcc(lua, sql_runs, 1), rawsetn(lua, L+2, "runs");
	push(lua, L), pushcc(lua, sql_run, 1), rawsetn(lua, L+2, "run");
	pushc(lua, sql_closed), rawsetn(lua, L+2, "closed");
	pushc(lua, sql_skip), rawsetn(lua, L+2, "skip");
	pushc(lua, sql_parse), rawsetn(lua, L+2, "parse");
	pushc(lua, sql_columns), rawsetn(lua, L+2, "columns");
	pushc(lua, sql_emptys), rawsetn(lua, L+2, "emptys");
	pushc(lua, sql_default), rawsetn(lua, L+2, "default");
	pushc(lua, sql_exclude), rawsetn(lua, L+2, "exclude");
	pushc(lua, sql_runbuffs), rawsetn(lua, L+2, "runbuffer");
}
