#define NAMESIZE 40
#define CONDSIZE (250*32)

typedef struct
{
	unsigned s[(CONDSIZE+31)>>5]; // [0, n+31>>5)
} Cond;

inline static void condTrue(Cond *to, int x)
{
	to->s[x>>5] |= 1<<(x&31);
}
inline static void condFalse(Cond *to, int x)
{
	to->s[x>>5] &= ~(1<<(x&31));
}
#define condFill(to,v,n) (memset((to)->s, v, ((n)+31)>>5<<2))
#define condCopy(to,from,n) (memcpy((to)->s, (from)->s, ((n)+31)>>5<<2))
inline static void condCut(Cond *to, int n)
{
	n-- > 0 && (to->s[n>>5] &= ~(-2<<(n&31)));
}
inline static void condAnd(Cond *to, Cond *from, int n)
{
	for (int i = 0, m = (n+31)>>5; i < m; i++)
		to->s[i] &= from->s[i];
}
inline static void condOr(Cond *to, Cond *from, int n)
{
	for (int i = 0, m = (n+31)>>5; i < m; i++)
		to->s[i] |= from->s[i];
}
inline static void condAndNot(Cond *to, Cond *from, int n)
{
	for (int i = 0, m = (n+31)>>5; i < m; i++)
		to->s[i] &= ~from->s[i];
}
inline static bool condCommon(Cond *a, Cond *b, int n)
{
	unsigned common = 0;
	for (int i = 0, m = (n+31)>>5; i < m && !common; i++)
		common = a->s[i] & b->s[i];
	return !!common;
}
Cond COND0;

typedef struct Info
{
	int argn;
	const char *names[ARGSIZE+1]; // func name, arg1 name ...
	bool args; // if last arg is _args
	const void *func;
} Info;

// events { name = attach { arg|_define={ value=Cond, [self]=noCond },
//                          1=false|preCond, 2=info1 ... func1=info1, func2=info2 ...
//							-1=whereDef, -2=originalDefine }
//          .meta = define { arg=default },
//          Call = name }
// groups { key=true }
//          .meta = skip { key=true }

#define whereDef(lua,attach) (rawgeti(lua, attach, -1), pops(lua))

/////////////////////////////// call //////////////////////////////

inline static bool Call_udata(lua_State *lua, int T, int A)
{
	int at = totype(lua, A);
	if ((at != LUA_TTABLE && at != LUA_TUSERDATA) || !getmeta(lua, A)) // A.meta
		return false;
	getmeta(lua, T); // T.meta
	if (raweq(lua, -1, -2)) // T.meta == A.meta
		return pop(lua, 2), true;
	rawget(lua, -2); // A.meta[T.meta]
	if (tobool(lua, -1))
		return pop(lua, 2), true;
	return pop(lua, 2), false;
}

#define Call_stop(lua,index) (-range(\
	isbool(lua, index) ? (int)(0x7FFFFffff + tobool(lua, index)) : toint(lua, index), 0, 2))

static void Call_type(lua_State *lua, int L, double *usec, int *stop, bool *defs)
{
	if (rawgetk(lua, L, upx(4)), !popifz(lua, 1))
		(*usec = queue_delay(lua)) < 0 && (*usec = 0),
		pushz(lua), rawsetk(lua, L, upx(4)); // args._delay=nil
	if (rawgetk(lua, L, upx(7)), !popifz(lua, 1)) // args._stop
		*stop = Call_stop(lua, -1), pop(lua, 1);
	if (rawgetk(lua, L, upx(9)), !popifz(lua, 1)) // args._defines
		*defs = popb(lua);
	// define
	int LL = gettop(lua);
	for (pushz(lua); lua_next(lua, L); pop(lua, 1)) // LL+1 arg LL+2 value
	{
		if ( !isstr(lua, LL+1))
			return (void)werrType(lua, tostr(lua, L+2), "bad argument name", LUA_TSTRING, totype(lua, -2));
		if (tostr(lua, LL+1)[0] == '_') // args._*
			continue;
		rawgetk(lua, L+1, LL+1); // LL+3 define[arg]
		int t = totype(lua, LL+3), at = totype(lua, LL+2);
		if ( !t)
			return (void)werror(lua, tostr(lua, L+2), "argument %s undefined", tostr(lua, LL+1));
		else if (t != LUA_TUSERDATA)
			if (t == at); else
				return (void)werrArg(lua, tostr(lua, L+2), tostr(lua, LL+1), t, at);
		else if ( !Call_udata(lua, LL+3, LL+2))
			return (void)werror(lua, tostr(lua, L+2),
				"bad argument %s (different metatable)", tostr(lua, LL+1));
		double V; long long v;
		if (t == LUA_TNUMBER && (V = tonum(lua, -2), V != V || ( (v = (long long)V), v != v<<10>>10)))
			return (void)werror(lua, tostr(lua, L+2), // whereDef
				"bad argument %s (number %f out of signed 54bit range)", tostr(lua, -3), V);
		pop(lua, 1);
	}
}

