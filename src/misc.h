#ifndef MISC
#define MISC
#if __linux
	#define LINUX 1
	#ifndef __USE_BSD
	#define __USE_BSD
	#endif
	#ifndef __USE_MISC
	#define __USE_MISC
	#endif
#elif __APPLE__ & __MACH__
	#define MAC 1
#elif _WIN32
	#define WIN 1
	#pragma warning (disable:4550)
	#pragma warning (disable:4554)
	#pragma warning (disable:4723)
	#pragma warning (disable:4806)
#else
	#error "unsupport os"
#endif

#if defined(__GNUC__) && ( defined(LINUX) || defined(__APPLE_CPP__) || defined(__APPLE_CC__) || defined(__MACOS_CLASSIC__) )
#define uintptr uintptr_t
#else
#define uintptr unsigned int
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#ifndef _USE_LUA51
#include "luajit.h"
#endif

#if WIN
	#include <io.h>
	#include <windows.h>
	#include <time.h>
	#define memccpy _memccpy


	static char *errmsg(int err)
	{
		static char s[30];
		sprintf(s, "err%dx%x", err, err);
		return s;
	}
	
	#define strerror errmsg
	#define hstrerror strerror
	#define usleep(us) Sleep(us/1000)
	#define sched_yield() Sleep(0)
#else
	#include <errno.h>
	#include <unistd.h>
	#include <sched.h>
	#include <dirent.h>
#if !ANDROID
	#include <ifaddrs.h>
#endif
	#include <sys/time.h>
	#include <sys/stat.h>
#endif
#if __arm__
	#warning twice 32bit access instead of unaligned 64bit on ARM
	inline static long long readL(const void *p)
	{
		return *(volatile unsigned*)p | (unsigned long long)((volatile unsigned*)p)[1] << 32;
	}
	inline static double readD(const void *p)
	{
		double v;
		((int *)&v)[0] = ((volatile int*)p)[0];
		((int *)&v)[1] = ((volatile int*)p)[1];
		return v;
	}
	inline static long long writeL(void *p, long long v)
	{
		((volatile unsigned *)p)[0] = (unsigned)v;
		((volatile unsigned *)p)[1] = (unsigned)(v >> 32);
		return v;
	}
	inline static double writeD(void *p, double v)
	{
		((volatile unsigned *)p)[0] = ((unsigned*)&v)[0];
		((volatile unsigned *)p)[1] = ((unsigned*)&v)[1];
		return v;
	}
#else
	#define readL(p) *(long long*)(p)
	#define readD(p) *(double*)(p)
	#define writeL(p,v) *(long long*)(p) = (long long)(v)
	#define writeD(p,v) *(double*)(p) = (double)(v)
#endif

#define M_G 1
#define M_onerror 2
#define M_string 3
#define M_file 4
#define M_now 5
#define M_nowq 6
#define M__from 7
#define M_qsub 8
#define M_queue 9
#define M_qfroms 10
#define M_qpre 11
#define M_qpost 12
#define M_qerr 13
#define M_qfunc 14
#define M_qcall 15
#define M_encoded 16
#define M_decoded 17
#define M_nets 18
#define M_netmeta 19
#define M_netlaunch 20
#define M_sqls 21
#define M_sqlmeta 22
#define M_mysqls 23
#define M_mysqlmeta 24
#define M_decoderef 25
#define M_encoderef 26
#define M_decodebuff 27
#define M_sqlrunbuff 28
#define M_sqlrunbuffs 29

//////////////////////////////// base ////////////////////////////////


static void logErr(const char *s)
{
/*#if WIN
	fflush(stdout) != EOF && _write(stdout->_file, s, (int)strlen(s)) >= 0
		&& _write(stdout->_file, "\n", 1);
#else
	printf("%s\n", s), fflush(stdout);
#endif*/
	printf("%s\n", s), fflush(stdout);
}


