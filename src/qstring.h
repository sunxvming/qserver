// -1|3 bytes,first,last +1 string
static int str_tostr(lua_State *lua)
{
	size_t len; const char *s = tobytes(lua, 1, &len, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), len), j = indexn(luaL_optint(lua, 3, -1), len);
	if (isstr(lua, 1) && i==0 && j==len)
		return push(lua, 1), 1;
	return pushsl(lua, s+i, j >= i ? j-i : 0), 1;
}

// -1|4 bytes,first,last,share +1 bytes
static int str_tobytes(lua_State *lua)
{
	size_t len; const char *s = tobytes(lua, 1, &len, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), len), j = indexn(luaL_optint(lua, 3, -1), len);
	if (tobool(lua, 4) && totype(lua, 1)==LUA_TUSERDATA && i==0 && j==len)
		return push(lua, 1), 1;
	return memcpy(newbdata(lua, j-i), s+i, j >= i ? j-i : 0), 1;
}

// -1|3 bytes,offset,\0end +1 bytelen
static int str_len(lua_State *lua)
{
	size_t len; const char *s = tobytes(lua, 1, &len, "#1");
	size_t n = indexn0(touint(lua, 2), len);
	if (tobool(lua, 3))
		len = strlen((char*)s+n);
	else
		len -= n;
	return pushn(lua, len), 1;
}

// -1|3 bytes,offset,\0end +1 utflen
static int str_ulen(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	size_t n = indexn0(touint(lua, 2), len);
	unsigned char *m = s + (tobool(lua, 3) ? strlen((char*)s+n) : len);
	for (s += n, n = 0; s < m; n++)
		s += *s <= 0x7f ? 1 : *s >= 0xe0 ? 3 : *s >= 0xc0 ? 2 : (n--, 1);
	s > m && n--;
	return pushn(lua, n), 1;
}

// -1|3 bytes,first,last +* byte...
static int str_byte(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	size_t n = tolen(lua, 1);
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, i+1), n);
	n = j-i;
	while (i < j)
		pushn(lua, s[i++]);
	return (int)n > 0 ? (int)n : 0;
}

// -1|3 bytes,offset,signed +1 littleendian
static int str_to16l(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 2)
		return error(lua, "bad argument #1 (2 bytes expected, got less bytes)");
	short v = *(short*)(s+rangez(touint(lua, 2), 1, len)-1);
	pushi(lua, tobool(lua, 3) ? v : (unsigned short)v);
	return 1;
}
// -1|3 bytes,offset,signed +1 bigendian
static int str_to16b(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 2)
		return error(lua, "bad argument #1 (2 bytes expected, got less bytes)");
	s += rangez(touint(lua, 2), 1, len)-1;
	char S[2];
	S[0] = s[1];
	S[1] = s[0];
	pushi(lua, tobool(lua, 3) ? *(short*)S : *(unsigned short*)S);
	return 1;
}

// -1|3 bytes,offset,signed +1 littleendian
static int str_to32l(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 4)
		return error(lua, "bad argument #1 (4 bytes expected, got less bytes)");
	int v = *(int*)(s+rangez(touint(lua, 2), 1, len)-1);
	if (tobool(lua, 3))
		pushi(lua, v);
	else
		pushn(lua, (unsigned)v);
	return 1;
}
// -1|3 bytes,offset,signed +1 bigendian
static int str_to32b(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 4)
		return error(lua, "bad argument #1 (4 bytes expected, got less bytes)");
	s += rangez(touint(lua, 2), 1, len)-1;
	char S[4];
	S[0] = s[3];
	S[1] = s[2];
	S[2] = s[1];
	S[3] = s[0];
	if (tobool(lua, 3))
		pushi(lua, *(int*)S);
	else
		pushn(lua, *(unsigned*)S);
	return 1;
}

// -1|3 bytes,offset,signed +1 littleendian
static int str_to64l(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 8)
		return error(lua, "bad argument #1 (8 bytes expected, got less bytes)");
	long long v = readL(s+rangez(touint(lua, 2), 1, len)-1);
	double V;
	if (tobool(lua, 3) ? (V = (double)v, v != v<<10>>10)
		: (V = (double)(unsigned long long)v, (unsigned long long)v >= 1LL<<53))
		return error(lua, "%f out of unsigned 53 or signed 54bit range", V);
	return pushn(lua, V), 1;
}
// -1|3 bytes,offset,signed +1 bigendian
static int str_to64b(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 8)
		return error(lua, "bad argument #1 (8 bytes expected, got less bytes)");
	s += rangez(touint(lua, 2), 1, len)-1;
	char S[8];
	S[0] = s[7];
	S[1] = s[6];
	S[2] = s[5];
	S[3] = s[4];
	S[4] = s[3];
	S[5] = s[2];
	S[6] = s[1];
	S[7] = s[0];
	long long v = readL(S);
	double V;
	if (tobool(lua, 3) ? (V = (double)v, v != v<<10>>10)
		: (V = (double)(unsigned long long)v, (unsigned long long)v >= 1LL<<53))
			return error(lua, "%f out of unsigned 53 or signed 54bit range", V);
	return pushn(lua, V), 1;
}

// -1|2 bytes,offset +1 littleendian
static int str_toDl(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 8)
		return error(lua, "bad argument #1 (8 bytes expected, got less bytes)");
	pushn(lua, readD(s+rangez(touint(lua, 2), 1, len)-1));
	return 1;
}
// -1|2 bytes,offset +1 bigendian
static int str_toDb(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	if (len < 8)
		return error(lua, "bad argument #1 (8 bytes expected, got less bytes)");
	s += rangez(touint(lua, 2), 1, len)-1;
	char S[8];
	S[0] = s[7];
	S[1] = s[6];
	S[2] = s[5];
	S[3] = s[4];
	S[4] = s[3];
	S[5] = s[2];
	S[6] = s[1];
	S[7] = s[0];
	return pushn(lua, readD(S)), 1;
}

