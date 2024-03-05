#define QUEUESIZE 50000

// queue longlong[QUEUESIZE+1], from later to earlier

static int queue_search(long long *queue, long long usec)
{
	int low = 1, high = (int)queue[0];
	while (low <= high)
	{
		int mid = (low + high) >> 1; // ignore overflow
		long long v = queue[mid];
		if (v > usec)
			low = mid+1;
		else if (v < usec)
			high = mid-1;
		else
			return mid;
	}
	return ~low;
}

// -0 +n *n args
static int queue_Args(lua_State *lua)
{
	int n = 1;
	while (totype(lua, upx(n)) >= 0)
		push(lua, upx(n)), n++;
	return n-1;
}

// -1 arg +n args
static int queue_args(lua_State *lua)
{
	if (toudata(lua, -1) == queue_Args)
		return pop(lua, 1), 0; // no arg
	if (tocfunc(lua, -1) != queue_Args)
		return 1; // 1 arg
	return calln(lua, 0);
}

//////////////////////////////// enqueue //////////////////////////
static bool sub = false;
// -3... usec,from,func|name,arg... +1 finalUsec *5 queue,froms{},fns{},args{},min
static int enqueue(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	if ( !isfunc(lua, 3) && !isstr(lua, 3))
		return error(lua, "bad argument #3 (function or string expected, got %s)",
			luaL_typename(lua, 3));
	int L = gettop(lua);
	long long usec = mustlong(lua, 1), min = (long long)tonum(lua, upx(5));
	if (usec <= min)
		usec = ++min, pushn(lua, min), lua_replace(lua, upx(5)); // usec = ++min
	long long *queue = (long long*)toudata(lua, upx(1));
	if (queue[0] >= QUEUESIZE)
		return error(lua, "queue too long");
	int x = queue_search(queue, usec);
	if (x < 0)
		x = ~x;
	else // usec == queue[x]
		for (usec = queue[x]+1; x > 1 && queue[x-1] <= usec; x--)
			usec = queue[x-1]+1;
	memmove(queue+x+1, queue+x, ((int)queue[0]-x+1)*sizeof(long long));
	queue[x] = usec, queue[0]++;
	pushn(lua, usec), lua_replace(lua, 1); // usec=usec
	rawsetkv(lua, upx(2), 1, 2); // froms[usec]=from
	rawsetkv(lua, upx(3), 1, 3); // fns[usec]=func|name
	if (L == 3)
		pushaddr(lua, (void*)queue_Args), rawsetk(lua, upx(4), 1); // args[usec]=no arg
	else if (L == 4)
		rawsetkv(lua, upx(4), 1, 4); // args[usec]=1 arg
	else // L >= 5  >=2 args  here top==L
		pushcc(lua, queue_Args, L-3), rawsetk(lua, upx(4), 1); // args[usec]=args
	getmetai(lua, global, M_qsub); // sub=meta[qsub]
	//isnil(lua, -1) || (rawsetiv(lua, -1, (int)tolen(lua, -1)+1, 1), 0); // sub[#sub+1]=usec
	sub && (rawsetiv(lua, -1, (int)tolen(lua, -1)+1, 1), 0); // sub[#sub+1]=usec
	return push(lua, 1), 1;
}

// -0|1 start|quit +1 started *1 quitqueue
static int subqueue(lua_State *lua)
{
	bool go = checkArgable(lua, 1, "#1", LUA_TBOOLEAN);
	int L = gettop(lua)+1;
	getmeta(lua, global), rawgeti(lua, -1, M_qsub); // L meta L+1 sub
	if ( !go)
		pushb(lua, !isnil(lua, L+1));
	else if (tobool(lua, 1)) // start
	{
		//if (isnil(lua, L+1) || tolen(lua, L+1))
		if (isnil(lua, L+1))
			newtablen(lua, 5, 0), rawseti(lua, L, M_qsub); // meta[qsub]={}
		else
			lua_cleartable(lua, L+1);
		pushb(lua, true);
		sub = true;
	}
	else if (isnil(lua, L+1))
		pushb(lua, false);
	else // quit
	{
		for (int n = (int)tolen(lua, L+1); n >= 1; n--)
			push(lua, upx(1)), rawgeti(lua, -2, n), pushb(lua, false), pushb(lua, true),
			call(lua, 3, 0); // quitqueue(usec,false,true)
		//pushz(lua), rawseti(lua, L, M_qsub); // meta[qsub]=nil
		lua_cleartable(lua, -1);
		pushb(lua, false);
		sub = false;
	}
	return 1;
}