// -1 {arg} +1|* usec|*
// *13 attach{},name,enqueue,'_delay',groups,'_define','_stop','_order','_defines','_try','_slow','_slows','_slown'
static int Call(lua_State *lua)
{
	int L = totype(lua, 1);
	if (L != LUA_TTABLE)
		return rawgeti(lua, upx(1), -1), werrArg(lua, pops(lua), "#1", LUA_TTABLE, L);
	L = gettop(lua);
	if (L != 1)
		return error(lua, "too many arguments");
	settop(lua, L = 1); // L {arg}
	getmeta(lua, upx(1)); // L+1 define{}
	rawgeti(lua, upx(1), -1); // L+2 whereDef=attach[-1]
	rawgeti(lua, upx(1), -2); // L+3 originalDefine=attach[-2]

	// types
	double delay = -1; bool defs = false;
	int stop = -2; // call if order < -stop (i.e. -order > stop)
	Call_type(lua, L, &delay, &stop, &defs);
	if (delay >= 0)
	{
		push(lua, upx(3)), pushn(lua, delay), luaL_where(lua, 1),
		push(lua, upx(2)), push(lua, L), call(lua, 4, 1); // enqueue(delay,where,name,arg)
		return 1;
	}
	// defaults
	for (pushz(lua); lua_next(lua, L+1); )
		rawgetk(lua, L, -2), // arg default
		popz(lua) ? rawsetk(lua, L, -2) : pop(lua, 1);

	if (rawgetk(lua, L, upx(10)), popb(lua) && (rawgetk(lua, upx(1), upx(10)), popz(lua)))
		return 0; // no func on _try, all funcs are false

	int n = (int)(tolen(lua, upx(1))-1);
	Cond is; condFill(&is, -1, n);
	// when args
	for (pushz(lua); lua_next(lua, L);) // arg value
	{
		if (rawgetk(lua, upx(1), -2), popifz(lua, 2)) // attach[arg]
			continue; // no condition on this arg, all funcs are true
		if (rawgetk(lua, -1, -2), popifz(lua, 1)) // attach[arg][value]
		{
			if (raweq(lua, -3, upx(6))) // _define
				return 0; // no func on this group, all funcs are false
			else
				rawgetk(lua, -1, -1); // no condition on this value, no condition funcs are true
		}
		condAnd(&is, (Cond*)toudata(lua, -1), n); // is &= attach[arg][value|self]
		pop(lua, 3);
	}
	if (n > 0)
	{
		getmeta(lua, upx(5)), rawgetk(lua, upx(1), upx(6)); // skips, attach._define
		for (pushz(lua); lua_next(lua, -3); )
		{
			pop(lua, 1), rawgetk(lua, -2, -1); // skip, attach._define[skip]
			Cond *cond = (Cond*)popudata(lua);
			cond && (condAndNot(&is, cond, n), 0); // is &= ~attach._define[skip]
		}
		condCut(&is, n), pop(lua, 2);
	}
	rawgeti(lua, upx(1), 1); Cond *pre = (Cond*)popudata(lua); // pre=attach[1]

	if (defs)
	{
		rawgetk(lua, upx(1), upx(6)); // attach._define
		newtablen(lua, 0, 10); // defs
		if (stop == 0 || (stop == -1 && !pre))
			return 1;
		if (stop == -1)
			condAnd(&is, pre, n);
		for (pushz(lua); lua_next(lua, -3); )
			if (condCommon(&is, (Cond*)popudata(lua), n))
				pushb(lua, true), rawsetk(lua, -3, -2); // defs[_define]=true
		return 1;
	}

	int slow = (rawgetk(lua, L+3, upx(11)), popint(lua));
	int slows = (rawgetk(lua, L+3, upx(12)), popint(lua));
	int slown = (rawgetk(lua, L+3, upx(13)), popint(lua));
	long long times = slow || slows > 0 ? timeNow(0, true) : 0;
	long long time = times, due, attn = 0;
	// call
	int rn = 0;
	for (int p = pre ? 0 : (pre = &COND0, -1); p > stop; p--) // order == -p
	for (int i = 0; i < (n+31)>>5; i++)
	for (int I = is.s[i]&(pre->s[i]^p), j = i<<5; I; I = (unsigned)I>>1, j++)
		if (I & 1)
		{
			rawgeti(lua, upx(1), j+2); Info *info = (Info*)popudata(lua); // info
			lua_reffunc(lua, info->func); // func
			for (int x = 1; x <= info->argn; x++)
				lua_refstr(lua, info->names[x]), rawget(lua, L); // arg[names[x]]
			if (info->args) // _args
			{
				push(lua, L), rn += calln(lua, info->argn+1);
				if (rawgetk(lua, L, upx(7)), !popifz(lua, 1)) // _args._stop
					if (stop = Call_stop(lua, -1), pop(lua, 1), p <= stop)
						n = -1; // to End
			}
			else
				rn += calln(lua, info->argn);
			attn++;
			if (slow > 0 && (due = time, time = timeNow(0, true), due = time - due) > slow)
			{
				lua_Debug de;
				lua_reffunc(lua, info->func), lua_getinfo(lua, ">S", &de);
				printf("INFO slow event %s %s:%d %lld usec\n", tostr(lua, upx(2)),
					de.source[0]=='@' ? de.source+1 : de.source, de.linedefined, due);
			}
			if (n < 0)
				goto End;
		}
	End:
	if (slown > 0 && attn >= slown)
		printf("WARN slow event %s %lld attachs\n", tostr(lua, upx(2)), attn);
	if (slows > 0 && (due = timeNow(0, true) - times) > slows)
		printf("WARN slow event %s %lld usec\n", tostr(lua, upx(2)), due);
	return rn;
}