#if WIN
#define bool unsigned char
#else
#ifndef __cplusplus
typedef unsigned char bool;
#endif
#endif
#undef false
#define false 0
#undef true
#define true 1
#define gettop(lua) lua_gettop(lua)
#define settop(lua,top) lua_settop(lua, top)
#define upx(index) lua_upvalueindex(index)
#define regist LUA_REGISTRYINDEX
#define global LUA_GLOBALSINDEX
#define isnil(lua,index) lua_isnoneornil(lua,index)
#define isnum(lua,index) (lua_type(lua, index)==LUA_TNUMBER)
#define isstr(lua,index) (lua_type(lua, index)==LUA_TSTRING)
#define isbool(lua,index) (lua_type(lua, index)==LUA_TBOOLEAN)
#define isfunc(lua,index) (lua_type(lua, index)==LUA_TFUNCTION)
#define totype(lua,index) lua_type(lua, index)
#define tonum(lua,index) lua_tonumber(lua, index)
#define tolong(lua,index) d2l(lua_tonumber(lua, index))
#define toint(lua,index) d2i(lua_tonumber(lua, index))
#define touint(lua,index) d2ui(lua_tonumber(lua, index))
#define tobool(lua,index) lua_toboolean(lua, index)
#define tostr(lua,index) lua_tostring(lua, index)
#define totab(lua,index) lua_totable(lua, index)
#define toudata(lua,index) lua_touserdata(lua, index)
#define tobody(lua,index) lua_topointer(lua, index)
#define tohead(lua,index) lua_tohead(lua, index)
#define tolen(lua,index) lua_objlen(lua, index)
#define tocfunc(lua,index) lua_tocfunction(lua, index)
#define tonamet(lua,t) lua_typename(lua, t)
#define tonamex(lua,index) luaL_typename(lua, index)
#define push(lua,index) lua_pushvalue(lua, index)
#define pushz(lua) lua_pushnil(lua)
#define pushb(lua,b) lua_pushboolean(lua, b)
#define pushi(lua,i) lua_pushinteger(lua, i)
#define pushn(lua,n) lua_pushnumber(lua, (double)(n))
#define pushs(lua,s) lua_pushstring(lua, s)
#define pushsl(lua,s,len) lua_pushlstring(lua, s, len)
#define pushc(lua,cfunc) lua_pushcfunction(lua, cfunc)
#define pushcc(lua,cfunc,n) lua_pushcclosure(lua, cfunc, n)
#define pushaddr(lua,addr) lua_pushlightuserdata(lua, addr)
#define pop(lua,n) lua_pop(lua, n)
#define popif(lua,yes,n) ((yes) && (lua_pop(lua, n), true))
#define popifz(lua,n) popif(lua, lua_isnil(lua, -1), n)
#define popeq(lua) (lua_rawequal(lua, -1, -2) ? lua_pop(lua, 2), true : lua_pop(lua, 2), false)
#define raweq(lua,a,b) lua_rawequal(lua, a, b)
#define streq(lua,index,str) (strcmp(lua_tostring(lua, index), str)==0)
#define streqx(lua,a,b) (lua_tostring(lua, a)==lua_tostring(lua, b))
#define call(lua,argn,ren) lua_call(lua, argn, ren)
#define pcall(lua,argn,ren,err) lua_pcall(lua, argn, ren, err)
#define newudata(lua,size) lua_newuserdata(lua, size)
#define getmeta(lua,index) lua_getmetatable(lua, index)
#define setmeta(lua,index) lua_setmetatable(lua, index)
#define getmetai(lua,tab,index) \
	(getmeta(lua,tab) ? rawgeti(lua,-1,index),lua_remove(lua,-2) : pushz(lua))
