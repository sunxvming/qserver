#ifdef _WIN32
	#include <winsock2.h> // must before misc.h
	#include <ws2tcpip.h>
	#pragma comment(lib, "ws2_32.lib")
#endif

#if _MSC_VER>=1900
#include "stdio.h" 
_ACRTIMP_ALT FILE* __cdecl __acrt_iob_func(unsigned);
#ifdef __cplusplus 
extern "C"
#endif 
FILE* __cdecl __iob_func(unsigned i) {
	return __acrt_iob_func(i);
}

#endif /* _MSC_VER>=1900 */

#include <unordered_map>
#include <vector>
#include <string>
#include <algorithm>
#include "power.h"
#include "misc.h"
#include "datime.h"
#include "rsa.h"
#include "des.h"
#include "lbn.h"
#include "qstring.h"
#include "codec.h"
#include "queue.h"
#include "event.h"
#include "remote.h"
#include "net.h"
#include "sql.h"
#include "lz4.h"
#include "qmysql.h"
#include "zip.h"
#include "conf.h"
#include "path.h"
/////////////////////////////////// math //////////////////////////////////

// -1 number +1 integer
static int base_int(lua_State *lua)
{
	return pushn(lua, mustlong(lua, 1)), 1;
}

// -1 number +1 integer
static int base_int32(lua_State *lua)
{
	return pushi(lua, mustint(lua, 1)), 1;
}

// -1|2 number/string,round +1 integer
static int base_toint(lua_State *lua)
{
	double V = tonum(lua, 1);
	if (V == 0 && !lua_isnumber(lua, 1))
		return pushz(lua), 1;
	double r = luaL_optnumber(lua, 2, 0);
	long long v = d2l
		(r >= 1 ? ceil(V) : r <= -1 ? floor(V) : r && r==r ? floor(V+0.5) : V);
	pushn(lua, v >= 1LL<<53 ? (1LL<<53)-1 : v < -1LL<<53 ? -1LL<<53 : v);
	return 1;
}

// -1|2 number/string,round +1 integer
static int base_toint32(lua_State *lua)
{
	double V = tonum(lua, 1);
	if (V == 0 && !lua_isnumber(lua, 1))
		return pushz(lua), 1;
	double r = luaL_optnumber(lua, 2, 0);
	int v = d2i(r >= 1 ? ceil(V) : r <= -1 ? floor(V) : r && r==r ? floor(V+0.5) : V);
	return pushi(lua, v), 1;
}

// -2 obj,tostrFunc +2 obj,tostrFunc
static int base_tostring(lua_State *lua)
{
	checkArg(lua, 2, "#2", LUA_TFUNCTION);
	if ( !getmeta(lua, 1))
		error(lua, "no metatatable");
	pushs(lua, "__tostring");
	if (rawgetk(lua, -2, -1), !popz(lua))
		error(lua, "tostring exist");
	rawsetkv(lua, -2, -1, 2);
	return settop(lua, 2), 2;
}

// -1 +1
static int base_not(lua_State *lua)
{
	long long a = tolong(lua, 1);
	return pushn(lua, (double)~a), 1;
}

// -2 +2
static int base_and(lua_State *lua)
{
	long long a = tolong(lua, 1);
	long long b = tolong(lua, 2);
	return pushn(lua, (double)(a&b)), 1;
}

// -2 +2
static int base_or(lua_State *lua)
{
	long long a = tolong(lua, 1);
	long long b = tolong(lua, 2);
	return pushn(lua, (double)(a|b)), 1;
}

// -2 +2
static int base_xor(lua_State *lua)
{
	long long a = tolong(lua, 1);
	long long b = tolong(lua, 2);
	return pushn(lua, (double)(a^b)), 1;
}

// -2 +2
static int base_lshift(lua_State *lua)
{
	long long a = tolong(lua, 1);
	long long b = mustlong(lua, 2);
	return pushn(lua, (double)(b>53 || b<-53 ? 0 : b<0 ? (a&0x3FffffFFFFffffLL)>>-b : a<<b)), 1;
}

// -2 +2
static int base_rshift(lua_State *lua)
{
	long long a = tolong(lua, 1);
	long long b = mustlong(lua, 2);
	return pushn(lua, (double)(b>53 || b<-53 ? 0 : b<0 ? a<<-b : (a&0x3FffffFFFFffffLL)>>b)), 1;
}