/////////////////////////////// define //////////////////////////////

// -1 {arg=type/default} +0
// *5 events{1=nil>nil|whereSingle,3=attachSingle},name,enqueue,_G,groups
static int define_define(lua_State *lua)
{
	checkArg(lua, -1, "-1", LUA_TTABLE);
	rawgeti(lua, upx(1), 1);
	if ( !popifz(lua, 1))
		return error(lua, "%sincomplete 'define' or 'when'", tostr(lua, -1));
	int L = gettop(lua); // L {arg}
	newtablen(lua, 1, 10); // L+1 attach{}
	newtablen(lua, 1, 10); // L+2 define{}
	push(lua, L+2), setmeta(lua, L+1); // attach.meta = define
	int x = 0;
	for (pushz(lua); lua_next(lua, L); x++)
	{
		if ( !isstr(lua, -2))
			return errType(lua, "bad argument name", LUA_TSTRING, totype(lua, -2));
		if (x > ARGSIZE)
			return error(lua, "too many arguments");
		const char *arg = tostr(lua, -2); // check valid arg name
		if ((unsigned)(strlen(arg)-1) >= NAMESIZE)
			return error(lua, "argument name %s too long", arg);
		if (arg[0] == '_')
		{
			rawsetk(lua, L+2, -2); // define[arg]=default
			continue;
		}
		int t = totype(lua, -1);
		if (t != LUA_TBOOLEAN && t != LUA_TNUMBER && t != LUA_TSTRING && t != LUA_TTABLE
			&& (t != LUA_TUSERDATA || !popif(lua, getmeta(lua, -1), 1))
			&& t != LUA_TFUNCTION)
			return error(lua, "bad type of argument %s (got %s)", arg, lua_typename(lua, t));
		double V; long long v;
		if (t == LUA_TNUMBER && (V = tonum(lua, -1), V != V || ( (v = (long long)V), v != v<<10>>10)))
			return error(lua, "bad argument %s (number %f out of signed 54bit range)", arg, V);
		rawsetk(lua, L+2, -2); // define[arg]=default
	}
	luaL_where(lua, 1), rawseti(lua, L+1, -1); // attach[-1]=whereDef
	rawsetiv(lua, L+1, -2, L); // attach[-2]=originalDefine
	pushb(lua, false), rawseti(lua, L+1, 1); // attach[1]=false
	if (isnil(lua, upx(2)))
		luaL_where(lua, 1), rawseti(lua, upx(1), 1), // events[1]=whereSingle
		rawsetiv(lua, upx(1), 3, L+1); // events[3]=attach{}single
	else
	{
		rawsetkv(lua, upx(1), upx(2), L+1); // events[name] = attach{}
		if (rawgetk(lua, upx(4), upx(2)), isnil(lua, -1)) // _G[name]
			push(lua, L+1), push(lua, upx(2)), push(lua, upx(3)), pushs(lua, "_delay"),
			push(lua, upx(5)), pushs(lua, "_define"), pushs(lua, "_stop"),
			pushs(lua, "_order"), pushs(lua, "_defines"), pushs(lua, "_try"),
			pushs(lua, "_slow"), pushs(lua, "_slows"), pushs(lua, "_slown"),
			pushcc(lua, Call, 13), rawsetkv(lua, upx(1), -1, upx(2)), // events[call]=name
			rawsetk(lua, upx(4), upx(2)); // _G[name]=call
		else if (tocfunc(lua, -1) != Call)
			return error(lua, "inconsistent global %s", tostr(lua, upx(2)));
	}
	return 0;
}

