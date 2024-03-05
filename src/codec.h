/////////////////////////////// enc //////////////////////////////

// return buf, at least x bytes available
static char *enc_buf(lua_State *lua, int ref, char **buf, size_t x)
{
	char *B = *buf, *b0 = (char*)toudata(lua, ref-1);
	size_t n0 = tolen(lua, ref-1), x0 = B - b0;
	size_t n = x0 + x;
	if (n < n0)
		return *buf = B + x, B;
	n = n <= n0 + n0 ? n0 + n0 : n + 1024;
	if (n < x0 || n < n0)
		error(lua, "out of memory");
	char *b = newbdata(lua, n);
	memcpy(b, b0, n0), lua_replace(lua, ref-1);
	return *buf = b + x0 + x, b + x0;
}

static bool enc_ref(lua_State *lua, int d, int ref, int *refn, char **buf)
{
	#ifdef _USE_LUA51
	push(lua, d), rawget(lua, ref);
	#else
	rawgetk(lua, ref, d);
	#endif

	size_t r = popint(lua);
	if (r == 0)
	{
		#ifdef _USE_LUA51
		return pushi(lua, d), pushi(lua, ++*refn), rawset(lua, ref), true;
		#else
		return pushi(lua, ++*refn), rawsetk(lua, ref, d), true;
		#endif
	}

	if (r < 64)
		*enc_buf(lua, ref, buf, 1) = 3<<6|(char)r;
	else if (r == (unsigned short)r)
		*enc_buf(lua, ref, buf, 3) = 3<<2|1, *(short*)(*buf-2) = (short)r;
	else
		*enc_buf(lua, ref, buf, 5) = 3<<2|2, *(int*)(*buf-4) = (int)r;
	return false;
}

