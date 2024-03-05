#ifdef __cplusplus
extern "C" {
#endif

int deflateInit2_(void *z, int level, int method, int winbit, int mem, int strategy,
	const char *version, size_t sizez);
int deflate(void *z, int flush);
int deflateEnd(void *z);
int inflateInit2_(void *z, int winbit, const char *version, size_t sizez);
int inflate(void *z, int flush);
int inflateEnd(void *z);
int crc32(int crc, const char *s, size_t n);

typedef struct z_stream_s
{
	const char		*next_in;  /* next input byte */
	unsigned int	avail_in;  /* number of bytes available at next_in */
	unsigned long	total_in;  /* total nb of input bytes read so far */

	unsigned char	*next_out; /* next output byte should be put there */
	unsigned int	avail_out; /* remaining free space at next_out */
	unsigned long	total_out; /* total nb of bytes output so far */

	char			*msg;      /* last error message, NULL if no error */
	void			*state; /* not visible by applications */

	void			*zalloc;  /* used to allocate the internal state */
	void			*zfree;   /* used to free the internal state */
	void			*opaque;  /* private data object passed to zalloc and zfree */

	int				data_type;  /* best guess about the data type: binary or text */
	unsigned long	adler;      /* adler32 value of the uncompressed data */
	unsigned long	reserved;   /* reserved for future use */
} z_stream;

#define Z_DEFLATED   8

#ifdef WIN32
#define OS_CODE  0x0b
#endif

#if defined(MACOS) || defined(TARGET_OS_MAC)
#define OS_CODE  0x07
#endif

#ifndef OS_CODE
#define OS_CODE  0x03  /* assume Unix */
#endif

// -1|2 str|data,zlibMode +1 data
static int zip_deflate(lua_State *lua)
{
	size_t n; const char *s = tobytes( lua, 1, &n, "#1" );
	int mode = tobool( lua, 2 );
	int gz = tobool( lua, 3 );
	int pad = ( !mode && gz ) ? 18 : 0;
	size_t on = n + (n>>12) + (n>>14) + (n>>25) + 13; // compressBound
	pushz(lua);
	unsigned char *os = (unsigned char *) newbdata( lua, on + pad );
	z_stream z	= { 0,0,0, 0,0,0, 0,0, 0,0,0, 0,0,0 };
	z.next_in	= s;
	z.avail_in	= (unsigned int)n;
	z.next_out	= &os[ ( !mode && gz ) ? 10 : 0 ];
	z.avail_out	= (unsigned int)on;
	int err = deflateInit2_( &z, -1, 8, mode ? 15 : -15, 8, 0, "1.2.3", sizeof( z ) );
	if ( err || ( err = deflate( &z, 4 ), deflateEnd( &z ), err ) != 1 ) // Z_FINISH Z_STREAM_END
		error( lua, "error %d", err );

	if ( !mode && gz )
	{
		os[0] = 0x1F;
		os[1] = 0x8B;
		os[2] = Z_DEFLATED;
		os[3] = 0;
		os[4] = 0;
		os[5] = 0;
		os[6] = 0;
		os[7] = 0;
		os[8] = 0;
		os[9] = OS_CODE;

		*(int*)( &os[ z.total_out + 10 ] ) = crc32( 0, s, n );
		*(int*)( &os[ z.total_out + 14 ] ) = n;
	}

	return lua_userdatalen( os, z.total_out + pad ), 1;
}


void inflating(lua_State *lua, const char *s, size_t n, size_t on, bool raw)
{
	unsigned char *os = (unsigned char *) newbdata( lua, on );
	z_stream z	= { 0,0,0, 0,0,0, 0,0, 0,0,0, 0,0,0 };
	z.next_in	= s;
	z.avail_in	= (unsigned int)n;
	z.next_out	= os;
	z.avail_out	= (unsigned int)on;
	int err = inflateInit2_( &z, raw ? -15 : 15, "1.2.3", sizeof( z ) );
	if ( err || ( err = inflate( &z, 4 ), inflateEnd( &z ), err ) != 1 ) // Z_FINISH Z_STREAM_END
		error( lua, "error %d", err );
	lua_userdatalen( os, z.total_out );
}
// -2|3 str|data,size,zlibMode +1 data
static int zip_inflate(lua_State *lua)
{
	size_t n; const char *s = tobytes(lua, 1, &n, "#1");
	int on = 0;
	if ( *(int*)s == 0x00088b1F && n > 18 )
	{
		// Skip gz header and get the orignal length.
		on = *(int*)( s + n - 4 );
		s += 10;
		n -= 18;
	}
	else
	{
		on = mustint(lua, 2);
		if (on <= 0)
			return error(lua, "buffer overflow");
	}
	return inflating(lua, s, n, on, !tobool(lua, 3)), 1;
}

// -2|2 str|data,size +1 data
static int zip_crc32(lua_State *lua)
{
	size_t n; const char *s = tobytes(lua, 1, &n, "#1");
	return pushi( lua, crc32( crc32( 0, NULL, 0 ), s, n ) ), 1;
}

// -1 {name=data} +1 zip
static int zip_zip(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	size_t en = 0, n = 0, c = 22, kn, vn;
	for (pushz(lua); lua_next(lua, 1); pop(lua, 1))
	{
		if (++en > 32767)
			error(lua, "too many entry");
		tobytes(lua, -2, &kn, "#1 key"), tobytes(lua, -1, &vn, "#1 value");
		if (kn > 32767)
			error(lua, "name too long");
		n += 30+kn, n += vn+(vn>>12)+(vn>>14)+(vn>>25)+13, c += 46+kn;
	}
	int y, M, d, h, m, s, _;
	unpackTime(timeNow(0, false), &y, &M, &d, &h, &m, &s, &_, &_, &_, &_, &_);
	int time = ( y - 1980 ) << 25 | M << 21 | d << 16 | h << 11 | m << 5 | s >> 1;
	int L = gettop(lua)+1;
	unsigned char *zip = (unsigned char*) newbdata( lua, n + c ), *zn = zip + n, *z = zip, *zm; // L zip
	for ( pushz( lua ); lua_next( lua, 1 ); pop( lua, 1 ) ) // L+1 key L+2 value
	{
		const char *k = tobytes( lua, L + 1, &kn, NULL ), *v = tobytes( lua, L + 2, &vn, NULL );
		*(int*)z = 0x04034b50;
		*(short*)( z + 4 ) = 0x14, *(short*)( z + 6 ) = 0, *(short*)( z + 8 ) = 8;
		*(int*)( z + 10 ) = time, *(int*)( z + 14 ) = crc32( crc32( 0, NULL, 0 ), v, vn );
		*(size_t*)( z + 22 ) = vn, *(short*)( z + 26 ) = (short)kn, *(short*)( z + 28 ) = 0;
		memcpy( z + 30, k, kn );
		unsigned char *zv = z + 30 + kn;
		z_stream zz	= { 0,0,0, 0,0,0, 0,0, 0,0,0, 0,0,0 };
		zz.next_in	= v;
		zz.avail_in	= (unsigned int)vn;
		zz.next_out	= zv;
		zz.avail_out= (unsigned int)( zv < zn ? ( zn -zv ) : 0);
		int err = deflateInit2_( &zz, -1, 8, -15, 8, 0, "1.2.3", sizeof( zz ) );
		if ( err || ( err = deflate( &zz, 4 ), deflateEnd( &zz ), err ) != 1 ) // Z_FINISH Z_STREAM_END
			error( lua, "%s error %d", k, err );
		vn = zz.total_out, *(size_t*)( z + 18 ) = vn;
		z = zv+vn;
	}
	zn = zm = z;

	for (z = zip; z < zn; )
	{
		*(int*)zm = 0x02014b50, *(short*)(zm+4) = 0x14;
		memcpy(zm+6, z+4, 26);
		*(short*)(zm+32) = 0, *(short*)(zm+34) = 0, *(short*)(zm+36) = 0;
		*(int*)(zm+38) = 0, *(int*)(zm+42) = (int)(z-zip);
		size_t kn = *(short*)(z+26);
		memcpy(zm+46, z+30, kn);
		z += 30+kn+*(int*)(z+18), zm += 46+kn;
	}
	*(int*)zm = 0x06054b50, *(short*)(zm+4) = 0, *(short*)(zm+6) = 0;
	*(short*)(zm+8) = *(short*)(zm+10) = (short)en;
	*(int*)(zm+12) = (int)(zm-zn), *(int*)(zm+16) = (int)(zn-zip), *(short*)(zm+20) = 0;
	zm += 22;
	lua_userdatalen(zip, zm-zip);
	return push(lua, L), 1;
}

// -1 zip +1 {name=data}
static int zip_unzip(lua_State *lua)
{
	size_t n; const char *ss = tobytes(lua, 1, &n, "#1"), *s = ss;
	if (n <= 30)
		return error(lua, "bad zip");
	int L = gettop(lua)+1;
	newtable(lua); // L unzip
	for (ss += n-30; s < ss && *(int*)s != 0x02014b50; )
	{
		short kind = *(short*)(s+8);
		if (*(int*)s != 0x04034b50 || *(short*)(s+6) & 0x80 || (kind != 8 && kind != 0))
			return error(lua, "unsupported zip");
		int crc = *(int*)(s+14);
		size_t n = *(size_t*)(s+18), on = *(size_t*)(s+22);
		size_t name = *(short*)(s+26), extra = *(short*)(s+28);
		if ((s += 30+name+extra) >= ss-n)
			return error(lua, "bad zip");
		pushsl(lua, s-name-extra, name); // L+1 name
		if (kind == 0 && n != on)
			return error(lua, "bad zip %s", tostr(lua, L+1));
		if (kind == 8)
			inflating(lua, s, n, on, true); // L+2 data
		else
			memcpy(newbdata(lua, n), s, n); // L+2 data
		if (crc32(crc32(0, NULL, 0), (char*)toudata(lua, L+2), on) != crc)
			return error(lua, "bad crc %s", tostr(lua, L+1));
		rawset(lua, L);
		s += n;
	}
	return push(lua, L), 1;
}

static void zip(lua_State *lua, int M, int G)
{
	pushc(lua, zip_deflate), rawsetn(lua, G, "_deflate");
	pushc(lua, zip_inflate), rawsetn(lua, G, "_inflate");
	pushc(lua, zip_crc32), rawsetn(lua, G, "_crc32");
	pushc(lua, zip_zip), rawsetn(lua, G, "_zip");
	pushc(lua, zip_unzip), rawsetn(lua, G, "_unzip");
}

#ifdef __cplusplus
}
#endif