// -1 name +1 define_define *4 events{1=nil},enqueue,_G,groups
static int define_name(lua_State *lua)
{
	checkArg(lua, -1, "-1", LUA_TSTRING);
	rawgeti(lua, upx(1), 1);
	if ( !popifz(lua, 1))
		return error(lua, "%sincomplete 'define' or 'when'", tostr(lua, -1));
	const char *name = tostr(lua, -1);
	if (strlen(name) > NAMESIZE)
		return error(lua, "name too long");
	if (rawgetk(lua, upx(1), -1), !popifz(lua, 1)) // events[name]
		return werror(lua, whereDef(lua, -1), "duplicate define %s", name);
	push(lua, upx(1)), push(lua, -2), push(lua, upx(2)), push(lua, upx(3)), push(lua, upx(4));
	pushcc(lua, define_define, 5);
	return 1;
}

/////////////////////////////////// attach //////////////////////////////////

// -1 {arg=value} +0 *1 events{1=nil>whereWhen,2=when}
static int attach_when(lua_State *lua)
{
	checkArg(lua, -1, "-1", LUA_TTABLE);
	rawgeti(lua, upx(1), 1);
	if ( !popifz(lua, 1))
		return error(lua, "%sincomplete 'define' or 'when'", tostr(lua, -1));
	rawsetiv(lua, upx(1), 2, -1);
	luaL_where(lua, 1), rawseti(lua, upx(1), 1);
	return 0;
}