#define getmetan(lua,tab,name) luaL_getmetafield(lua, tab, name)
#define getuv(lua,index) lua_getfenv(lua, index)
#define getuvi(lua,tab,index) (getuv(lua,tab), rawgeti(lua,-1,index), lua_remove(lua,-2))
#define getuvn(lua,tab,name) (getuv(lua,tab), rawgetn(lua,-1,name), lua_remove(lua,-2))
#define newtable(lua) lua_newtable(lua)
#define newtablen(lua,in,kn) lua_createtable(lua, in, kn)

inline static long long d2l(double d)
{
	long long v = (long long)d;
	return v < 0 && d > 0 ? v - 1 : v; // 0x8000000000000000LL->0x7FFFffffFFFFffffLL
}
inline static int d2i(double d)
{
	int v = (int)d;
	return v < 0 && d > 0 ? v - 1 : v; // 0x80000000->0x7FFFffff
}
inline static unsigned d2ui(double d)
{
	return d > 0 ? d < 4294967297.0 ? (unsigned)(long long)d : (unsigned)4294967296 : 0;
}

inline static bool popz(lua_State *lua)
{
	bool v = isnil(lua, -1);
	return pop(lua, 1), v;
}
inline static int poptype(lua_State *lua)
{
	int v = totype(lua, -1);
	return pop(lua, 1), v;
}
inline static double popn(lua_State *lua)
{
	double v = tonum(lua, -1);
	return pop(lua, 1), v;
}
inline static long long poplong(lua_State *lua)
{
	long long v = tolong(lua, -1);
	return pop(lua, 1), v;
}
inline static int popint(lua_State *lua)
{
	int v = toint(lua, -1);
	return pop(lua, 1), v;
}
inline static bool popb(lua_State *lua)
{
	bool v = tobool(lua, -1);
	return pop(lua, 1), v;
}
inline static const char *pops(lua_State *lua)
{
	const char *v = tostr(lua, -1);
	return pop(lua, 1), v;
}
inline static void *popudata(lua_State *lua)
{
	void *v = toudata(lua, -1);
	return pop(lua, 1), v;
}
inline static void *pophead(lua_State *lua)
{
	void *v = (void *)tohead(lua, -1);
	return pop(lua, 1), v;
}
inline static void *popbody(lua_State *lua)
{
	void *v = (void *)tobody(lua, -1);
	return pop(lua, 1), v;
}
inline static size_t poplen(lua_State *lua)
{
	size_t v = tolen(lua, -1);
	return pop(lua, 1), v;
}

#define rawget(lua,table) lua_rawget(lua, table)
#define rawgetk(lua,table,key) lua_rawgetk(lua, table, key)
#define rawgeti(lua,table,key) lua_rawgeti(lua, table, key)
#define rawgetn(lua,table,key) lua_rawgetn(lua, table, key)
#define rawset(lua,table) lua_rawset(lua, table)
#define rawsetk(lua,table,key) lua_rawsetk(lua, table, key)
#define rawseti(lua,table,key) lua_rawseti(lua, table, key)
#ifdef _USE_LUA51
#define rawsetn(lua,table,key) lua_setfield(lua, table, key)
#else
#define rawsetn(lua,table,key) lua_rawsetn(lua, table, key)
#endif
#define rawsetv(lua,table,value) lua_rawsetv(lua, table, value)
#define rawsetkv(lua,table,key,value) lua_rawsetkv(lua, table, key, value)
#define rawsetiv(lua,table,key,value) lua_rawsetiv(lua, table, key, value)
#define rawsetnv(lua,table,key,value) lua_rawsetnv(lua, table, key, value)

#define tabget(lua,table) lua_gettable(lua, table)
#define tabgetk(lua,table,key) lua_gettablek(lua, table, key)
#define tabgeti(lua,table,key) lua_gettablei(lua, table, key)
#define tabgetn(lua,table,key) lua_getfield(lua, table, key)
#define tabset(lua,table) lua_settable(lua, table)
#define tabsetk(lua,table,key) lua_settablek(lua, table, key)
#define tabseti(lua,table,key) lua_settablei(lua, table, key)
#define tabsetn(lua,table,key) lua_setfield(lua, table, key)
#define tabsetv(lua,table,value) lua_settablev(lua, table, value)
#define tabsetkv(lua,table,key,value) lua_settablekv(lua, table, key, value)
#define tabsetiv(lua,table,key,value) lua_settableiv(lua, table, key, value)
#define tabsetnv(lua,table,key,value) lua_setfieldv(lua, table, key, value)