// -2 +2
static int base_arshift(lua_State *lua)
{
	long long a = tolong(lua, 1);
	long long b = mustlong(lua, 2);
	return pushn(lua, (double)(b>53 ? a>>53 : b<-53 ? 0 : b<0 ? a<<-b : a>>b)), 1;
}

/////////////////////////////////// table /////////////////////////////////

// -1|4 table,first,last,keepmeta +1 sub
static int table_sub(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	scopy(lua, 1, luaL_optint(lua, 2, 1), luaL_optint(lua, 3, -1), !tobool(lua, 4), tobool(lua, 4));
	return 1;
}

// -2|3 to,from,nometa +2 to,ok
static int table_copy(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	checkArg(lua, 2, "#2", LUA_TTABLE);
	bool ok = false;
	if (tobool(lua, 3))
		for (pushz(lua); lua_next(lua, 2); )
			rawsetk(lua, 1, -2), ok = true;
	else
		for (pushz(lua); lua_next(lua, 2); )
			tabsetk(lua, 1, -2), ok = true;
	return push(lua, 1), pushb(lua, ok), 2;
}

static int table_duplicate(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	lua_duplicatetable(lua, 1);
	return 1;
}

static int table_clear(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	lua_cleartable(lua, 1);
	return 1;
}

static int table_new(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	checkArg(lua, 2, "#2", LUA_TNUMBER);
	lua_createtable(lua, tonum(lua, 1), tonum(lua, 2));
	return 1;
}

static int table_size(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	lua_sizetable(lua, 1);
	return 2;
}

// -2|3 to,from,nometa +2 to,ok
static int table_append(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	checkArg(lua, 2, "#2", LUA_TTABLE);
	bool ok = false;
	if (tobool(lua, 3))
		for (pushz(lua); lua_next(lua, 2); )
		{
			rawgetk(lua, 1, -2);
			if ( popz(lua) )
			{
				rawsetk(lua, 1, -2);
				ok = true;
			}
			else
				pop(lua, 1);
		}
	else
		for (pushz(lua); lua_next(lua, 2); )
		{
			tabgetk(lua, 1, -2);
			if ( popz(lua) )
			{
				tabsetk(lua, 1, -2);
				ok = true;
			}
			else
				pop(lua, 1);
		}
	return push(lua, 1), pushb(lua, ok), 2;
}

// -1* to,... +1 to
static int table_push(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	spush(lua, 1, gettop(lua)-1, false);
	return 1;
}

// -2* to,from... +1 to
static int table_pushs(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	int L = gettop(lua);
	for (int i = 2; i <= L; i++)
		checkArg(lua, i, "", LUA_TTABLE),
		spushs(lua, 1, i, false);
	return push(lua, 1), 1;
}

// -2* values,keys,moreValues... +* value...
static int table_unpack(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	checkArg(lua, 2, "#2", LUA_TTABLE);
	int L = gettop(lua);
	int n = sunpack(lua, 1, 2, false);
	for (int i = 3; i <= L; i++)
		push(lua, i);
	return n+L-2;
}

// -2|5 table,number,first,last,howeq +2 index|-index,table[abs(index)]
static int table_binfind(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	checkArg(lua, 2, "#2", LUA_TNUMBER);
	int n = binfind(lua, 1, luaL_optint(lua, 3, 1), luaL_optint(lua, 4, -1), tonum(lua, 2),
		tonum(lua, 5) < 0 ? -1 : tonum(lua, 5) > 0 ? 1 : 0);
	if (n > 0)
		pushi(lua, n), rawgeti(lua, 1, n);
	else
		pushi(lua, n+1), rawgeti(lua, 1, ~n);
	return 2;
}