// -2 name,func +0
// *13 events{1=whereWhen|whereSingle,2=nil|when,3=attachSingle},
//      enqueue,_G,groups,'_args','_define','_stop','_order','_defines','_try','_slow','_slows','_slown'
static int attach_attach(lua_State *lua)
{
	checkArg(lua, -2, "-2", LUA_TSTRING);
	const char *name = tostr(lua, -2);
	if ( !lua_isfunction(lua, -1))
		return error(lua, "global is readonly");
	rawgeti(lua, upx(1), 1);
	const char *where = pops(lua);
	if ( !where)
		return error(lua, "missing 'define' or 'when'");
	int L = gettop(lua)+1;
	rawgeti(lua, upx(1), 2); // L when=events[2]
	rawgetk(lua, upx(1), L-2); // L+1 attach=events[name]
	bool single = isnil(lua, L);
	if (single)
		if ( !isnil(lua, L+1))
			return werror(lua, whereDef(lua, L+1), "duplicate define %s", name);
		else if ((unsigned char)name[0]-'A' <= 'Z'-'A')
			return error(lua, "name of single define can't be uppercase");
		else
			// L+1 attach = events[name] = events[3]
			rawgeti(lua, upx(1), 3), lua_replace(lua, L+1), rawsetkv(lua, upx(1), L-2, L+1);
	else if (isnil(lua, L+1))
		return error(lua, "%s undefined", name);
	int n = (int)(tolen(lua, L+1)-1); // numbers of attach
	if (n >= CONDSIZE)
		return error(lua, "too many %s", name);
	getmeta(lua, L+1); // L+2 define=attach.meta
	for (int x = 1; x <= 3; x++)
		pushz(lua), rawseti(lua, upx(1), x); // events[1,5]=nil

	// info
	Info *info = (Info*)newudata(lua, sizeof(Info)); // L+3 Info
	info->names[0] = name, info->args = false;
	info->func = tobody(lua, L-1);
	info->argn = getArgs(lua, L-1, info->names+1);
	for (int x = 1; x <= info->argn; x++)
	{
		lua_refstr(lua, info->names[x]);
		if (raweq(lua, -1, upx(5))) // _args
			x <= --info->argn && error(lua, "_args must be the last argument"),
			info->args = true, pop(lua, 1);
		else if (tostr(lua, -1)[0] == '_') // _*
			pop(lua, 1);
		else if (rawget(lua, L+2), popz(lua)) // check define[arg]
			return werror(lua, whereDef(lua, L+1),
				"argument %s undefined", info->names[x]);
	}
	if (single)
	{
		int m = 0;
		for (pushz(lua); lua_next(lua, L+2); pop(lua, 1))
			m++;
		if (m > info->argn)
			return werror(lua, whereDef(lua, L+1), "%d unused arguments", m - info->argn);
	}

	rawgeti(lua, L+1, 1); Cond *pre = (Cond*)popudata(lua); // preCond=attach[1]

	pre && (condFalse(pre, n), 0); // attach[1][n]=false
	bool whendef = false;
	// when arg
	if ( !isnil(lua, L))
		for (pushz(lua); lua_next(lua, L); ) // arg value
	{
		if ( !isstr(lua, -2))
			return werrType(lua, whereDef(lua, L+1), "bad condition name",
				LUA_TSTRING, totype(lua, -2));
		if (raweq(lua, -2, upx(8))) // _order
		{
			if (toint(lua, -1) <= 0)
			{
				if ( !pre)
					pre = (Cond*)condFill((Cond*)newudata(lua, sizeof(Cond)), 0, CONDSIZE),
					rawseti(lua, L+1, 1); // attach[1]=preCond
				condTrue(pre, n); // attach[1][n]=true
			}
			pop(lua, 1);
			continue;
		}
		if (raweq(lua, -2, upx(6))) // _define
		{
			pushb(lua, whendef = true), rawsetk(lua, upx(4), -2); // group[when._define] = true
			pop(lua, 1);
			continue;
		}
		bool Try = raweq(lua, -2, upx(10));
		if (Try) // _try
		{
			if ( !tobool(lua, -1))
			{
				pop(lua, 1);
				continue;
			}
		}
		else if (rawgetk(lua, L+2, -2), popz(lua)) // check define[arg]
			return werror(lua, whereDef(lua, L+1), "condition %s undefined",
				tostr(lua, -2));
		if (rawgetk(lua, L+1, -2), popifz(lua, 1)) // attach[arg]==nil?
		{
			newtablen(lua, 0, 7), rawsetkv(lua, L+1, -3, -1); // attach[arg] = {}
			condFill((Cond*)newudata(lua, sizeof(Cond)), Try ? 0 : -1, CONDSIZE);
			rawsetk(lua, -2, -2); // attach[arg][self]=cond, all true | all false for _try
			if (Try)
				pushb(lua, false), condFill((Cond*)newudata(lua, sizeof(Cond)), -1, CONDSIZE),
				rawset(lua, -3); // attach[arg][false]=cond, all true for _try
		}
		if (Try)
		{
			rawgetk(lua, -1, -1), condTrue((Cond*)toudata(lua, -1), n); // attach[arg][self][n]=true
			pop(lua, 3);
			continue;
		}
		for (pushz(lua); lua_next(lua, -2); pop(lua, 1))
			condFalse((Cond*)toudata(lua, -1), n); // attach[arg][exist value][n]=false
		if (rawgetk(lua, -1, -2), popifz(lua, 1)) // attach[arg][value]==nil?
		{
			Cond *cond = (Cond*)newudata(lua, sizeof(Cond));
			condCopy(cond, (rawgetk(lua, -2, -2), (Cond*)popudata(lua)), CONDSIZE);
			rawsetkv(lua, -2, -3, -1); // attach[arg][value] copy attach[arg][self]
		}
		condTrue((Cond*)toudata(lua, -1), n); // attach[arg][value][n]=true
		pop(lua, 3);
	}

	// group
	if (rawgetk(lua, L+1, upx(6)), popifz(lua, 1)) // L+4 attach._define
		newtablen(lua, 0, 7), rawsetkv(lua, L+1, upx(6), -1);
	for (pushz(lua); lua_next(lua, upx(4)); )
	{
		pop(lua, 1); // group
		if (rawgetk(lua, L+4, -1), popifz(lua, 1)) // attach._define[group]
			condFill((Cond*)newudata(lua, sizeof(Cond)), 0, CONDSIZE),
			rawsetkv(lua, L+4, -2, -1);
		condTrue((Cond*)toudata(lua, -1), n); // attach._define[group][n]=true
		pop(lua, 1);
	}
	if (whendef)
		rawgetk(lua, L, upx(6)), pushz(lua), rawset(lua, upx(4)); // group[when._define]=nil

	rawsetiv(lua, L+1, n+2, L+3); // attach[n+2]=info
	rawsetkv(lua, L+1, L-1, L+3); // attach[func]=info
	if (single)
	{
		if (rawgetk(lua, upx(3), L-2), isnil(lua, -1)) // _G[name]
			push(lua, L+1), push(lua, L-2), push(lua, upx(2)), pushs(lua, "_delay"),
			push(lua, upx(4)), push(lua, upx(6)), push(lua, upx(7)),
			push(lua, upx(8)), push(lua, upx(9)), push(lua, upx(10)),
			push(lua, upx(11)), push(lua, upx(12)), push(lua, upx(13)),
			pushcc(lua, Call, 13), rawsetkv(lua, upx(1), -1, L-2), // events[call]=name
			rawsetk(lua, upx(3), L-2); // _G[name]=call
		else if (tocfunc(lua, -1) != Call)
			return error(lua, "inconsistent global %s", name);
	}
	return 0;
}