inline static void newmeta(lua_State *lua, int index)
{
	lua_createtable(lua, 0, 4);
	pushb(lua, false), rawsetn(lua, -2, "__metatable");
	if (index)
		push(lua, -1), setmeta(lua, index >= 0 || index <= regist ? index : index-2);
}
inline static void newmetan(lua_State *lua, int index, int in, int kn)
{
	lua_createtable(lua, in, kn);
	pushb(lua, false), rawsetn(lua, -2, "__metatable");
	if (index)
		push(lua, -1), setmeta(lua, index >= 0 || index <= regist ? index : index-2);
}
inline static void newmetameta(lua_State *lua, int index, int in, int kn, const char *fake)
{
	lua_createtable(lua, in, kn);
	pushs(lua, fake), rawsetn(lua, -2, "__metatable");
	if (index)
		push(lua, -1), setmeta(lua, index >= 0 || index <= regist ? index : index-2);
}
inline static void newmetaweak(lua_State *lua, int index, int in, int kn, const char *weak)
{
	lua_createtable(lua, in, kn);
	pushs(lua, weak), rawsetn(lua, -2, "__mode");
	if (index)
		push(lua, -1), setmeta(lua, index >= 0 || index <= regist ? index : index-2);
}
inline static char *newbdata(lua_State *lua, size_t size)
{

	#ifdef _USE_LUA51

	char *u = (char*)newudata(lua, size+5);
	u[size+4] = 0, *(unsigned int*)u = size;
	return u+4;

	#else

	char *u = (char*)newudata(lua, size+1);
	u[size] = 0, lua_userdatalen(u, size);
	getmeta(lua, global), rawgeti(lua, -1, M_string), setmeta(lua, -3), pop(lua, 1);
	return u;

	#endif
}

inline static int calln(lua_State *lua, int argn)
{
	int L = gettop(lua)-1-argn;
	lua_call(lua, argn, LUA_MULTRET);
	return gettop(lua)-L;
}

//////////////////////////////////// check //////////////////////////////////

static int error(lua_State *lua, const char *format, ...)
{
	for (int x = 1; luaL_where(lua, x), !tostr(lua, -1)[0] && x < 10; x++)
		;
	va_list va; va_start(va, format), lua_pushvfstring(lua, format, va), va_end(va);
	return lua_concat(lua, 2), lua_error(lua);
}
static int werror(lua_State *lua, const char *where, const char *format, ...)
{
	for (int x = 1; luaL_where(lua, x), !tostr(lua, -1)[0] && x < 10; x++)
		;
	where && (pushs(lua, where), 0);
	va_list va; va_start(va, format), lua_pushvfstring(lua, format, va), va_end(va);
	return lua_concat(lua, where ? 3 : 2), lua_error(lua);
}
static int wwerror(lua_State *lua, const char *where, const char *where2, const char *format, ...)
{
	for (int x = 1; luaL_where(lua, x), !tostr(lua, -1)[0] && x < 10; x++)
		;
	where && (pushs(lua, where), 0), where2 && (pushs(lua, where2), 0);
	va_list va; va_start(va, format), lua_pushvfstring(lua, format, va), va_end(va);
	return lua_concat(lua, 2 + !!where + !!where2), lua_error(lua);
}
static int errType(lua_State *lua, const char *s, int t, int got)
{
	return error(lua, "%s (%s expected, got %s)", s, tonamet(lua, t), tonamet(lua, got));
}
static int errArg(lua_State *lua, const char *m, int t, int got)
{
	return error(lua, "bad argument %s (%s expected, got %s)", m,
		tonamet(lua, t), tonamet(lua, got));
}
static int errArgT(lua_State *lua, const char *m, const char *t, int got)
{
	return error(lua, "bad argument %s (%s expected, got %s)", m, t, tonamet(lua, got));
}
static int werrType(lua_State *lua, const char *where, const char *s, int t, int got)
{
	return werror(lua, where, "%s (%s expected, got %s)", s,
		tonamet(lua, t), tonamet(lua, got));
}
static int werrArg(lua_State *lua, const char *where, const char *m, int t, int got)
{
	return werror(lua, where, "bad argument %s (%s expected, got %s)", m,
		tonamet(lua, t), tonamet(lua, got));
}