// -2|* table,index,n,add... +1|* table或[index].. *1 removed
static int table_replace(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	int n = (int)tolen(lua, 1);
	int i = luaL_optint(lua, 2, 1), m = luaL_optint(lua, 3, 0);
	i < 1 && (i = 1), i > n+1 && (i = n+1);
	m < 0 && (m = 0), m > n-i+1 && (m = n-i+1);
	int a = gettop(lua)-3; a < 0 && (a = 0);
	int nn = n+a-m;
	if (nn < 0)
		error(lua, "length too large");
	if (tobool(lua, upx(1)))
		for (int j = 0; j < m; j++)
			rawgeti(lua, 1, i+j);
	char *tab = (char*)tohead(lua, 1);
	if (nn < *(int*)(tab+24)) // GCtab->asize
	{
		lua_marktable(lua, 1);
		long long *s = *(long long**)(tab+8); // GCtab->array
		memmove(s+i+a, s+i+m, (n-i-m+1)*8);
		if (a < m)
		{
			pushz(lua), rawseti(lua, 1, n);
			for (int j = nn+1; j < n; j++)
				s[j] = s[n];
		}
	}
	else if (a < m)
	{
		int k = i+a;
		for (int j = i+m; j <= n; j++, k++)
			rawgeti(lua, 1, j), rawseti(lua, 1, k);
		while (k <= n)
			pushz(lua), rawseti(lua, 1, k);
	}
	else
		for (int j = n, k = nn; j >= i+m; j--, k--)
			rawgeti(lua, 1, j), rawseti(lua, 1, k);
	for (; a > 0; a--)
		rawsetiv(lua, 1, i+a-1, 3+a);
	return tobool(lua, upx(1)) ? m : (push(lua, 1), 1);
}

/////////////////////////////////// io //////////////////////////////////

// -1|2 filename,mode +1|2 string,err *6 'rb',io.open,'read','*a',io.close,'err'
static int io_readall(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TSTRING);
	checkArgable(lua, 2, "#2", LUA_TSTRING) || (push(lua, upx(1)), 0);
	int L = gettop(lua)+1;
	push(lua, upx(2)), push(lua, 1), push(lua, 2), call(lua, 2, 2); // L file L+1 err
	if (isnil(lua, L))
		return 2;
	tabgetk(lua, L, upx(3)), push(lua, L), push(lua, upx(4)), call(lua, 2, 1); // L+2 string
	push(lua, upx(5)), push(lua, L), call(lua, 1, 0);
	return isnil(lua, -1) ? push(lua, upx(6)), 2 : 1;
}

// -1|3 path,hidden,times +1|2 sizes{name=size|-1other|-2dir},times{name=2000usec}
static int io_dir(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TSTRING);
	settop(lua, 3);
#if WIN
	struct _finddata_t st; intptr_t f;
	char s[MAX_PATH+3];
	if (strcpy_s(s, MAX_PATH, tostr(lua, 1)) || (strcat(s, "\\*"), f = _findfirst(s, &st)) < 0)
		return error(lua, "can not open directory %s", tostr(lua, 1));
	int L = gettop(lua)+1, tz = 0;
	newtable(lua); // L sizes
	tobool(lua, 3) ? newtable(lua) : pushz(lua); // L+1 times|nil
	do
	{
		if (strcmp(st.name, ".")==0 || strcmp(st.name, "..")==0
			|| !tobool(lua, 2) && st.name[0]=='.')
			continue;
		pushs(lua, st.name); // L+2 name
		if (st.attrib & 32)
			pushi(lua, st.size); // L+3 size
		else if (st.attrib & 16)
			pushi(lua, -2); // L+3 -2
		else
			pushi(lua, -1); // L+3 -1
		if (tobool(lua, 3))
			pushi(lua, (int)((unsigned)st.time_write-946684800-tz)), rawsetk(lua, L+1, L+2);
		rawset(lua, L);
	} while (_findnext(f, &st) >= 0);
	_findclose(f);
#else
	char s[8192+1];
	if (tolen(lua, 1) >= 2048)
		error(lua, "path too long");
	char *ss = strcpy(s, tostr(lua, 1)) + strlen(s);
	ss > s && ss[-1] != '/' && (*ss++ = '/'), s[8192] = 0;
	DIR *dir = opendir(tostr(lua, 1));
	if ( !dir)
		return error(lua, "can not open directory %s", tostr(lua, 1));
	struct dirent *e;
	struct stat st;
	int L = gettop(lua)+1;
	newtable(lua); // L sizes
	tobool(lua, 3) ? newtable(lua) : pushz(lua); // L+1 times|nil
	while ((e = readdir(dir)))
	{
		if (strcmp(e->d_name, ".")==0 || strcmp(e->d_name, "..")==0
			|| (!tobool(lua, 2) && e->d_name[0]=='.'))
			continue;
		pushs(lua, e->d_name); // L+2 name
		strncpy(ss, e->d_name, 2048);
		if (stat(s, &st))
			pushs(lua, strerror(errno));
		else if (st.st_mode & S_IFREG)
			pushn(lua, st.st_size); // L+3 size
		else if (st.st_mode & S_IFDIR)
			pushi(lua, -2);	// L+3 -2
		else
			pushi(lua, -1); // L+3 -1
		if (tobool(lua, 3))
#if LINUX
			pushi(lua, st.st_mtime-946684800+timediff),
#else
			pushi(lua, st.st_mtimespec.tv_sec-946684800+timediff),
#endif
			rawsetk(lua, L+1, L+2);
		rawset(lua, L);
	}
	closedir(dir);