//////////////////////////////////// group ///////////////////////////////////

// -1|2 group,ifskip +0 *1 groups
static int event_skip(lua_State *lua)
{
	if (isnil(lua, 1))
		return error(lua, "bad argument #1 (not nil expected, got nil)");
	if (isbool(lua, 2) && tobool(lua, 2) == false)
		getmeta(lua, upx(1)), pushz(lua), rawsetk(lua, -2, 1);
	else
		getmeta(lua, upx(1)), pushb(lua, true), rawsetk(lua, -2, 1);
	return 0;
}

// -1|2 func,errHint +1 nil|name *1 events{}
static int event_defname(lua_State *lua)
{
	checkArgableZ(lua, 2, "#2", LUA_TSTRING);
	if (tocfunc(lua, 1) != Call || (rawgetk(lua, upx(1), 1), isnil(lua, -1))) // L name
		return isnil(lua, 2) ? pushz(lua), 1 : error(lua, "%s (event expect, got %s)",
			tostr(lua, 2) ? tostr(lua, 2) : "bad argument", luaL_typename(lua, 1));
	return 1;
}

// -1|2 func,errHint +1* nil|names... *1 events{}
static int event_defargs(lua_State *lua)
{
	checkArgableZ(lua, 2, "#2", LUA_TSTRING);
	int L = gettop(lua)+1;
	if (tocfunc(lua, 1) != Call || (rawgetk(lua, upx(1), 1), isnil(lua, -1))) // L name
		return isnil(lua, 2) ? pushz(lua), 1 : error(lua, "%s (event expect, got %s)",
			tostr(lua, 2) ? tostr(lua, 2) : "bad argument", luaL_typename(lua, 1));
	rawget(lua, upx(1)), getmeta(lua, L); // L attach L+1 define
	int n = 0;
	for (pushz(lua); lua_next(lua, L+1); n++)
		pop(lua, 1), push(lua, -1);
	return n;	
}

static int event_remove(lua_State *lua)
{
	checkArg( lua, 1, "1", LUA_TFUNCTION );

	rawgetk( lua, upx( 1 ), 1 ); // events[call] = name
	if ( ! isstr( lua, -1 ) )
		return 0;

	rawgetk( lua, upx( 3 ), 2 ); // call name call
	if ( raweq( lua, -1, 1 ) )
		pushz( lua ), rawsetk( lua, upx( 3 ), 2 ); // _G[name] = call -> nil

	rawgetk( lua, upx( 1 ), 2 ); // call name call attach
	if ( ! isnil( lua, -1 ) )
		pushz( lua ), rawsetk( lua, upx( 1 ), 2 ); // events[name] = attach -> nil

	pushz( lua ), rawsetk( lua, upx( 1 ), 1 ); // events[call] = name -> nil

	return 0;
}