// -1|2 num,bdata +1 littleendian
static int str_from16l(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	long long v = (long long)tonum(lua, 1);
	if (v >= 1<<16 || v < -1<<15)
		return error(lua, "%f out of 16bit range", tonum(lua, 1));
	if (tobool(lua, 2))
		*(short*)newbdata(lua, 2) = (short)v;
	else
		pushsl(lua, (char*)&v, 2);
	return 1;
}
// -1|2 num,bdata +1 bigendian
static int str_from16b(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	long long v = (long long)tonum(lua, 1);
	if (v >= 1<<16 || v < -1<<15)
		return error(lua, "%f out of 16bit range", tonum(lua, 1));
	char *s = (char*)&v; char S[] = { s[1],s[0] };
	if (tobool(lua, 2))
		*(short*)newbdata(lua, 2) = *(short*)S;
	else
		pushsl(lua, S, 2);
	return 1;
}

// -1|2 num,bdata +1 littleendian
static int str_from32l(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	long long v = (long long)tonum(lua, 1);
	if (v >= 1LL<<32 || v < -1<<31)
		return error(lua, "%f out of 32bit range", tonum(lua, 1));
	if (tobool(lua, 2))
		*(int*)newbdata(lua, 4) = (int)v;
	else
		pushsl(lua, (char*)&v, 4);
	return 1;
}
// -1|2 num,bdata +1 bigendian
static int str_from32b(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	long long v = (long long)tonum(lua, 1);
	if (v >= 1LL<<32 || v < -1<<31)
		return error(lua, "%f out of 32bit range", tonum(lua, 1));
	char *s = (char*)&v; char S[] = { s[3],s[2],s[1],s[0] };
	if (tobool(lua, 2))
		*(int*)newbdata(lua, 4) = *(int*)S;
	else
		pushsl(lua, S, 4);
	return 1;
}

// -1|2 num,bdata +1 littleendian
static int str_from64l(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	long long v = roundlong(lua, 1);
	if (tobool(lua, 2))
		*(long long*)newbdata(lua, 8) = v;
	else
		pushsl(lua, (char*)&v, 8);
	return 1;
}
// -1|2 num,bdata +1 bigendian
static int str_from64b(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	long long v = roundlong(lua, 1);
	char *s = (char*)&v; char S[] = { s[7],s[6],s[5],s[4],s[3],s[2],s[1],s[0] };
	if (tobool(lua, 2))
		*(long long*)newbdata(lua, 8) = readL(S);
	else
		pushsl(lua, S, 8);
	return 1;
}

// -1|2 num,bdata +1 bytes
static int str_fromDl(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	double v = tonum(lua, 1);
	if (tobool(lua, 2))
		*(double*)newbdata(lua, 8) = v;
	else
		pushsl(lua, (char*)&v, 8);
	return 1;
}
// -1|2 num,bdata +1 bigendian
static int str_fromDb(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	double v = tonum(lua, 1);
	char *s = (char*)&v; char S[] = { s[7],s[6],s[5],s[4],s[3],s[2],s[1],s[0] };
	if (tobool(lua, 2))
		*(double*)newbdata(lua, 8) = readD(S);
	else
		pushsl(lua, S, 8);
	return 1;
}

// -2 bytes,lead +1 ifLead
static int str_lead(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	size_t llen; unsigned char *ls = (unsigned char*)tobytes(lua, 2, &llen, "#2");
	pushb(lua, len >= llen && memcmp(s, ls, llen)==0);
	return 1;
}
// -2 bytes,tail +1 ifTail
static int str_tail(lua_State *lua)
{
	size_t len; unsigned char *s = (unsigned char*)tobytes(lua, 1, &len, "#1");
	size_t tlen; unsigned char *ts = (unsigned char*)tobytes(lua, 2, &tlen, "#2");
	pushb(lua, len >= tlen && memcmp(s+len-tlen, ts, tlen)==0);
	return 1;
}

// -1|4 bytes,first,last,bdata|'c' +1... littleendian16bits|wchar...
static int str_ucs(lua_State *lua)
{
	size_t n, m; unsigned char *cs = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	bool bdata = isbool(lua, 4);
	unsigned char *c, *cj = cs+j;
	for (n = 0, c = cs+i; c < cj; n++)
		c += *c <= 0x7f ? 1 : *c >= 0xe0 ? 3 : *c >= 0xc0 ? 2 : (n--, 1);
	c > cj && n > 0 && n--;
	if (isstr(lua, 4) && tostr(lua, 4)[0]=='c')
	{
		for (c = cs+i, m = n; m > 0; m--)
		{
			if ( *c <= 0x7f )
			{
				pushi(lua, *c++);
			}
			else
			{
				if ( *c >= 0xe0 )
				{
					pushi(lua, ( c[0]-0xe0 ) << 12 | ( c[1]-0x80 ) << 6 | c[2]-0x80);
					c += 3;
				}
				else
				{
					if ( *c >= 0xc0 )
					{
						pushi(lua, ( c[0]-0xc0 ) << 6 | c[1]-0x80);
						c += 2;
					}
					else
					{
						c++;
					}
				}
			}
		}
		return (int)n;
	}
	unsigned short *ws = (unsigned short*)(bdata ? newbdata(lua, n+n) : malloc(n+n)), *w;
	for (c = cs+i, w = ws, m = n; m > 0; m--)
	{
		if ( *c <= 0x7f )
		{
			*w++ = *c++;
		}
		else
		{
			if ( *c >= 0xe0 )
			{
				*w++ = ( c[0]-0xe0 ) << 12 | ( c[1]-0x80 ) << 6 | c[2]-0x80;
				c += 3;
			}
			else
			{
				if ( *c >= 0xc0 )
				{
					*w++ = ( c[0]-0xc0 ) << 6 | c[2]-0x80;
					c += 2;
				}
				else
				{
					c++;
				}
			}
		}
	}
	if ( !bdata)
		pushsl(lua, (char*)ws, n+n), free(ws);
	return 1;
}