#endif
	return tobool(lua, 3) ? 2 : (pop(lua, 1), 1);
}

// -1 filename +2 size,2000usec
static int io_stat(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TSTRING);
#if WIN
	struct _finddata_t st; intptr_t f;
	if ((f = _findfirst(tostr(lua, 1), &st)) < 0)
		return error(lua, "file error %s", strerror(errno));
	_findclose(f);
	pushi(lua, st.size);
	pushi(lua, (int)((unsigned)st.time_write-946684800+timediff));
#else
	struct stat st;
	if (stat(tostr(lua, 1), &st))
		return error(lua, "file error %s", strerror(errno));
	pushn(lua, st.st_size);
#if LINUX
	pushi(lua, st.st_mtime-946684800+timediff);
#else
	pushi(lua, st.st_mtimespec.tv_sec-946684800LL+timediff);
#endif
#endif
	return 2;
}

/////////////////////////////////// base //////////////////////////////////

// -1 data +1 address
static int base_address(lua_State *lua)
{
	return pushn(lua, (size_t)tobody(lua, 1)), 1;
}

// -0|2 meta, size +1 udata
static int base_udata(lua_State *lua)
{
	checkArgableZ(lua, 1, "#1", LUA_TTABLE);
	checkArgableZ(lua, 2, "#2", LUA_TNUMBER);
	newudata(lua, touint(lua, 2)), push(lua, 1), setmeta(lua, -2);
	return 1;
}

// -0|1 unit[1s 0.001ms 0.000001us] +1 2000sec/msec/usec *1 _utc
static int os_utc(lua_State *lua)
{
	double unit = luaL_optnumber(lua, 1, 0.001);
	return pushn(lua, (double)timeNow(unit, true)), 1;
}

// -0|1 unit[1s 0.001ms 0.000001us] +1 2000sec/msec/usec *1 _now
static int os_now(lua_State *lua)
{
	double unit = luaL_optnumber(lua, 1, 0.001);
	return pushn(lua, (double)timeNow(unit, false)), 1;
}

// -0|2 unit[1s 0.001ms 0.000001us],queue +1 2000sec/msec/usec *1 _now
static int base_now(lua_State *lua)
{
	double unit = luaL_optnumber(lua, 1, 0.001);
	if (tobool(lua, 2))
	{
		if (getmetai(lua, global, M_nowq), isnil(lua, -1))
			return 1;
		long long us = (long long)tonum(lua, -1);
		long long uu = d2l(1000000 * unit);
		return pushn(lua, uu > 1 ? us / uu : us), 1;
	}
	if (getmetai(lua, global, M_now), !isnil(lua, -1))
	{
		long long us = (long long)tonum(lua, -1);
		long long uu = d2l(1000000 * unit);
		return pushn(lua, uu > 1 ? us / uu : us), 1;
	}
	return pushn(lua, (double)timeNow(unit, false)), 1;
}

// -* +* *1 setfenv
static int base_setfenv(lua_State *lua)
{
	if ( !isfunc(lua, 1) && mustint(lua, 1) < 1)
		return error(lua, "can't set thread environment");
	push(lua, upx(1)), lua_insert(lua, 1);
	return calln(lua, gettop(lua)-1);
}

// -2 func,meta +0 *2 debug.getmetatable,debug.setmetatable
static int debug_setmeta(lua_State *lua)
{
	if (tonum(lua, 3)==176824)
	{
		if (isnil(lua, 4))
			return push(lua, upx(1)), push(lua, 1), call(lua, 1, 1), 1;
		else
			return push(lua, upx(2)), push(lua, 1), push(lua, 4), call(lua, 2, 0), 0;
	}
	checkArg(lua, 1, "#1", LUA_TFUNCTION);
	checkArgableZ(lua, 2, "#2", LUA_TTABLE);
	push(lua, upx(2)), push(lua, 1), push(lua, 2),
		call(lua, 2, 0);
	return 0;
}