static void dumpTypes(lua_State *lua, const char *pre)
{
	printf("=LUA= %s : ", pre);
	for (int L = gettop(lua), i = 1; i <= L; i++)
		printf("%d %s~%p ", i, tonamex(lua, i), tobody(lua, i));
	printf("\n");
}

inline static void checkArg(lua_State *lua, int x, const char *m, int t)
{
	int T = totype(lua, x);
	T != t && (errArg(lua, m, t, T), 0);
}
inline static bool checkArgable(lua_State *lua, int x, const char *m, int t)
{
	int T = totype(lua, x);
	return T != LUA_TNONE && (T == t || errArg(lua, m, t, T));
}
inline static bool checkArgableZ(lua_State *lua, int x, const char *m, int t)
{
	int T = totype(lua, x);
	return T == LUA_TNONE ? pushz(lua), false : T && (T == t || errArg(lua, m, t, T));
}

/////////////////////////////////// number ///////////////////////////////

// with type check
inline static long long mustlong(lua_State *lua, int index)
{
	if ( !isnum(lua, index))
		error(lua, "signed 54bit int expected, got %s", tonamex(lua, index));
	double V = tonum(lua, index); long long v = (long long)V;
	if (v != V || v != v<<10>>10)
		error(lua, "number %f must be signed 54bit int", V);
	return v;
}
// with type check
inline static int mustint(lua_State *lua, int index)
{
	if ( !isnum(lua, index))
		error(lua, "signed 32bit int expected, got %s", tonamex(lua, index));
	double V = tonum(lua, index); long long vv = (long long)V; int v = (int)vv;
	if (v != V || v != vv)
		error(lua, "number %f must be signed 32bit int", V);
	return v;
}
// with type check
inline static long long roundlong(lua_State *lua, int index)
{
	if ( !isnum(lua, index))
		error(lua, "signed 54bit range expected, got %s", tonamex(lua, index));
	double V = tonum(lua, index); long long v = (long long)V;
	if (V != V || v != v<<10>>10)
		error(lua, "number %f out of signed 54bit range", V);
	return v;
}
// with type check
inline static int roundint(lua_State *lua, int index)
{
	if ( !isnum(lua, index))
		error(lua, "signed 32bit range expected, got %s", tonamex(lua, index));
	double V = tonum(lua, index); long long vv = (long long)V; int v = (int)vv;
	if (V != V || v != vv)
		error(lua, "number %f out of signed 32bit range", V);
	return v;
}

// with type check
inline static long long popmustlong(lua_State *lua)
{
	long long v = mustlong(lua, -1);
	return pop(lua, 1), v;
}
// with type check
inline static int popmustint(lua_State *lua)
{
	int v = mustint(lua, -1);
	return pop(lua, 1), v;
}
// with type check
inline static long long poproundlong(lua_State *lua)
{
	long long v = roundlong(lua, -1);
	return pop(lua, 1), v;
}
// with type check
inline static int poproundint(lua_State *lua)
{
	int v = roundint(lua, -1);
	return pop(lua, 1), v;
}