// -1|4 littleendian16bits,first,last,bdata +1 bytes
static int str_utf(lua_State *lua)
{
	size_t n; unsigned short *ws = (unsigned short*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n>>1), j = indexn(luaL_optint(lua, 3, -1), n>>1);
	bool bdata = tobool(lua, 4);
	unsigned short *w, *wj = ws+j;
	for (n = 0, w = ws+i; w < wj; w++)
		n += *w <= 0x7f ? 1 : *w <= 0x7ff ? 2 : 3;
	char *cs = bdata ? newbdata(lua, n) : (char*)malloc(n), *c;
	for (w = ws+i, c = cs; w < wj; w++)
		*w <= 0x7f ? *c++ = (char)*w
		: *w <= 0x7ff ? (*c++ = (char)(*w>>6|0xc0), *c++ = (char)((*w&0x3f)|0x80))
		: (*c++ = (char)(*w>>12|0xe0), *c++ = (char)((*w>>6&0x3f)|0x80), *c++ = (char)((*w&0x3f)|0x80));
	if ( !bdata)
		pushsl(lua, cs, n), free(cs);
	return 1;
}

// -1|4 bytes,first,last,bdata +1 encodedUrl
static int str_enurl(lua_State *lua)
{
	size_t n; unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	bool bdata = tobool(lua, 4);
	unsigned char *c, *cj = s+j;
	for (n = 0, c = s+i; c < cj; c++)
		n += (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')
			|| *c == '-' || *c == '_' || *c == '.' ? 1 : 3;
	unsigned char *es = (unsigned char*)(bdata ? newbdata(lua, n) : malloc(n)), *e;
	for (c = s+i, e = es; c < cj; )
	{
		if ( (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')
			|| *c == '-' || *c == '_' || *c == '.' )
		{
			*e++ = *c++;
		}
		else
		{
			*e++ = '%';
			*e++ = "0123456789ABCDEF"[*c>>4&15];
			*e++ = "0123456789ABCDEF"[*c&15];
			c++;
		}
	}
	if ( !bdata)
		pushsl(lua, (char*)es, n), free(es);
	return 1;
}

static int str_enurl2(lua_State *lua)
{
	size_t n; unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	bool bdata = tobool(lua, 4);
	unsigned char *c, *cj = s+j;
	for (n = 0, c = s+i; c < cj; c++)
		n += (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')
		|| *c == '-' || *c == '_' || *c == '.' || *c == ' ' ? 1 : 3;
	unsigned char *es = (unsigned char*)(bdata ? newbdata(lua, n) : malloc(n)), *e;
	for (c = s+i, e = es; c < cj; )
	{
		if ( (*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z')
			|| *c == '-' || *c == '_' || *c == '.' )
		{
			*e++ = *c++;
		}
		else if ( *c == ' ' )
		{
			*e++ = '+';
			c++;
		}
		else
		{
			*e++ = '%';
			*e++ = "0123456789ABCDEF"[*c>>4&15];
			*e++ = "0123456789ABCDEF"[*c&15];
			c++;
		}
	}
	if ( !bdata)
		pushsl(lua, (char*)es, n), free(es);
	return 1;
}

// -1|4 bytes,first,last,bdata +1 decodedUrl
static int str_deurl(lua_State *lua)
{
	size_t n; unsigned char *es = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	bool bdata = tobool(lua, 4);
	unsigned char *e, *ej = es+j;
	for (n = 0, e = es+i; e < ej; n++)
		e += *e == '%' ? 3 : 1;
	unsigned char *s = (unsigned char*)(bdata ? newbdata(lua, n) : malloc(n)), *c;
	for (e = es+i, c = s; e < ej; )
	{
		if ( *e != '%' )
		{
			*c++ = *e++;
		}
		else
		{
			if ( e + 3 > ej )
			{
				*c++ = '%';
				e += 3;
			}
			else
			{
				*c++ = (char)((e[1] >= 'A' ? e[1]-'A'+10 : e[1]-'0')<<4
							| ((e[2] >= 'A' ? e[2]-'A'+10 : e[2]-'0')&0xFF));
				e += 3;
			}
		}
	}
	if ( !bdata)
		pushsl(lua, (char*)s, n), free(s);
	return 1;
}

static int str_deurl2(lua_State *lua)
{
	size_t n; unsigned char *es = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	bool bdata = tobool(lua, 4);
	unsigned char *e, *ej = es+j;
	for (n = 0, e = es+i; e < ej; n++)
		e += *e == '%' ? 3 : 1;
	unsigned char *s = (unsigned char*)(bdata ? newbdata(lua, n) : malloc(n)), *c;
	for (e = es+i, c = s; e < ej; )
	{
		if ( *e == '+' )
		{
			*c++ = ' ';
			e++;
		}
		else if ( *e != '%' )
		{
			*c++ = *e++;
		}
		else
		{
			if ( e + 3 > ej )
			{
				*c++ = '%';
				e += 3;
			}
			else
			{
				char c1, c2;
				if ( e[1] >= 'A' && e[1] <= 'Z' )
					c1 = e[1]-'A'+10;
				else if ( e[1] >= 'a' && e[1] <= 'z' )
					c1 = e[1]-'a'+10;
				else
					c1 = e[1]-'0';

				if ( e[2] >= 'A' && e[2] <= 'Z' )
					c2 = e[2]-'A'+10;
				else if ( e[2] >= 'a' && e[2] <= 'z' )
					c2 = e[2]-'a'+10;
				else
					c2 = e[2]-'0';

				*c++ = (char) ( ( ( c1<<4 ) | c2 ) & 0xFF );
				e += 3;
			}
		}
	}
	if ( !bdata)
		pushsl(lua, (char*)s, n), free(s);
	return 1;
}

// -1|5 bytes,first,last,bdata,mode +1 base64
static int str_enbase64(lua_State *lua)
{
	size_t n; unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	bool bdata = tobool(lua, 4);
	char S[66] = { "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=" };
	isstr(lua, 5) && memcpy(S+62, tostr(lua, 5), tolen(lua, 5) < 3 ? tolen(lua, 5) : 3);
	const char *pad = isstr(lua, 5) && tolen(lua, 5) < 3 ? NULL : S+64;
	n = i < j ? j-i : 0, n = pad ? (n+2)/3<<2 : n/3*4 + (n%3*8+5)/6;
	unsigned char *c, *cj = s+j;
	unsigned char *es = (unsigned char*)(bdata ? newbdata(lua, n) : malloc(n)), *e;
	for (c = s+i, e = es; c < cj-2; c += 3)
		*e++ = S[c[0]>>2], *e++ = S[(c[0]&3)<<4 | c[1]>>4],
		*e++ = S[(c[1]&15)<<2 | c[2]>>6], *e++ = S[c[2]&63];
	if (c == cj-1)
		*e++ = S[c[0]>>2], *e++ = S[(c[0]&3)<<4], pad && (*e++ = *pad, *e++ = *pad);
	else if (c == cj-2)
		*e++ = S[c[0]>>2], *e++ = S[(c[0]&3)<<4 | c[1]>>4],
		*e++ = S[(c[1]&15)<<2], pad && (*e++ = *pad);
	if ( !bdata)
		pushsl(lua, (char*)es, n), free(es);
	return 1;
}

// -1|5 bytes,first,last,bdata,mode +1 origin
static int str_debase64(lua_State *lua)
{
	size_t n; unsigned char *es = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	bool bdata = tobool(lua, 4);
	const char *mode = tostr(lua, 5);
	const char *pad = mode ? tolen(lua, 5) < 3 ? NULL : mode+2 : "=";
	pad && i < j && es[j-1] == *pad && j--, pad && i < j && es[j-1] == *pad && j--;
	unsigned char S[256];
	memset(S, 0, 256);
	S[mode && tolen(lua, 5) > 1 ? mode[1] : '/'] = 63;
	S[mode && tolen(lua, 5) > 0 ? mode[0] : '+'] = 62;
	for (unsigned char x = 0; x < 62; x++)
		S["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"[x]] = x;
	n = i < j ? j-i : 0, n = n/4*3 + (n%4*6+2)/8;
	unsigned char *e, *ej = es+j;
	unsigned char *s = (unsigned char*)(bdata ? newbdata(lua, n) : malloc(n)), *c;
	for (e = es+i, c = s; e < ej-3; e += 4)
		*c++ = S[e[0]]<<2 | S[e[1]]>>4, *c++ = (S[e[1]]&15)<<4 | S[e[2]]>>2,
		*c++ = (S[e[2]]&3)<<6 | S[e[3]];
	if (e == ej-1)
		*c++ = S[e[0]]<<2;
	else if (e == ej-2)
		*c++ = S[e[0]]<<2 | S[e[1]]>>4;
	else if (e == ej-3)
		*c++ = S[e[0]]<<2 | S[e[1]]>>4, *c++ = (S[e[1]]&15)<<4 | S[e[2]]>>2;
	if ( !bdata)
		pushsl(lua, (char*)s, n), free(s);
	return 1;
}

static unsigned mdFF(unsigned a, unsigned b, unsigned c, unsigned d, unsigned x, unsigned s, unsigned ac)
{
	a += ((b & c) | (~b & d)) + x + ac;
	return ((a << s) | (a >> (32-s))) + b;
}
static unsigned mdGG(unsigned a, unsigned b, unsigned c, unsigned d, unsigned x, unsigned s, unsigned ac)
{
	a += ((d & b) | (~d & c)) + x + ac;
	return ((a << s) | (a >> (32-s))) + b;
}
static unsigned mdHH(unsigned a, unsigned b, unsigned c, unsigned d, unsigned x, unsigned s, unsigned ac)
{
	a += (b ^ c ^ d) + x + ac;
	return ((a << s) | (a >> (32-s))) + b;
}
static unsigned mdII(unsigned a, unsigned b, unsigned c, unsigned d, unsigned x, unsigned s, unsigned ac)
{
	a += (c ^ (b | ~d)) + x + ac;
	return ((a << s) | (a >> (32-s))) + b;
}
static void mdRounds(unsigned x[16], unsigned re[4])
{
	unsigned a = re[0], b = re[1], c = re[2], d = re[3];

	// Round 1
	a = mdFF(a, b, c, d, x[ 0],  7, 0xD76AA478), d = mdFF(d, a, b, c, x[ 1], 12, 0xE8C7B756);
	c = mdFF(c, d, a, b, x[ 2], 17, 0x242070DB), b = mdFF(b, c, d, a, x[ 3], 22, 0xC1BDCEEE);
	a = mdFF(a, b, c, d, x[ 4],  7, 0xF57C0FAF), d = mdFF(d, a, b, c, x[ 5], 12, 0x4787C62A);
	c = mdFF(c, d, a, b, x[ 6], 17, 0xA8304613), b = mdFF(b, c, d, a, x[ 7], 22, 0xFD469501);

	a = mdFF(a, b, c, d, x[ 8],  7, 0x698098D8), d = mdFF(d, a, b, c, x[ 9], 12, 0x8B44F7AF);
	c = mdFF(c, d, a, b, x[10], 17, 0xFFFF5BB1), b = mdFF(b, c, d, a, x[11], 22, 0x895CD7BE);
	a = mdFF(a, b, c, d, x[12],  7, 0x6B901122), d = mdFF(d, a, b, c, x[13], 12, 0xFD987193);
	c = mdFF(c, d, a, b, x[14], 17, 0xA679438E), b = mdFF(b, c, d, a, x[15], 22, 0x49B40821);

	// Round 2
	a = mdGG(a, b, c, d, x[ 1],  5, 0xF61E2562), d = mdGG(d, a, b, c, x[ 6],  9, 0xC040B340);
	c = mdGG(c, d, a, b, x[11], 14, 0x265E5A51), b = mdGG(b, c, d, a, x[ 0], 20, 0xE9B6C7AA);
	a = mdGG(a, b, c, d, x[ 5],  5, 0xD62F105D), d = mdGG(d, a, b, c, x[10],  9, 0x02441453);
	c = mdGG(c, d, a, b, x[15], 14, 0xD8A1E681), b = mdGG(b, c, d, a, x[ 4], 20, 0xE7D3FBC8);

	a = mdGG(a, b, c, d, x[ 9],  5, 0x21E1CDE6), d = mdGG(d, a, b, c, x[14],  9, 0xC33707D6);
	c = mdGG(c, d, a, b, x[ 3], 14, 0xF4D50D87), b = mdGG(b, c, d, a, x[ 8], 20, 0x455A14ED);
	a = mdGG(a, b, c, d, x[13],  5, 0xA9E3E905), d = mdGG(d, a, b, c, x[ 2],  9, 0xFCEFA3F8);
	c = mdGG(c, d, a, b, x[ 7], 14, 0x676F02D9), b = mdGG(b, c, d, a, x[12], 20, 0x8D2A4C8A);

	// Round 3
	a = mdHH(a, b, c, d, x[ 5],  4, 0xFFFA3942), d = mdHH(d, a, b, c, x[ 8], 11, 0x8771F681);
	c = mdHH(c, d, a, b, x[11], 16, 0x6D9D6122), b = mdHH(b, c, d, a, x[14], 23, 0xFDE5380C);
	a = mdHH(a, b, c, d, x[ 1],  4, 0xA4BEEA44), d = mdHH(d, a, b, c, x[ 4], 11, 0x4BDECFA9);
	c = mdHH(c, d, a, b, x[ 7], 16, 0xF6BB4B60), b = mdHH(b, c, d, a, x[10], 23, 0xBEBFBC70);

	a = mdHH(a, b, c, d, x[13],  4, 0x289B7EC6), d = mdHH(d, a, b, c, x[ 0], 11, 0xEAA127FA);
	c = mdHH(c, d, a, b, x[ 3], 16, 0xD4EF3085), b = mdHH(b, c, d, a, x[ 6], 23, 0x04881D05);
	a = mdHH(a, b, c, d, x[ 9],  4, 0xD9D4D039), d = mdHH(d, a, b, c, x[12], 11, 0xE6DB99E5);
	c = mdHH(c, d, a, b, x[15], 16, 0x1FA27CF8), b = mdHH(b, c, d, a, x[ 2], 23, 0xC4AC5665);

	// Round 4
	a = mdII(a, b, c, d, x[ 0],  6, 0xF4292244), d = mdII(d, a, b, c, x[ 7], 10, 0x432AFF97);
	c = mdII(c, d, a, b, x[14], 15, 0xAB9423A7), b = mdII(b, c, d, a, x[ 5], 21, 0xFC93A039);
	a = mdII(a, b, c, d, x[12],  6, 0x655B59C3), d = mdII(d, a, b, c, x[ 3], 10, 0x8F0CCC92);
	c = mdII(c, d, a, b, x[10], 15, 0xFFEFF47D), b = mdII(b, c, d, a, x[ 1], 21, 0x85845DD1);

	a = mdII(a, b, c, d, x[ 8],  6, 0x6FA87E4F), d = mdII(d, a, b, c, x[15], 10, 0xFE2CE6E0);
	c = mdII(c, d, a, b, x[ 6], 15, 0xA3014314), b = mdII(b, c, d, a, x[13], 21, 0x4E0811A1);
	a = mdII(a, b, c, d, x[ 4],  6, 0xF7537E82), d = mdII(d, a, b, c, x[11], 10, 0xBD3AF235);
	c = mdII(c, d, a, b, x[ 2], 15, 0x2AD7D2BB), b = mdII(b, c, d, a, x[ 9], 21, 0xEB86D391);

	re[0] += a, re[1] += b, re[2] += c, re[3] += d;
}
// all in little endian
static void mdBytes(unsigned char pre[64], unsigned char bytes[], size_t first, size_t last1, unsigned char Re[16])
{
	unsigned *re = (unsigned*)Re;
	re[0] = 0x67452301, re[1] = 0xEFCDAB89, re[2] = 0x98BADCFE, re[3] = 0x10325476;
	unsigned char x[64];
	size_t d;
	if (pre)
		mdRounds((unsigned*)pre, re);
	for (d = first; d+63 < last1; d += 64)
		mdRounds((unsigned*)(bytes + d), re);
	memcpy(x, bytes + d, last1 - d), d = last1 - d;
	memset(x + d, 0, 4 - d%4), x[d] = 0x80, d += 4 - d%4; // append bit 1
	if (d > 56)
		memset(x + d, 0, 64 - d), mdRounds((unsigned*)x, re), d = 0;
	if (d < 56)
		memset(x + d, 0, 56 - d);
	writeL(x + 56, (long long)(last1 - first + (pre ? 64 : 0)) << 3);
	mdRounds((unsigned*)x, re);
}
static void mdHex(unsigned char bytes[], size_t first, size_t last1, char hex[32])
{
	unsigned char re[16];
	mdBytes(NULL, bytes, first, last1, re);
	for (int d = 0; d < 16; d++)
		hex[d+d] = "0123456789abcdef"[re[d]>>4&15],
		hex[d+d+1] = "0123456789abcdef"[re[d]&15];
}
// -1|4 bytes,first,last,bdata +1 hex
static int str_md5(lua_State *lua)
{
	size_t n; unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	i > j && (j = i);
	if (tobool(lua, 4))
	{
		unsigned char *re = (unsigned char*)newbdata(lua, 16);
		mdBytes(NULL, s, i, j, re);
		return 1;
	}
	char hex[32];
	mdHex(s, i, i<=j ? j : i, hex);
	return pushsl(lua, hex, 32), 1;
}

// convert little endian to big endian
static void shaRounds(unsigned char x[64], unsigned re[5])
{
	unsigned char bx[320] = {
		x[ 3],x[ 2],x[ 1],x[ 0], x[ 7],x[ 6],x[ 5],x[ 4], x[11],x[10],x[ 9],x[ 8], x[15],x[14],x[13],x[12],
		x[19],x[18],x[17],x[16], x[23],x[22],x[21],x[20], x[27],x[26],x[25],x[24], x[31],x[30],x[29],x[28],
		x[35],x[34],x[33],x[32], x[39],x[38],x[37],x[36], x[43],x[42],x[41],x[40], x[47],x[46],x[45],x[44],
		x[51],x[50],x[49],x[48], x[55],x[54],x[53],x[52], x[59],x[58],x[57],x[56], x[63],x[62],x[61],x[60],
	};
	unsigned *w = (unsigned *)bx;
	for (unsigned W, i = 16; i < 80; i++)
		W = w[i-3] ^ w[i-8]^ w[i-14] ^ w[i-16],
		w[i] = (W << 1) | (W >> 31);
	unsigned a = re[0], b = re[1], c = re[2], d = re[3], e = re[4], f, t;
	for (int i = 0; i < 20; i++)
		f = (b & c) | (~b & d),
		t = ((a << 5) | (a >> 27)) + f + e + 0x5A827999 + w[i],
		e = d, d = c, c = (b << 30) | (b >> 2), b = a, a = t;
	for (int i = 20; i < 40; i++)
		f = b ^ c ^ d,
		t = ((a << 5) | (a >> 27)) + f + e + 0x6ED9EBA1 + w[i],
		e = d, d = c, c = (b << 30) | (b >> 2), b = a, a = t;
	for (int i = 40; i < 60; i++)
		f = (b & c) | (d & (b | c)),
		t = ((a << 5) | (a >> 27)) + f + e + 0x8F1BBCDC + w[i],
		e = d, d = c, c = (b << 30) | (b >> 2), b = a, a = t;
	for (int i = 60; i < 80; i++)
		f = b ^ c ^ d,
		t = ((a << 5) | (a >> 27)) + f + e + 0xCA62C1D6 + w[i],
		e = d, d = c, c = (b << 30) | (b >> 2), b = a, a = t;
	re[0] += a, re[1] += b, re[2] += c, re[3] += d, re[4] += e;
}
static void shaBytes(unsigned char pre[64], unsigned char bytes[], size_t first, size_t last1, unsigned char Re[20])
{
	unsigned re[5];
	re[0] = 0x67452301, re[1] = 0xEFCDAB89, re[2] = 0x98BADCFE, re[3] = 0x10325476, re[4] = 0xC3D2E1F0;
	unsigned char x[64];
	size_t d;
	if (pre)
		shaRounds(pre, re);
	for (d = first; d+63 < last1; d += 64)
		shaRounds(bytes + d, re);
	memcpy(x, bytes + d, last1 - d), d = last1 - d;
	memset(x + d, 0, 4 - d%4), x[d] = 0x80, d += 4 - d%4; // append bit 1
	if (d > 56)
		memset(x + d, 0, 64 - d), shaRounds(x, re), d = 0;
	if (d < 56)
		memset(x + d, 0, 56 - d);
	writeL(x + 56, (long long)(last1 - first + (pre ? 64 : 0)) << 3);
	unsigned char be[8] = { x[63], x[62], x[61], x[60], x[59], x[58], x[57], x[56] };	
	writeL(x + 56, readL(be));
	shaRounds(x, re);
	unsigned char *l = (unsigned char *)re;
	unsigned char L[20] = { l[ 3],l[ 2],l[ 1],l[ 0], l[ 7],l[ 6],l[ 5],l[ 4], l[11],l[10],l[ 9],l[ 8],
		l[15],l[14],l[13],l[12], l[19],l[18],l[17],l[16] };
	memcpy(Re, L, 20);
}
static void shaHex(unsigned char bytes[], size_t first, size_t last1, char hex[40])
{
	unsigned char re[20];
	shaBytes(NULL, bytes, first, last1, re);
	for (int d = 0; d < 20; d++)
		hex[d+d] = "0123456789abcdef"[re[d]>>4&15],
		hex[d+d+1] = "0123456789abcdef"[re[d]&15];
}
// -1|4 bytes,first,last,bdata +1 hex
static int str_sha1(lua_State *lua)
{
	size_t n; unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t i = indexn0(luaL_optint(lua, 2, 1), n), j = indexn(luaL_optint(lua, 3, -1), n);
	i > j && (j = i);
	if (tobool(lua, 4))
	{
		unsigned char *re = (unsigned char*)newbdata(lua, 20);
		shaBytes(NULL, s, i, j, re);
		return 1;
	}
	char hex[40];
	shaHex(s, i, i<=j ? j : i, hex);
	return pushsl(lua, hex, 40), 1;
}

// -2|5 dataBytes,keyBytes,first,last,bdata +1 hex
static int str_hmacmd5(lua_State *lua)
{
	size_t n; unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t kn; unsigned char *ks = (unsigned char*)tobytes(lua, 2, &kn, "#2");
	size_t i = indexn0(luaL_optint(lua, 3, 1), n), j = indexn(luaL_optint(lua, 4, -1), n);
	unsigned char key[64], keyi[64], rei[16];
	if (kn > 64)
		mdBytes(NULL, ks, 0, kn, key), memset(key + 16, 0, 48);
	else
		memcpy(key, ks, kn), memset(key + kn, 0, 64 - kn);
	for (int d = 0; d < 64; d++)
		keyi[d] = key[d] ^ 0x36;
	for (int d = 0; d < 64; d++)
		key[d] ^= 0x5c;
	mdBytes(keyi, s, i, i<=j ? j : i, rei);
	if (tobool(lua, 5))
	{
		unsigned char *re = (unsigned char*)newbdata(lua, 16);
		mdBytes(key, rei, 0, 16, re);
		return 1;
	}
	unsigned char re[16]; char hex[40];
	mdBytes(key, rei, 0, 16, re);
	for (int d = 0; d < 16; d++)
		hex[d+d] = "0123456789abcdef"[re[d]>>4&15],
		hex[d+d+1] = "0123456789abcdef"[re[d]&15];
	return pushsl(lua, hex, 32), 1;
}
// -2|5 dataBytes,keyBytes,first,last,bdata +1 hex
static int str_hmacsha1(lua_State *lua)
{
	size_t n; unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t kn; unsigned char *ks = (unsigned char*)tobytes(lua, 2, &kn, "#2");
	size_t i = indexn0(luaL_optint(lua, 3, 1), n), j = indexn(luaL_optint(lua, 4, -1), n);
	unsigned char key[64], keyi[64], rei[20];
	if (kn > 64)
		shaBytes(NULL, ks, 0, kn, key), memset(key + 20, 0, 44);
	else
		memcpy(key, ks, kn), memset(key + kn, 0, 64 - kn);
	for (int d = 0; d < 64; d++)
		keyi[d] = key[d] ^ 0x36;
	for (int d = 0; d < 64; d++)
		key[d] ^= 0x5c;
	shaBytes(keyi, s, i, i<=j ? j : i, rei);
	if (tobool(lua, 5))
	{
		unsigned char *re = (unsigned char*)newbdata(lua, 20);
		shaBytes(key, rei, 0, 20, re);
		return 1;
	}
	unsigned char re[20]; char hex[40];
	shaBytes(key, rei, 0, 20, re);
	for (int d = 0; d < 20; d++)
		hex[d+d] = "0123456789abcdef"[re[d]>>4&15],
		hex[d+d+1] = "0123456789abcdef"[re[d]&15];
	return pushsl(lua, hex, 40), 1;
}

char* ucs2toutf8( const wchar_t* str, unsigned int number, unsigned int* cc )
{
	unsigned int wi = 0, ci = 0;
	for ( const wchar_t* w = str; *w != 0 && wi < number; w ++, wi ++ )
	{
		int wt = ( *w & (wchar_t) 0xFFFFFF80 ) == 0 ? 1 : ( *w & (wchar_t) 0xFFFFF800 ) == 0 ? 2 : 3;
		ci += wt;
	}

	char* buffer = new char[ci + 1];

	const wchar_t* w = str;
	char* c = buffer;
	for ( unsigned i = 0; i < wi; i ++, w ++ )
	{
		if ( ( *w & (wchar_t) 0xFFFFFF80 ) == 0 )
		{
			*c++ = (char)*w;
		}
		else if ( ( *w & (wchar_t) 0xFFFFF800 ) == 0 )
		{
			*c++ = (char)( ( *w >> 6 & 0x1F ) | 0xC0 );
			*c++ = (char)( ( *w & 0x3F ) | 0x80 );
		}
		else
		{
			*c++ = (char)( ( *w >> 12 & 0x0F ) | 0xE0 );
			*c++ = (char)( ( *w >> 6 & 0x3F ) | 0x80 );
			*c++ = (char)( ( *w & 0x3F ) | 0x80 );
		}
	}

	buffer[ci] = 0;
	if ( cc != NULL )
		*cc = ci;
	return buffer;
}

static int str_fromhexucs(lua_State *lua)
{
	size_t len; char *s = (char*) tobytes( lua, 1, &len, "#1" );
	wchar_t* res = (wchar_t*) malloc( len * sizeof( wchar_t ) );
	memset(res, 0, len);
	int n = 0;

	for ( unsigned i = 0; i < len; i ++ )
	{
		if ( s[i] != '\\' )
		{
			res[n++] = s[i];
			continue;
		}

		if ( i + 5 > len - 1 )
			break;

		unsigned int c[4] = {0};
		for ( int j = 0; j < 4; j ++ )
		{
			c[j] = s[i+2+j];
			if ( c[j] >= 'A' && c[j] <= 'Z' )
				c[j] = c[j]-'A'+10;
			else if ( c[j] >= 'a' && c[j] <= 'z' )
				c[j] = c[j]-'a'+10;
			else
				c[j] = c[j]-'0';
		}

		res[n++] = c[0]<<12 | c[1]<<8 | c[2]<<4 | c[3];
		i += 5;
	}

	res[n] = 0;

	unsigned int ci = 0;
	char* ret = ucs2toutf8( res, n, &ci );
	pushsl(lua, ret, ci);
	free( res );
	free( ret );
	return 1;
}

static int str_xor(lua_State *lua)
{
	size_t n1; unsigned char *s1 = (unsigned char*)tobytes(lua, 1, &n1, "#1");
	size_t n2; unsigned char *s2 = (unsigned char*)tobytes(lua, 2, &n2, "#2");

	char* res = (char*) malloc( n1 );
	for ( unsigned int i = 0; i < n1; i ++ )
		res[i] = s1[i] ^ s2[ i % n2 ];

	return pushsl(lua, res, n1), 1;
}

static void string(lua_State *lua, int M, int G)
{
	int L = gettop(lua)+1;
	rawgetn(lua, G, "string"); // L string
	pushc(lua, str_tostr), rawsetn(lua, L, "tostr");
	pushc(lua, str_tobytes), rawsetn(lua, L, "tobytes");
	pushc(lua, str_len), rawsetn(lua, L, "len");
	pushc(lua, str_ulen), rawsetn(lua, L, "ulen");
	pushc(lua, str_byte), rawsetn(lua, L, "byte");
	pushc(lua, str_to16l), rawsetn(lua, L, "to16l");
	pushc(lua, str_to16b), rawsetn(lua, L, "to16b");
	pushc(lua, str_to32l), rawsetn(lua, L, "to32l");
	pushc(lua, str_to32b), rawsetn(lua, L, "to32b");
	pushc(lua, str_to64l), rawsetn(lua, L, "to64l");
	pushc(lua, str_to64b), rawsetn(lua, L, "to64b");
	pushc(lua, str_toDl), rawsetn(lua, L, "toDl");
	pushc(lua, str_toDb), rawsetn(lua, L, "toDb");
	pushc(lua, str_from16l), rawsetn(lua, L, "from16l");
	pushc(lua, str_from16b), rawsetn(lua, L, "from16b");
	pushc(lua, str_from32l), rawsetn(lua, L, "from32l");
	pushc(lua, str_from32b), rawsetn(lua, L, "from32b");
	pushc(lua, str_from64l), rawsetn(lua, L, "from64l");
	pushc(lua, str_from64b), rawsetn(lua, L, "from64b");
	pushc(lua, str_fromDl), rawsetn(lua, L, "fromDl");
	pushc(lua, str_fromDb), rawsetn(lua, L, "fromDb");
	pushc(lua, str_lead), rawsetn(lua, L, "lead");
	pushc(lua, str_tail), rawsetn(lua, L, "tail");
	pushc(lua, str_tail), rawsetn(lua, L, "tail");
	pushc(lua, str_ucs), rawsetn(lua, L, "ucs");
	pushc(lua, str_utf), rawsetn(lua, L, "utf");
	pushc(lua, str_enurl), rawsetn(lua, L, "enurl");
	pushc(lua, str_deurl), rawsetn(lua, L, "deurl");
	pushc(lua, str_enurl2), rawsetn(lua, L, "enurl2");
	pushc(lua, str_deurl2), rawsetn(lua, L, "deurl2");
	pushc(lua, str_enbase64), rawsetn(lua, L, "enbase64");
	pushc(lua, str_debase64), rawsetn(lua, L, "debase64");
	pushc(lua, str_md5), rawsetn(lua, L, "md5");
	pushc(lua, str_sha1), rawsetn(lua, L, "sha1");
	pushc(lua, str_hmacmd5), rawsetn(lua, L, "hmacmd5");
	pushc(lua, str_hmacsha1), rawsetn(lua, L, "hmacsha1");
	pushc(lua, str_fromhexucs), rawsetn(lua, L, "fromhexucs");
	pushc(lua, str_xor), rawsetn(lua, L, "xor");


	pushc(lua, str_rsapuben), rawsetn(lua, L, "rsapubkeyen");
	pushc(lua, str_rsapubde), rawsetn(lua, L, "rsapubkeyde");
	pushc(lua, str_rsaprien), rawsetn(lua, L, "rsaprikeyen");
	pushc(lua, str_rsapride), rawsetn(lua, L, "rsaprikeyde");
	pushc(lua, str_rsaverify), rawsetn(lua, L, "rsaverify");
	pushc(lua, str_rsasign), rawsetn(lua, L, "rsasign");
	pushc(lua, str_3desen), rawsetn(lua, L, "des3en");
	pushc(lua, str_3desde), rawsetn(lua, L, "des3de");


	pushs(lua, ""), getmeta(lua, -1), pushc(lua, str_len), rawsetn(lua, -2, "__len"),
		rawseti(lua, M, M_string); // meta[M_string] = meta('')
}