// -1 func +* names
static int debug_getargs(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TFUNCTION);
	const char *names[ARGSIZE];
	int n = getArgs(lua, 1, names);
	for (int i = 0; i < n; i++)
		lua_refstr(lua, names[i]);
	return n;
}

// -2 func,index *1 getupvalue
static int debug_getupv(lua_State *lua)
{
	if (tocfunc(lua, 1))
		return pushz(lua), 1;
	push(lua, upx(1)), lua_insert(lua, 1);
	call(lua, gettop(lua)-1, LUA_MULTRET);
	return gettop(lua);
}

// -3 func,index,upv *1 setupvalue
static int debug_setupv(lua_State *lua)
{
	if (tocfunc(lua, 1))
		return error(lua, "can NOT set upvalue of c function"), 0;
	push(lua, upx(1)), lua_insert(lua, 1);
	call(lua, gettop(lua)-1, LUA_MULTRET);
	return gettop(lua);
}

// -2 func|level,index *1 getlocal
static int debug_getlocal(lua_State *lua)
{
	if ( !isfunc(lua,1) && !isnum(lua, 1))
		return errArgT(lua, "#1", "function or number", 1);
	if (isnum(lua, 1))
	{
		mustint(lua, 1);
		int i = toint(lua, 1);
		lua_Debug d;
		if (lua_getstack(lua, i, &d) && lua_getinfo(lua, "f", &d))
		{
			if (tocfunc(lua, -1))
				return pushz(lua), 1;
			else
				pop(lua, 1);
		}
		pushi(lua, i+1), lua_replace(lua, 1);
	}
	push(lua, upx(1)), lua_insert(lua, 1);
	call(lua, gettop(lua)-1, LUA_MULTRET);
	return gettop(lua);
}

// -3 func|level,index,value *1 setlocal
static int debug_setlocal(lua_State *lua)
{
	if (tocfunc(lua, 1))
		return error(lua, "can NOT set local of c function"), 0;
	if ( !isfunc(lua,1) && !isnum(lua, 1))
		return errArgT(lua, "#1", "function or number", 1);
	if (isnum(lua, 1))
	{
		mustint(lua, 1);
		int i = toint(lua, 1);
		lua_Debug d;
		if (lua_getstack(lua, i, &d) && lua_getinfo(lua, "f", &d))
		{
			if (tocfunc(lua, -1))
				error(lua, "can NOT set local of c function");
			else
				pop(lua, 1);
		}
		pushi(lua, i+1), lua_replace(lua, 1);
	}
	push(lua, upx(1)), lua_insert(lua, 1);
	call(lua, gettop(lua)-1, LUA_MULTRET);
	return gettop(lua);
}

#if WIN
#define snprintf _snprintf
#endif

#define MAX_LOGALLOC_NAME 32
struct lognode
{
	int			line;
	char		name[MAX_LOGALLOC_NAME];
	char		source[ MAX_LOGALLOC_NAME ];
	int			count;
	int			size;
	lognode*	next;

	lognode( int l, const char* n, const char* s, int ss )
	{
		Set( l, n, s, ss );
	}

	inline void Set( int l, const char* n, const char* s, int ss )
	{
		line = l;
		::strncpy( name, n, MAX_LOGALLOC_NAME - 1 ), name[ MAX_LOGALLOC_NAME - 1 ] = 0;
		::strncpy( source, s, MAX_LOGALLOC_NAME - 1 ), name[ MAX_LOGALLOC_NAME - 1 ] = 0;
		count = 1;
		size = ss;
		next = NULL;
	}
};

lognode* gLogUsed = NULL;
lognode* gLogFree = NULL;
unsigned int gLogMax = 0;

static void logalloc_init( unsigned int max )
{
	gLogMax = max;

	if ( gLogUsed == NULL && gLogFree == NULL )
	{
		gLogFree = (lognode*) malloc( max * sizeof( lognode ) );
		memset( gLogFree, 0 , max * sizeof( lognode ) );
		lognode* tmp = gLogFree;
		for ( unsigned int i = 0; i < max - 1; i ++ )
		{
			tmp->next = tmp + 1;
			tmp = tmp->next;
		}
	}
}