static const char *tobytes(lua_State *lua, int index, size_t *len, const char *m)
{
	int t = totype(lua, index);
	if (t == LUA_TSTRING)
		return lua_tolstring(lua, index, len);
	if (t == LUA_TUSERDATA)
	{
		#ifdef _USE_LUA51
		
		const char* buf = (const char*)toudata(lua, index);
		*len = *(unsigned int*)buf;
		return buf+4;

		#else

		void *meta = (getmetai(lua, global, M_string), popbody(lua));
		if (meta == (getmeta(lua, index) ? popbody(lua) : NULL))
			return *len = tolen(lua, index), (const char*)toudata(lua, index);

		#endif
	}
	if (m)
		error(lua, "bad argument %s (bytes expected, got %s)", m, tonamet(lua, t));
	return *len = 0, (const char *)NULL;
}

inline static int range(int x, int min, int max)
{
	return x < min ? min : x > max ? max : x;
}
inline static size_t rangez(size_t x, size_t min, size_t max)
{
	return x < min ? min : x > max ? max : x;
}

//////////////////////////////// array /////////////////////////////////////

// return [1,n], or 0 if n is 0
inline static size_t indexn(int x, size_t n)
{
	return n == 0 ? 0 : x == 0 ? 1 :
		x < 0 ? (x += n+1) > 0 ? (size_t)x : 1 :
		(size_t)x > n ? n : (size_t)x;
}
// return n ? indexn(x, n)-1 : 0
inline static size_t indexn0(int x, size_t n)
{
	return n ? indexn(x, n)-1 : 0;
}

// -n
inline static void spush(lua_State *lua, int tab, int n, bool raw)
{
	int m = (int)tolen(lua, tab);
	if (n < 0 || n > gettop(lua))
		error(lua, "bad argument");
	if ((m + n) < 0)
		error(lua, "array too large");
	if (raw)
		for (; n > 0; n--)
			rawseti(lua, tab, m + n);
	else
		for (; n > 0; n--)
			tabseti(lua, tab, m + n);
}

inline static void spushs(lua_State *lua, int tab, int first, bool raw)
{
	int a = (int)tolen(lua, tab), b = (int)tolen(lua, first);
	if ((a + b) < 0)
		error(lua, "array too large");
	if (raw)
		for (int i = 1; i <= b; i++)
			rawgeti(lua, first, i), rawseti(lua, tab, a + i);
	else
		for (int i = 1; i <= b; i++)
			tabgeti(lua, first, i), tabseti(lua, tab, a + i);
}

inline static void sfillz(lua_State *lua, int tab, size_t n)
{
	for (size_t m = tolen(lua, tab); ++n <= m; )
		pushz(lua), rawseti(lua, tab, (int)n);
}

static void scopy(lua_State *lua, int tab, int first, int last, bool raw, bool samemeta)
{
	int n = (int)tolen(lua, tab);
	samemeta = samemeta && getmeta(lua, tab);
	first = (int)indexn(first, n), last = (int)indexn(last, n);
	n = last-first+1, n < 0 && (n = 0);
	newtablen(lua, n, 0);
	if (samemeta)
		lua_insert(lua, -2), setmeta(lua, -2);
		
	if (raw && last < *(int*)((char*)tohead(lua, tab)+24)) // GCtab->asize
	{
		long long *fs = *(long long**)((char*)tohead(lua, tab)+8); // GCtab->array
		long long *ts = *(long long**)((char*)tohead(lua, -1)+8); // GCtab->array
		memcpy(ts+1, fs+first, n*8);
	}
	else if (raw)
		for (int i = 1; i <= n; i++, first++)
			rawgeti(lua, tab, first), rawseti(lua, -2, i);
	else
		for (int i = 1; i <= n; i++, first++)
			tabgeti(lua, tab, first), tabseti(lua, -2, i);
}

