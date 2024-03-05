/////////////////////////////// callin //////////////////////////////

// -2|4 remote,bytes[,first,last] +1 usec *3 enqueue,_G,last
static int callin(lua_State *lua)
{
	lua_isnone(lua, 1) && (pushz(lua), 0);
	lua_isnone(lua, 2) && (pushz(lua), 0);
	checkArgable(lua, 3, "#2", LUA_TNUMBER) || (pushi(lua, 1), 0);
	checkArgable(lua, 4, "#3", LUA_TNUMBER) || (pushi(lua, -1), 0);
	settop(lua, 4), push(lua, 1), lua_remove(lua, 1); // 1 data 2 first 3 last 4 remote
	int n = decode(lua); // L-1 name L args
	int L = gettop(lua);
	if (n < 2)
		return error(lua, "no enough data");
	if (n > 2)
		return error(lua, "excessive data %d", n);
	if ( !isstr(lua, L-1))
		return errType(lua, "bad remote name", LUA_TSTRING, totype(lua, L-1));
	unsigned char *name = (unsigned char*)tostr(lua, L-1);
	if (name[0]-(unsigned)'A' > 'Z'-'A')
		return error(lua, "remote %s must be uppercase", name);
	if ( !lua_istable(lua, L))
		return error(lua, "invalid argument to remote %s", name);
	// func
	rawgetk(lua, upx(2), L-1); // L+1 _G[name]
	if (tocfunc(lua, -1) != Call)
		return error(lua, "remote %s not found or invalid", name);
	// arg types
	for (pushz(lua); lua_next(lua, L); pop(lua, 1)) // name/value
	{
		if ( !isstr(lua, -2))
			return error(lua, "bad argument name to remote %s (string expected, got %s)",
				name, luaL_typename(lua, -2));
		unsigned char *aname = (unsigned char *)tostr(lua, -2);
		if (aname[0]-(unsigned)'A' > 'Z'-'A')
			return error(lua, "argument %s must be uppercase to remote %s", aname, name);
	}
	double us = (double)timeNow(0, false); timelast++;
	double last = tonum(lua, upx(3));
	us = us >= last ? us : last;
	push(lua, upx(1)), pushn(lua, us), push(lua, 4);
	push(lua, L-1), push(lua, L), call(lua, 4, 1); // enqueue(us, remote, name, args)
	pushn(lua, tonum(lua, -1)+1), lua_replace(lua, upx(3)); // last = enqueue+1
	return 1;
}

/////////////////////////////// callout //////////////////////////////

// -1 args +1 arg|usec *7 remote,doSend,name,enqueue,'_delay',_callout,defaultDelay
static int callout_call(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TTABLE);
	if (gettop(lua) != 1)
		return error(lua, "too many arguments");
	rawgetk(lua, 1, upx(5)); // args._delay
	double delay = !popifz(lua, 1);
	delay ? pushz(lua), rawsetk(lua, 1, upx(5)) : push(lua, upx(7)); // args._delay=nil or defaultDelay
	delay = tobool(lua, -1) ? queue_delay(lua) : popb(lua); // delay=0 or usec

	push(lua, upx(3)), lua_insert(lua, 1); // 1 name 2 args
	encode(lua); // encode(name,args)
	int L = gettop(lua); // L bdata
	if (delay)
		push(lua, upx(4)), pushn(lua, delay), push(lua, upx(6)); // delay _callout

	// doSend remote name args data
	push(lua, upx(2)), push(lua, upx(1)), push(lua, 1), push(lua, 2), push(lua, L);
	if (delay)
		call(lua, 7, 1); // enqueue(0|delay, _callout, doSend, ...)
	else
		call(lua, 4, 0), push(lua, 2); // doSend(...)
	return 1;
}

// -2 remote,key +1 callArgs *6 __index,doSend,enqueue,'_delay',_callout,defaultDelay
static int callout_name(lua_State *lua)
{
	unsigned char *name = (unsigned char*)tostr(lua, 2);
	if (name && name[0]-(unsigned)'A' <= 'Z'-'A')
	{
		push(lua, 1), push(lua, upx(2)), push(lua, 2), push(lua, upx(3)),
			push(lua, upx(4)), push(lua, upx(5)), push(lua, upx(6)),
			pushcc(lua, callout_call, 7);
		return 1; // net.Name
	}
	push(lua, upx(1));
	if (lua_istable(lua, -1))
		push(lua, 2), tabget(lua, -2);
	else if (lua_isfunction(lua, -1))
		push(lua, 1), push(lua, 2), call(lua, 2, 1);
	else
		pushz(lua);
	return 1;
}

// -1|3 remote,doSend(remote,name,args,data),defaultDelay +1 remote|doSend
// *4 enqueue,'_delay',_callout,'__index'
static int callout(lua_State *lua)
{
	if (totype(lua, 1) != LUA_TTABLE && totype(lua, 1) != LUA_TUSERDATA)
		return error(lua, "bad argument #1 (table or userdata expected, got %s)",
			luaL_typename(lua, 1));
	bool set = checkArgableZ(lua, 2, "#2", LUA_TFUNCTION);
	if (tobool(lua, 3))
		checkArgable(lua, 3, "#3", LUA_TNUMBER);
	else
		settop(lua, 2), pushb(lua, false);
	if ( !set)
	{
		if (getmeta(lua, 1) && (rawgetk(lua, -1, upx(4)), tocfunc(lua, -1) == callout_name))
			return lua_getupvalue(lua, -1, 2), 1;
		return pushz(lua), 1;
	}
	getmeta(lua, 1) || (newmeta(lua, 1), 0);
	rawgetk(lua, -1, upx(4));
	if (tocfunc(lua, -1) != callout_name)
		push(lua, 2), push(lua, upx(1)), push(lua, upx(2)), push(lua, upx(3)),
			push(lua, 3), pushcc(lua, callout_name, 6),
		rawsetk(lua, -2, upx(4));
	else if (lua_getupvalue(lua, -1, 2), !raweq(lua, -1, 2))
		return error(lua, "already _callout with a different function %p", tobody(lua, -1));
	return push(lua, 1), 1;
}

////////////////////////////////////// init /////////////////////////////////

static void remote(lua_State *lua, int M, int G)
{
	rawgetn(lua, G, "_enqueue"), push(lua, G), pushn(lua, 0), pushcc(lua, callin, 3);
	rawsetn(lua, G, "_callin");
	rawgetn(lua, G, "_enqueue"), pushs(lua, "_delay"), pushz(lua), pushs(lua, "__index");
	pushcc(lua, callout, 4), push(lua, -1), lua_setupvalue(lua, -2, 3);
	rawsetn(lua, G, "_callout");
}