static void logalloc( int size, const char *name, const char *source, int line )
{
	char s[102];
	snprintf(s, sizeof(s)-1, "~ALLOC %d %s %s:%d~", size, name, source, line);
	s[sizeof(s)-1] = 0;
	logErr(s);
}

static void logalloc2( int size, const char *name, const char *source, int line )
{
	lognode* tmp = gLogUsed;
	lognode* pre = NULL;
	while ( tmp != NULL )
	{
		if ( tmp->line == line && ::strncmp( tmp->name, name,  MAX_LOGALLOC_NAME - 1 ) == 0 && ::strncmp( tmp->source, source,  MAX_LOGALLOC_NAME - 1 ) == 0 )
		{
			tmp->size += size;
			tmp->count ++;
			return;
		}

		pre = tmp;
		tmp = tmp->next;
	}

	if ( gLogFree != NULL )
	{
		tmp = gLogFree;
		gLogFree = gLogFree->next;

		tmp->Set( line, name, source, size );
	}
	else
	{
		if ( gLogMax != 0 )
			return;

		tmp = new lognode( line, name, source, size );
	}

	if ( pre != NULL )
		pre->next = tmp;
	else
		gLogUsed = tmp;
}

static void dumplogalloc( )
{
	logErr( "Dump logalloc begin" );
	while ( gLogUsed != NULL )
	{
		char s[102];
		snprintf( s, sizeof(s) - 1, "%s %s %d: %d %d", gLogUsed->name, gLogUsed->source, gLogUsed->line, gLogUsed->size, gLogUsed->count );
		s[ sizeof(s) - 1 ] = 0;
		logErr( s );

		lognode* tmp = gLogUsed;
		gLogUsed = gLogUsed->next;

		tmp->next = gLogFree;
		gLogFree = tmp;
	}

	logErr( "Dump logalloc end" );
}

// -1 bool +0
static int debug_logalloc(lua_State *lua)
{
	lua_logalloc = tobool(lua, 1) ? logalloc : NULL;
	return 0;
}

static int debug_logalloc2(lua_State *lua)
{
	unsigned int m = touint( lua, 1 );
	if ( m == 0 )
		lua_logalloc = NULL;
	else if ( m == 1 )
		dumplogalloc( );
	else if ( m == 2 )
		lua_logalloc = logalloc2, gLogMax = 0;
	else
		lua_logalloc = logalloc2, logalloc_init( m );

	return 0;
}

static unsigned maxalloctab = 0;
static std::unordered_map<std::string, unsigned> _logtabs;

static bool cmp(const std::pair<std::string, unsigned> a, const std::pair<std::string, unsigned> b)
{
	return a.second > b.second;
}

static void logalloctab(const char *name, const char* source, int line)
{
	char buffer[2048];
	sprintf(buffer, "%s:%d:%s\0", name, line, source);
	std::string s = buffer;
	if (_logtabs.find(s) != _logtabs.end())
		_logtabs.at(s) += 1;
	else
		_logtabs.insert(std::make_pair(s, 1));
	if (_logtabs[s] >= maxalloctab)
	{
		std::vector<std::pair<std::string, unsigned>> tabs;
		for (auto it = _logtabs.cbegin(); it != _logtabs.cend(); it++)
			tabs.push_back(std::make_pair(it->first, it->second));
		//std::sort(tabs.cbegin(), tabs.size(), sizeof(std::pair<std::string, unsigned>),
		std::sort(tabs.begin(), tabs.end(), cmp);
		for (auto it = _logtabs.cbegin(); it != _logtabs.cend(); it++) {
			char s[2048];
			sprintf(s, "%s|%d\0", it->first.c_str(), it->second);
			logErr(s);
		}
		_logtabs.clear();
	}
}

static int debug_logalloctab(lua_State* lua)
{
	unsigned num = touint(lua, 1);
	if (num == 0) {
		maxalloctab = 0;
		lua_logtable = NULL;
	}
	else {
		maxalloctab = num;
		lua_logtable = logalloctab;
	}
	return 0;
}