static void enc_data(lua_State *lua, int d, int ref, int *refn, char **buf)
{
	if (d - ref > 200)
	{
		for (pushz(lua); lua_next(lua, ref); )
			pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

		error(lua, "data path too long to encode");
	}
	int t = totype(lua, d);
	if (t == LUA_TNUMBER)
	{
		double V = tonum(lua, d); long long v = (long long)V;
		if (V != V)
		{
			for (pushz(lua); lua_next(lua, ref); )
				pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

			error(lua, "can't encode NaN");
		}
		if (v != v<<10>>10)
		{
			for (pushz(lua); lua_next(lua, ref); )
				pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

			error(lua, "number %f out of signed 54bit range", V);
		}
		if (v != V)
			*enc_buf(lua, ref, buf, 9) = 1<<2, writeD(*buf-8, V);
		else if (v == v<<58>>58)
			*enc_buf(lua, ref, buf, 1) = 1<<6|((char)v&63);
		else if (v == (short)v)
			*enc_buf(lua, ref, buf, 3) = 1<<2|1, *(short*)(*buf-2) = (short)v;
		else if (v == (int)v)
			*enc_buf(lua, ref, buf, 5) = 1<<2|2, *(int*)(*buf-4) = (int)v;
		else
			*enc_buf(lua, ref, buf, 9) = 1<<2|3, writeL(*buf-8, v);
	}
	else if (t == LUA_TSTRING)
	{
		size_t n;
		unsigned char *s = (unsigned char*)lua_tolstring(lua, d, &n);
		if (n > 1<<20)
		{
			for (pushz(lua); lua_next(lua, ref); )
				pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

			error(lua, "string too long");
		}
//		else if (strlen((char*)s) != n)
//			error(lua, "string can't contain \\0");
		else if (enc_ref(lua, d, ref, refn, buf))
		{
			if (n < 64)
				*enc_buf(lua, ref, buf, 1) = 2<<6|(char)n;
			else if (n == (unsigned short)n)
				*enc_buf(lua, ref, buf, 3) = 2<<2|1, *(short*)(*buf-2) = (short)n;
			else
				*enc_buf(lua, ref, buf, 5) = 2<<2|2, *(int*)(*buf-4) = (int)n;
			memcpy(enc_buf(lua, ref, buf, n), s, n);
		}
	}
	else if (t == LUA_TUSERDATA)
	{
		size_t n;
		unsigned char *s = (unsigned char*)tobytes(lua, d, &n, NULL);
		if ( !s)
		{
			for (pushz(lua); lua_next(lua, ref); )
				pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

			error(lua, "can't encode userdata");
		}
		if (n > 1<<20)
		{
			for (pushz(lua); lua_next(lua, ref); )
				pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

			error(lua, "bytes too long");
		}
		else if (enc_ref(lua, d, ref, refn, buf))
		{
			if (n == (unsigned short)n)
				*enc_buf(lua, ref, buf, 3) = 6<<2|1, *(short*)(*buf-2) = (short)n;
			else
				*enc_buf(lua, ref, buf, 5) = 6<<2|2, *(int*)(*buf-4) = (int)n;
			memcpy(enc_buf(lua, ref, buf, n), s, n);
		}
	}
	else if (t == LUA_TTABLE)
	{
		if (enc_ref(lua, d, ref, refn, buf))
		{
			char ed = isnil(lua, ref+1) ? 0 : 5<<2; // encoded
			int L = gettop(lua)+1;
			size_t n = tolen(lua, d);
			if (n < 256)
				*enc_buf(lua, ref, buf, 2) = 4<<2|ed, *(*buf-1) = (char)n;
			else if (n == (unsigned short)n)
				*enc_buf(lua, ref, buf, 3) = 4<<2|ed|1, *(short*)(*buf-2) = (short)n;
			else
				*enc_buf(lua, ref, buf, 5) = 4<<2|ed|2, *(int*)(*buf-4) = (int)n;
			for (unsigned int i = 1; i <= n; i++)
				rawgeti(lua, d, i), // L data[i]
				enc_data(lua, L, ref, refn, buf), pop(lua, 1);
			for (pushz(lua); lua_next(lua, d); pop(lua, 1)) // L key L+1 data[key]
			{
				long long v;
				if (isnum(lua, -2) && (v = mustlong(lua, -2)) >= 1 && (size_t) v <= n)
					continue;
				enc_data(lua, L, ref, refn, buf);
				enc_data(lua, L+1, ref, refn, buf);
			}
/*			if (getmeta(lua, d) && (rawgetk(lua, L, "__index"),
				!popif(lua, totype(lua, -1) != LUA_TTABLE, 2))) // L+1 __index
			{
				for (pushz(lua); lua_next(lua, L+1); pop(lua, 1)) // L+2 key L+3 __index[key]
				{
					long long v;
					if (isnum(lua, -2) && (v = mustlong(lua, -2)) >= 1 && v <= n)
						continue;
					enc_data(lua, L+2, ref, refn, buf, true); // bugous when ignored
					enc_data(lua, L+3, ref, refn, buf, true); // bugous when ignored
				}
				pop(lua, 2);
			}
*/			*enc_buf(lua, ref, buf, 1) = 0;
			if (ed)
			{
				int LL = gettop(lua);	
				push(lua, ref+1), push(lua, d), call(lua, 1, LUA_MULTRET); // L...
				for (; !lua_isnoneornil(lua, L); L++)
					enc_data(lua, L, ref, refn, buf);
				settop(lua, LL);
				*enc_buf(lua, ref, buf, 1) = 0;
			}
		}
	}
	else if (t == LUA_TBOOLEAN)
	{
		*enc_buf(lua, ref, buf, 1) = tobool(lua, d) ? 1 : 2;
	}
	else if (t == LUA_TNIL)
	{
		*enc_buf(lua, ref, buf, 1) = 0;
	}
	else // if ( !meta)
	{
		for (pushz(lua); lua_next(lua, ref); )
			pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

		error(lua, "can't encode %s", tonamet(lua, t));
	}
}

static int encode_base(lua_State *lua, char* buffer, unsigned int len)
{
	int L = gettop(lua)+1;
	char *buf = buffer ? (pushaddr(lua, buffer), buffer) : newbdata(lua, len>0 ? len : 16384); // L buf

	#ifdef _USE_LUA51
	char *head = buf;
	#endif

//	newtablen(lua, 0, 100);
	getmetai(lua, global, M_encoderef); // L+1 ref
	for (pushz(lua); lua_next(lua, -2); )
		pop(lua, 1), pushz(lua), rawsetk(lua, -3, -2);

	int refn = 0;
	getmetai(lua, global, M_encoded); // L+2 encoded
	for (int d = 1; d < L; d++)
		enc_data(lua, d, L+1, &refn, &buf);

	#ifdef _USE_LUA51
	*(unsigned int*)(head-4) = buf-head;
	#else
	lua_userdatalen(toudata(lua, L), buf-(char*)toudata(lua, L));
	#endif

	for (pushz(lua); lua_next(lua, L+1); )
		pop(lua, 1), pushz(lua), rawsetk(lua, L+1, -2);

	return push(lua, L), 1;
}

// -1|* data... +1 bdata
static int encode(lua_State *lua)
{
	if ( lua_isnone( lua, 1 ) )
		return error( lua, "argument #1 missing" );

	return encode_base( lua, NULL, 0 );
}