static int sunpack(lua_State *lua, int vs, int ks, bool raw)
{
	int n = (int)tolen(lua, ks);
	if (raw)
		for (int i = 1; i <= n; i++)
			rawgeti(lua, ks, i), rawget(lua, vs);
	else
		for (int i = 1; i <= n; i++)
			tabgeti(lua, ks, i), tabget(lua, vs);
	return n;
}

// treat nan larger than inf and equal to nan
static int binfind(lua_State *lua, int tab, int low, int high, double key, int howeq)
{
	int n = (int)tolen(lua, tab);
	low = (int)indexn(low, n), high = (int)indexn(high, n);
	if (key == key) // key isn't nan
		while (low <= high)
		{
			int mid = (unsigned)(low + high) >> 1;
			rawgeti(lua, tab, mid); double v = popn(lua);
			if (key > v)
				low = mid+1;
			else if (key < v || v != v)
				high = mid-1;
			else if (howeq > 0)
				low = mid+1;
			else if (howeq < 0)
				high = mid-1;
			else
				return mid;
		}
	else // key is nan
		while (low <= high)
		{
			int mid = (unsigned)(low + high) >> 1;
			rawgeti(lua, tab, mid); double v = popn(lua);
			if (v == v)
				low = mid+1;
			else if (howeq > 0)
				low = mid+1;
			else if (howeq < 0)
				high = mid-1;
			else
				return mid;
		}
	return ~low;
}

#if LINUX
	void *memmem(const void *mem, size_t n, const void *sub, size_t m);
	#define memfind memmem
#else
	inline static const void *memfind(const void *mem, size_t n, const void *sub, size_t m)
	{
		size_t i = 0;
		if (n)
			do
				if (memcmp((char*)mem+i, sub, m) == 0) return (char*)mem+i;
			while (++i < n);
		return NULL;
	}
#endif

////////////////////////////////// PRNG /////////////////////////////////

// initial: z[0]>1, z[1]>7, z[2]>15, z[3]>127
static unsigned lfsr113(unsigned *z)
{
	unsigned b;
	b = ((z[0]<<6)^z[1])>>13;
	z[0] = ((z[0]&0xFFFFFFFE)<<18)^b;
	b = ((z[1]<<2)^z[1])>>27;
	z[1] = ((z[1]&0xFFFFFFF8)<<2)^b;
	b = ((z[2]<<13)^z[2])>>21;
	z[2] = ((z[2]&0xFFFFFFF0)<<7)^b;
	b = ((z[3]<<3)^z[3])>>12;
	z[3] = ((z[3]&0xFFFFFF80)<<13)^b;
	return z[0]^z[1]^z[2]^z[3];
}

// initial: s[0..15] random, index=0
static unsigned well512(unsigned *s, unsigned *index)
{
	unsigned a, b, c, d;
	a = s[*index];
	c = s[(*index+13)&15];
	b = a^c^(a<<16)^(c<<15);
	c = s[(*index+9)&15];
	c ^= c>>11;
	a = s[*index] = b^c;
	d = a^((a<<5)&0xDA442d20);
	*index = (*index+15)&15;
	a = s[*index];
	return s[*index] = a^b^d^(a<<2)^(b<<18)^(c<<28);
}

///////////////////////////////// misc ////////////////////////////////

#define ARGSIZE 20

// name refered, return number of names
static int getArgs(lua_State *lua, int func, const char *names[])
{
	checkArg(lua, func, "index", LUA_TFUNCTION);
	const char *name; int x;
	push(lua, func);
	for (x = 1; (name = lua_getlocal(lua, NULL, x)); x++)
		if (x > ARGSIZE)
			return error(lua, "too many arguments");
		else
			pushs(lua, name), names[x-1] = tostr(lua, -1), luaL_ref(lua, regist);
	return pop(lua, 1), x-1;
}

////////////////////////////////////////////////////////////////////////

// Fringe Search: Beating A* at Pathfinding on Game Maps

#ifdef __cplusplus
}
#endif

#endif