static int lz4_compress(lua_State* lua)
{
	size_t size; const char *s = tobytes(lua, 1, &size, "#1");
	const int maxsize =  LZ4_compressBound(size);   // 计算压缩后的最大大小，以便为压缩后的数据分配足够的空间
	char *dest = (char *)newbdata(lua, maxsize);
	const int ret =  LZ4_compress_default(s, dest, size, maxsize);   // 返回压缩后的数据大小
	if (ret <= 0)
		error(lua, "lz4 compress failed, exit code : %d ", ret);
	return lua_userdatalen(dest, ret), 1;
}

static int lz4_decompress(lua_State* lua)
{
	size_t n; const char *s = tobytes(lua, 1, &n, "#1");
	size_t size = toint(lua, 2);
	char *dest = (char *)newbdata(lua, size);
	const int ret =  LZ4_decompress_safe(s, dest, n, size);
	if (ret <= 0)
		error(lua, "lz4 decompress failed, exit code : %d ", ret);
	return lua_userdatalen(dest, ret), 1;
}

/////////////////////////////////////// main //////////////////////////////

void power_loop(void *luaState)
{
	lua_State* lua = (lua_State*) luaState;
#if WIN || __arm__
	for (int i = 0; i < 20; i++)
		net_loop(lua, true), net_loop(lua, false),
		queue_pcall(lua, timeNow(0, false)) && timelast++; // since no microsecond
#else
	for (long long now = timeNow(0, false), due = 0; due < 2000; )
	{
		net_loop(lua, true), net_loop(lua, false);
		long long net = now; now = timeNow(0, false), net = now - net;
		long long q = now;
		while (queue_pcall(lua, now) &&
			(timelast++, (now = timeNow(0, false)) - q) < net)
			;
		due += net + -q + now;
	}
#endif
#if !LINUX && SERVER
	usleep(5000);
#endif
}