static int encode_len(lua_State *lua)
{
	if ( totype( lua, 1 ) != LUA_TNUMBER )
		return error( lua, "argument #1 length expected" );

	if ( lua_isnone( lua, 2 ) )
		return newbdata( lua, touint( lua, 1 ) ), 1;

	return encode_base( lua, NULL, touint( lua, 1 ) );
}

static int encode_buf(lua_State *lua)
{
	if ( lua_isnone( lua, 1 ) )
		return error( lua, "argument #1 buffer expected" );
	if ( lua_isnone( lua, 2 ) )
		return error( lua, "argument #2 missing" );

	char* buffer = (char*) toudata( lua, 1 );
	lua_remove( lua, 1 );
	return encode_base( lua, buffer, 0 );
}

/////////////////////////////// dec //////////////////////////////

inline static size_t dec_buf(lua_State *lua, size_t *X, size_t N, size_t x)
{
	size_t m = *X;
	if (x > N - m)
		error(lua, "no enough data");
	*X = m + x;
	return m;
}

static int decode_buf(lua_State *lua)
{
	if (!lua_istable(lua, 1))
		error(lua, "argument #1 table expected");

	getmeta(lua, global);
	rawsetiv(lua, -1, M_decodebuff, 1);

	for (pushz(lua); lua_next(lua, 1); )
		pop(lua, 1), pushz(lua), rawsetk(lua, 1, -2);

	return 0;
}

static void dec_data(lua_State *lua, const unsigned char *buf, size_t *X, size_t N,
	int ref, int last)
{
	if (gettop(lua) - last > 200)
	{
		for (pushz(lua); lua_next(lua, ref); )
			pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

		error(lua, "data path too long to decode");
	}
	unsigned char v = buf[dec_buf(lua, X, N, 1)];
	unsigned int n; bool ed = false;
	if (v>>6 == 1 || v>>2 == 1)
		v>>6 == 1 ? pushi(lua, (char)v<<26>>26) :
		v == 1<<2 ? pushn(lua, readD(buf+dec_buf(lua, X, N, 8))) :
		v == (1<<2|1) ? pushi(lua, *(short*)(buf+dec_buf(lua, X, N, 2))) :
		v == (1<<2|2) ? pushi(lua, *(int*)(buf+dec_buf(lua, X, N, 4))) :
			pushn(lua, (double)readL(buf+dec_buf(lua, X, N, 8)));
	else if (v>>6 == 2 || v>>2 == 2)
	{
		n = v>>6 == 2 ? (v-(2<<6)) :
			v == (2<<2|1) ? *(unsigned short*)(buf+dec_buf(lua, X, N, 2)) :
			*(unsigned int*)(buf+dec_buf(lua, X, N, 4));
		pushsl(lua, (char*)buf+dec_buf(lua, X, N, n), n);

		#ifdef _USE_LUA51
		push(lua, -1), rawseti(lua, ref, tolen(lua, ref)+1); // ref[#ref+1] = str
		#else
		rawsetiv(lua, ref, (int)tolen(lua, ref)+1, -1); // ref[#ref+1] = str
		#endif
	}
	else if (v>>2 == 6)
	{
		n = v == (6<<2|1) ? *(unsigned short*)(buf+dec_buf(lua, X, N, 2)) :
			*(unsigned int*)(buf+dec_buf(lua, X, N, 4));
		memcpy(newbdata(lua, n), buf+dec_buf(lua, X, N, n), n);

		#ifdef _USE_LUA51
		push(lua, -1), rawseti(lua, ref, tolen(lua, ref)+1); // ref[#ref+1] = bdata
		#else
		rawsetiv(lua, ref, (int)tolen(lua, ref)+1, -1); // ref[#ref+1] = bdata
		#endif
	}
	else if (v>>6 == 3 || v>>2 == 3)
	{
		n = v>>6 == 3 ? (v-(3<<6)) :
			v == (3<<2|1) ? *(unsigned short*)(buf+dec_buf(lua, X, N, 2)) :
			*(unsigned int*)(buf+dec_buf(lua, X, N, 4));
		rawgeti(lua, ref, n);
		if (isnil(lua, -1))
		{
			for (pushz(lua); lua_next(lua, ref); )
				pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

			error(lua, "decode invalid reference");
		}
	}
	else if (v>>2 == 4 || (ed = v>>2 == 5))
	{
		n = (v&3) == 0 ? buf[dec_buf(lua, X, N, 1)] :
			(v&3) == 1 ? *(unsigned short*)(buf+dec_buf(lua, X, N, 2)) :
			*(unsigned int*)(buf+dec_buf(lua, X, N, 4));
		if (n > N - *X)
		{
			for (pushz(lua); lua_next(lua, ref); )
				pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

			error(lua, "no enough data");
		}

		getmetai(lua, global, M_decodebuff);
		if (isnil(lua, -1))
		{
			pop(lua, 1);
			newtablen(lua, n, 30);
		}
		else
		{
			getmeta(lua, global);
			pushz(lua), rawseti(lua, -2, M_decodebuff);
			pop(lua, 1);
		}

		#ifdef _USE_LUA51
		push(lua, -1), rawseti(lua, ref, tolen(lua, ref)+1); // ref[#ref+1] = table
		#else
		rawsetiv(lua, ref, (int)tolen(lua, ref)+1, -1); // ref[#ref+1] = table
		#endif

		for (unsigned int i = 0; i++ < n; )
			dec_data(lua, buf, X, N, ref, last), rawseti(lua, -2, i);
		for (;;)
		{
			dec_data(lua, buf, X, N, ref, last);
			if (popifz(lua, 1))
				break;
			dec_data(lua, buf, X, N, ref, last), rawset(lua, -3);
		}
		if (ed)
		{
			int L = gettop(lua);
			push(lua, ref+1), push(lua, L); // L+1 decoded L+2 table 
			do
				dec_data(lua, buf, X, N, ref, last); // L+3 ...
			while ( !popifz(lua, 1));
			if ( !isnil(lua, ref+1))
				call(lua, gettop(lua)-L-1, 0);
			settop(lua, L);	
		}
	}
	else if (v == 1 || v == 2)
	{
		return pushb(lua, v&1);
	}
	else if (v == 0)
	{
		return pushz(lua);
	}
	else
	{
		for (pushz(lua); lua_next(lua, ref); )
			pop(lua, 1), pushz(lua), rawsetk(lua, ref, -2);

		return (void)error(lua, "can't decode data %d at %d", v, *X-1);
	}
}