// -1 sec +0
static double queue_delay(lua_State *lua)
{
	if ( !isnum(lua, -1))
		error(lua, "invalid delay (number expected, got %s)", luaL_typename(lua, -1));
	double sec = popn(lua);
	if (sec < -10)
		sec = -10;
	else if ( !(sec <= 366*86400))
		error(lua, "too long or illegal delay %f", sec);
	if (getmetai(lua, global, M_now), popifz(lua, 1))
		return sec*1000000+(double)timeNow(0, false);
	return sec*1000000+popn(lua);
}

////////////////////////////////// dequeue /////////////////////////////////

// -1|2 usec,peek +0|3... usec,from,func|name,arg... *4 queue,froms{},fns{},args{}
static int dequeue(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	settop(lua, 2);
	long long *queue = (long long*)toudata(lua, upx(1));
	long long usec = tolong(lua, 1), u = queue[queue[0]];
	if (queue[0] == 0 || usec < u)
		return 0;
	usec = u;
	bool peek = tobool(lua, 2);
	pushn(lua, usec), push(lua, -1), lua_replace(lua, 1); // usec=usec
	rawgetk(lua, upx(2), 1); // from=froms[usec]
	rawgetk(lua, upx(3), 1); // func|name=fns[usec]
	rawgetk(lua, upx(4), 1); // arg=args[usec]
	if ( !peek)
		queue[0]--,
		pushz(lua), rawsetk(lua, upx(2), 1), // froms[usec]=nil
		pushz(lua), rawsetk(lua, upx(3), 1), // fns[usec]=nil
		pushz(lua), rawsetk(lua, upx(4), 1); // args[usec]=nil
	return 3+queue_args(lua);
}

// -1|3 usec,peek,quiet +0|3... usec,from,func|name,arg... *4 queue,froms{},fns{},args{}
static int quitqueue(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TNUMBER);
	settop(lua, 3); // not quiet by default
	long long usec = mustlong(lua, 1);
	long long *queue = (long long*)toudata(lua, upx(1));
	int x = queue_search(queue, usec);
	if (x < 0)
		return tobool(lua, 3) ? 0 : error(lua, "no match time %f", (double)usec);
	bool peek = tobool(lua, 2);
	pushn(lua, usec), push(lua, -1), lua_replace(lua, 1); // usec=usec
	rawgetk(lua, upx(2), 1); // from=froms[usec]
	rawgetk(lua, upx(3), 1); // func|name=fns[usec]
	rawgetk(lua, upx(4), 1); // arg=args[usec]
	if ( !peek)
		memmove(queue+x, queue+x+1, ((int)queue[0]-x)*sizeof(long long)),
		queue[0]--,
		pushz(lua), rawsetk(lua, upx(2), 1), // froms[usec]=nil
		pushz(lua), rawsetk(lua, upx(3), 1), // fns[usec]=nil
		pushz(lua), rawsetk(lua, upx(4), 1); // args[usec]=nil
	return 3+queue_args(lua);
}

/////////////////////////////////// call //////////////////////////////////

// -1 err +1 err *1 onErr
static int queue_err(lua_State *lua)
{
	push(lua, upx(1)), getmetai(lua, global, M_qfunc), push(lua, 1), call(lua, 2, 1);
	return 1;
}

// -9 1=post,func|name,3=func|name,arg,pre,6='_from',from,usec,now *2 _G,M
static int queue_call(lua_State *lua)
{
	rawseti(lua, upx(2), M_now); // _now(,false)=now
	rawseti(lua, upx(2), M_nowq); // _now(,true)=usec
	rawset(lua, upx(1)); // _G._from=from
	do
	{
		if ( !popifz(lua, 1))
		{
			push(lua, 3), push(lua, 4), call(lua, 1+queue_args(lua), 1); // pre(func|name,arg...)
			if (popb(lua))
				break;
		}
		isstr(lua, 3) && (rawgetk(lua, upx(1), 3), lua_replace(lua, 3), 0);
		call(lua, queue_args(lua), LUA_MULTRET);
		isnil(lua, 1) || (call(lua, gettop(lua)-1, 0), 0);
	}
	while (false);
	return 0;
}