void power_init(void *luaState )
{
	lua_State* lua = (lua_State*) luaState;
	LUAJIT_VERSION_SYM();

	int L = gettop(lua);
	push(lua, global); int G = L+1; // G
	newtable(lua); // L+1 G L+2 readonly
	newmetan(lua, -1, 20, 4); int M = L+3;
	rawsetiv(lua, M, M_G, G);
	rawsetnv(lua, M, "__index", G);
	rawsetnv(lua, M, "__newindex", G);
	push(lua, L+2), lua_replace(lua, global);

	if ((rawgetn(lua, G, "jit"), !popifz(lua, 1))
		&& (rawgetn(lua, -1, "opt"), !popifz(lua, 2))
		&& (rawgetn(lua, -1, "start"), !popifz(lua, 3)))
		pushi(lua, 2), call(lua, 1, 0), pop(lua, 2); // jit.opt.start(2)

	pushz(lua), rawsetn(lua, G, "newproxy");
	pushc(lua, base_int), rawsetn(lua, G, "int");
	pushc(lua, base_int32), rawsetn(lua, G, "int32");
	pushc(lua, base_toint), rawsetn(lua, G, "toint");
	pushc(lua, base_toint32), rawsetn(lua, G, "toint32");
	pushc(lua, base_tostring), rawsetn(lua, G, "_tostring");
	pushc(lua, base_not), rawsetn(lua, G, "_not");
	pushc(lua, base_and), rawsetn(lua, G, "_and");
	pushc(lua, base_or), rawsetn(lua, G, "_or");
	pushc(lua, base_xor), rawsetn(lua, G, "_xor");
	pushc(lua, base_lshift), rawsetn(lua, G, "_lshift");
	pushc(lua, base_rshift), rawsetn(lua, G, "_rshift");
	pushc(lua, base_arshift), rawsetn(lua, G, "_arshift");

	rawgetn(lua, G, "table");
	pushz(lua), rawsetn(lua, -2, "getn");
	pushc(lua, table_sub), rawsetn(lua, -2, "sub");
	pushc(lua, table_copy), rawsetn(lua, -2, "copy");
	pushc(lua, table_duplicate), rawsetn(lua, -2, "duplicate");
	pushc(lua, table_clear), rawsetn(lua, -2, "clear");
	pushc(lua, table_new), rawsetn(lua, -2, "new");
	pushc(lua, table_size), rawsetn(lua, -2, "size");
	pushc(lua, table_append), rawsetn(lua, -2, "append");
	pushc(lua, table_push), rawsetn(lua, -2, "push");
	pushc(lua, table_pushs), rawsetn(lua, -2, "pushs");
	pushc(lua, table_unpack), rawsetn(lua, -2, "unpack");
	pushc(lua, table_binfind), rawsetn(lua, -2, "binfind");
	pushb(lua, false), pushcc(lua, table_replace, 1), rawsetn(lua, -2, "replace");
	pushb(lua, true), pushcc(lua, table_replace, 1), rawsetn(lua, -2, "splice");

	pushc(lua, base_udata), rawsetn(lua, G, "_udata");
	pushc(lua, base_address), rawsetn(lua, G, "_address");
	pushc(lua, lz4_compress), rawsetn(lua, G, "lz4_compress");
	pushc(lua, lz4_decompress), rawsetn(lua, G, "lz4_decompress");
	if (rawgetn(lua, G, "io"), !isnil(lua, -1))
	{
		//pushz(lua), rawsetn(lua, -2, "popen"); // io.popen=nil
		pushs(lua, "rb"), rawgetn(lua, -2, "open"), pushs(lua, "read"), pushs(lua, "*a"),
			rawgetn(lua, -5, "close"), pushs(lua, "can not read file"),
			pushcc(lua, io_readall, 6), rawsetn(lua, -2, "readall");
		pushc(lua, io_dir), rawsetn(lua, -2, "dir");
		pushc(lua, io_stat), rawsetn(lua, -2, "stat");
		if (rawgetn(lua, -1, "stdout"), getmeta(lua, -1))
			rawseti(lua, M, M_file);
	}
	newtable(lua), rawsetnv(lua, G, "os", -1); // os={}
	rawgetn(lua, G, "_utc"), pushcc(lua, os_utc, 1), rawsetn(lua, -2, "utc");
	rawgetn(lua, G, "_now"), pushcc(lua, os_now, 1), rawsetn(lua, -2, "now");
	rawgetn(lua, G, "_now"), pushcc(lua, base_now, 1), rawsetn(lua, G, "_now");

	rawgetn(lua, G, "debug");
	rawgetn(lua, -1, "traceback"), rawseti(lua, M, M_onerror); // meta[onerror]=onerror TODO traceback(notstring)
	pushz(lua), rawsetn(lua, -2, "getregistry"); // debug.getregistry=nil
	pushz(lua), rawsetn(lua, -2, "setfenv"); // debug.setfenv=nil
	rawgetn(lua, -1, "getmetatable"), rawgetn(lua, -2, "setmetatable"),
		pushcc(lua, debug_setmeta, 2), rawsetn(lua, -2, "setmeta");
	pushz(lua), rawsetn(lua, -2, "getmetatable"); // debug.getmetatable=nil
	pushz(lua), rawsetn(lua, -2, "setmetatable"); // debug.setmetatable=nil
	pushc(lua, debug_getargs), rawsetn(lua, -2, "getargs");
	rawgetn(lua, -1, "getupvalue"), pushcc(lua, debug_getupv, 1), rawsetn(lua, -2, "getupvalue");
	rawgetn(lua, -1, "setupvalue"), pushcc(lua, debug_setupv, 1), rawsetn(lua, -2, "setupvalue");
	rawgetn(lua, -1, "getlocal"), pushcc(lua, debug_getlocal, 1), rawsetn(lua, -2, "getlocal");
	rawgetn(lua, -1, "setlocal"), pushcc(lua, debug_setlocal, 1), rawsetn(lua, -2, "setlocal");
	pushc(lua, debug_logalloc), rawsetn(lua, -2, "logalloc");
	pushc(lua, debug_logalloc2), rawsetn(lua, -2, "logalloc2");
	pushc(lua, debug_logalloctab), rawsetn(lua, -2, "logalloctab");
	pushc(lua, lua_logrefs), rawsetn(lua, -2, "logrefs");

	datime(lua, G);
	string(lua, M, G);
	codec(lua, M, G);
	queue(lua, M, G);
	rawgetn(lua, G, "_enqueue"), push(lua, G), pushz(lua), pushcc(lua, event, 3);
	rawsetn(lua, G, "_define");
	remote(lua, M, G);
	network(lua, M, G);


	sql(lua, M, G);
	mysql(lua, M, G);
	luaopen_bn(lua);


	zip(lua, M, G);

	rawgetn(lua, G, "setfenv"), pushcc(lua, base_setfenv, 1);
	rawsetn(lua, G, "setfenv");

	luaopen_conf_core(lua);
	luaopen_path(lua);
	settop(lua, L);
}