// -1|3 bytes[,first,last] +* data1,data2...
static int decode(lua_State *lua)
{
	size_t n; const unsigned char *s = (unsigned char*)tobytes(lua, 1, &n, "#1");
	size_t x = indexn0(luaL_optint(lua, 2, 1), n);
	n = indexn(luaL_optint(lua, 3, -1), n);
//	newtablen(lua, 10, 0); // L-1 ref
	getmetai(lua, global, M_decoderef); // L-1 ref
	for (pushz(lua); lua_next(lua, -2); )
		pop(lua, 1), pushz(lua), rawsetk(lua, -3, -2);

	getmetai(lua, global, M_decoded); // L decoded
	int L = gettop(lua), last = L;
	while (x < n)
		dec_data(lua, s, &x, n, L-1, last++);

	for (pushz(lua); lua_next(lua, L-1); )
		pop(lua, 1), pushz(lua), rawsetk(lua, L-1, -2);

	return gettop(lua)-L;
}

// -2 encoded,decoded +0
static int codec_codec(lua_State *lua)
{
	checkArgableZ(lua, 1, "#1", LUA_TFUNCTION);
	checkArgableZ(lua, 2, "#2", LUA_TFUNCTION);

	#ifdef _USE_LUA51

	lua_CFunction func1 = tocfunc(lua, 1);
	lua_CFunction func2 = tocfunc(lua, 2);
	settop(lua, 0);
	getmeta(lua, global);
	pushc(lua, func1), rawseti(lua, -2, M_encoded);
	pushc(lua, func2), rawseti(lua, -2, M_decoded);

	#else

	getmeta(lua, global);
	rawsetiv(lua, -1, M_encoded, 1), rawsetiv(lua, -1, M_decoded, 2);

	#endif

	return 0;
}

static void codec(lua_State *lua, int M, int G)
{
	pushc(lua, encode), rawsetn(lua, G, "_encode");
	pushc(lua, encode_len), rawsetn(lua, G, "_encodelen");
	pushc(lua, encode_buf), rawsetn(lua, G, "_encodebuf");
	pushc(lua, decode), rawsetn(lua, G, "_decode");
	pushc(lua, decode_buf), rawsetn(lua, G, "_decodebuf");
	pushc(lua, codec_codec), rawsetn(lua, G, "_codec");

	getmeta(lua, global);
	newtablen(lua, 1024, 0), rawseti(lua, -2, M_decoderef);
	newtablen(lua, 0, 1024), rawseti(lua, -2, M_encoderef);
}