static bool queue_pcall(lua_State *lua, long long now)
{
	int L = gettop(lua);
	getmeta(lua, global); // L+1 meta
	rawgeti(lua, L+1, M_queue); // L+2 queue
	rawgeti(lua, L+1, M_qfroms); // L+3 froms
	getmeta(lua, -1); // L+4 fns
	getmeta(lua, -1); // L+5 args
	long long *queue = (long long*)toudata(lua, L+2);
	long long u = queue[queue[0]];
	if (queue[0] == 0 || now < u)
		return settop(lua, L), false;
	queue[0]--;
	pushn(lua, u); // L+6 usec
	rawgetk(lua, L+4, L+6), rawsetiv(lua, L+1, M_qfunc, L+7); // L+7 func|name
	rawgeti(lua, L+1, M_qerr); // L+8 onerror
	rawgeti(lua, L+1, M_qcall); // L+9 queue_call
	rawgeti(lua, L+1, M_qpost), push(lua, L+7), push(lua, L+7); // L+10 post func|name func|name
		rawgetk(lua, L+5, L+6), rawgeti(lua, L+1, M_qpre), // arg pre
		rawgeti(lua, L+1, M__from), rawgetk(lua, L+3, L+6), // _from from
		push(lua, L+6), pushn(lua, now); // usec now
	pushz(lua), rawsetk(lua, L+3, L+6); // froms[usec]=nil
	pushz(lua), rawsetk(lua, L+4, L+6); // fns[usec]=nil
	pushz(lua), rawsetk(lua, L+5, L+6); // args[usec]=nil
	if (pcall(lua, 9, 0, L+8) && !isnil(lua, -1))
		logErr(isstr(lua, -1) ? tostr(lua, -1) : tonamex(lua, -1));
	rawgeti(lua, L+1, M_G), rawgeti(lua, L+1, M__from),
		pushz(lua), rawset(lua, -3); // _G._from=nil
	pushz(lua), rawseti(lua, L+1, M_now); // _now(,true)=nil
	pushz(lua), rawseti(lua, L+1, M_nowq); // _now(,false)=nil
	return settop(lua, L), true;
}

// -3 preCall(func|name,arg...),postCall(func|name,result...),errCall(func|name,err) +0
static int queue_queue(lua_State *lua)
{
	checkArgableZ(lua, 1, "#1", LUA_TFUNCTION);
	checkArgableZ(lua, 2, "#2", LUA_TFUNCTION);
	bool err = checkArgableZ(lua, 3, "#3", LUA_TFUNCTION);
	getmeta(lua, global), rawsetiv(lua, -1, M_qpre, 1), rawsetiv(lua, -1, M_qpost, 2);
	err ? push(lua, 3), pushcc(lua, queue_err, 1) : rawgeti(lua, -1, M_onerror); rawseti(lua, -2, M_qerr);
	return 0;
}

// -0|1 n +1... n,... *1 queue
static int queue_queuen(lua_State *lua)
{
	long long *queue = (long long*)toudata(lua, upx(1));
	if (isnum(lua, 1))
	{
		int n = toint(lua, 1);
		n = n > (int)queue[0] ? (int)queue[0] : n;
		for (int i = (int)queue[0], j = i-n; i > j; i--)
			pushn(lua, (double)queue[i]);
		return n;
	}	
	return pushi(lua, (int)queue[0]), 1;
}

/////////////////////////////////// init //////////////////////////////////

static void queue(lua_State *lua, int M, int G)
{
	int L = gettop(lua)+1;
	long long *queue = (long long*)newudata(lua, (QUEUESIZE+1)*sizeof(long long)); // L queue
	queue[0] = 0;

	newtablen(lua, 0, 10000), newmetan(lua, -1, 0, 10000), newmetan(lua, -1, 0, 10000);


	// L+1 froms L+2 fns L+3 args

	push(lua, L), push(lua, L+1), push(lua, L+2), push(lua, L+3),
		pushn(lua, (double)timeNow(0, false)-1000000),
		pushcc(lua, enqueue, 5), rawsetn(lua, G, "_enqueue");
	push(lua, L), push(lua, L+1), push(lua, L+2), push(lua, L+3),
		pushcc(lua, dequeue, 4), rawsetn(lua, G, "_dequeue");
	push(lua, L), push(lua, L+1), push(lua, L+2), push(lua, L+3),
		pushcc(lua, quitqueue, 4), rawsetn(lua, G, "_quitqueue");
	push(lua, L), pushcc(lua, queue_queuen, 1), rawsetnv(lua, G, "_queuen", -1);
	rawgetn(lua, G, "_quitqueue"), pushcc(lua, subqueue, 1), rawsetn(lua, G, "_subqueue");
	pushc(lua, queue_queue), rawsetnv(lua, G, "_queue", -1), call(lua, 0, 0);

	push(lua, L), rawseti(lua, M, M_queue), push(lua, L+1), rawseti(lua, M, M_qfroms);
	pushs(lua, "_from"), rawseti(lua, M, M__from);
	push(lua, G), push(lua, M), pushcc(lua, queue_call, 2), rawseti(lua, M, M_qcall);
}