static int event_defnames(lua_State *lua)
{
	int L = gettop( lua ) + 1;
	newtable( lua );
	for ( pushz( lua ); lua_next( lua, upx( 1 ) ); )
	{
		if ( isfunc( lua, -2 ) && isstr( lua, -1 ) )
			rawsetkv( lua, L, -1, -2 );

		pop( lua, 1 );
	}

	return push( lua, L ), 1;
}

// -1|3 func|name,errHint,copy +1 nil|{arg=value}... *1 events{}
static int event_defvalues(lua_State *lua)
{
	checkArgableZ(lua, 2, "#2", LUA_TSTRING);
	checkArgableZ(lua, 3, "#3", LUA_TBOOLEAN);
	int L = gettop(lua)+1;
	if (tocfunc(lua, 1) != Call || (rawgetk(lua, upx(1), 1), isnil(lua, -1))) // L name
		return isnil(lua, 2) ? pushz(lua), 1 : error(lua, "%s (event expect, got %s)",
			tostr(lua, 2) ? tostr(lua, 2) : "bad argument", luaL_typename(lua, 1));
	rawget(lua, upx(1)); // L attach
	if (tobool(lua, 3))
	{
		getmeta(lua, L), newtable(lua); // L+1 define L+2 copy
		for (pushz(lua); lua_next(lua, L+1); )
			rawsetk(lua, L+2, -2);
	}
	else
		rawgeti(lua, L, -2); // originalDefine=attach[-2]
	return 1;
}

// -0|2 group,ifopen +0 *3 enqueue,_G,groups
static int event(lua_State *lua)
{
	condFill(&COND0, 0, CONDSIZE);
	settop(lua, 2); int L = 3;
	getmeta(lua, global); // L meta
	rawgetn(lua, L, "__newindex");
	if (tocfunc(lua, -1) != attach_attach)
	{
		pop(lua, 1), newtablen(lua, 0, 100); // L+1 events{}
		newudata(lua, 0), newmeta(lua, -1); // L+2 define L+3 define.meta
		newtablen(lua, 0, 100), newmeta(lua, -1),
			pop(lua, 1), lua_replace(lua, upx(3)); // groups

		push(lua, L+1), push(lua, upx(1)), push(lua, upx(2)), push(lua, upx(3)),
			pushcc(lua, define_name, 4), rawsetn(lua, L+3, "__index");
		push(lua, L+1), pushz(lua), push(lua, upx(1)), push(lua, upx(2)), push(lua, upx(3)),
			pushcc(lua, define_define, 5), rawsetn(lua, L+3, "__call");
		push(lua, L+2), rawsetn(lua, upx(2), "define");

		push(lua, L+1), pushcc(lua, attach_when, 1), rawsetn(lua, upx(2), "when");
		push(lua, L+1), push(lua, upx(1)), push(lua, upx(2)), push(lua, upx(3)),
			pushs(lua, "_args"), pushs(lua, "_define"), pushs(lua, "_stop"),
			pushs(lua, "_order"), pushs(lua, "_defines"), pushs(lua, "_try"),
			pushs(lua, "_slow"), pushs(lua, "_slows"), pushs(lua, "_slown"),
			pushcc(lua, attach_attach, 13), rawsetn(lua, L, "__newindex");

		push(lua, upx(3)), pushcc(lua, event_skip, 1), rawsetn(lua, upx(2), "_skip");
		push(lua, L+1), pushcc(lua, event_defname, 1), rawsetn(lua, upx(2), "_defname");
		push(lua, L+1), pushcc(lua, event_defargs, 1), rawsetn(lua, upx(2), "_defargs");
		push(lua, L+1), pushcc(lua, event_defvalues, 1), rawsetn(lua, upx(2), "_defvalues");

		push(lua, L+1), push(lua, upx(1)), push(lua, upx(2)), push(lua, upx(3)),
			pushcc(lua, event_remove, 4), rawsetn(lua, upx(2), "_defremove");
		push(lua, L+1), push(lua, upx(1)),
			pushcc(lua, event_defnames, 2), rawsetn(lua, upx(2), "_defnames");
	}
	settop(lua, L-1);
	if ( !isnil(lua, 1))
	{
		if (isbool(lua, 2) && tobool(lua, 2) == false)
		{
			pushz(lua), rawsetk(lua, upx(3), 1);
		}
		else
		{
			pushb(lua, true), rawsetk(lua, upx(3), 1);
		}
	}
	return 0;
}

