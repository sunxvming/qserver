static int timediff = 0;
static long long timelast = 0;

static long long timeNow(double unit, bool utc)
{
	long long us, uu;
#if WIN
	static long long start = 0;
	if (!start)
	{
		SYSTEMTIME stime; FILETIME time;
		GetSystemTime(&stime), SystemTimeToFileTime(&stime, &time);
		start = *(long long*)&time/10 - 12591158400000000LL - (long long)GetTickCount()*1000;
	}
	us = start + (long long)GetTickCount()*1000;
#else
	struct timeval time;
	gettimeofday(&time, NULL);
	us = time.tv_sec*1000000LL + time.tv_usec - 946684800000000LL;
#endif
	us > timelast ? (timelast = us) : (us = timelast);
	utc || (us += timediff*1000000LL);
	return (uu = d2l(1000000*unit)) > 1 ? us / uu : us;
}

static int DATIME_D2000 = 365*2000+(2000+3)/4-(2000+3)/100+(2000+3)/400;
static int DATIME_DM[] = { 0, 0, 31, 31+28, 31+28+31, 31+28+31+30, 31+28+31+30+31, 31+28+31+30+31+30,
	31+28+31+30+31+30+31, 31+28+31+30+31+30+31+31, 31+28+31+30+31+30+31+31+30,
	31+28+31+30+31+30+31+31+30+31, 31+28+31+30+31+30+31+31+30+31+30, 365, 365+31 };
static int DATIME_DMLEAP[] = { 0, 0, 31, 31+29, 31+29+31, 31+29+31+30, 31+29+31+30+31, 31+29+31+30+31+30,
	31+29+31+30+31+30+31, 31+29+31+30+31+30+31+31, 31+29+31+30+31+30+31+31+30,
	31+29+31+30+31+30+31+31+30+31, 31+29+31+30+31+30+31+31+30+31+30, 366, 366+31 };

static const char *packTime(long long *time, int y, int M, int d, int h, int m, int s, int usec)
{
	if (y < 0) return "year must >= 0";
	d += 365*y+(y+3)/4-(y+3)/100+(y+3)/400 - DATIME_D2000;
	d += (y%400==0||(y%100&&y%4==0) ? DATIME_DMLEAP : DATIME_DM)[M<1 ? 1 : M>12 ? 12 : M];
	*time = (d-1)*86400000000LL + h*3600000000LL + m*60000000LL + s*1000000LL + usec;
	return NULL;
}

static const char *unpackTime(long long time, int *y, int *M, int *d, int *h, int *m, int *s,
	int *msec, int *usec, int *wday, int *yday, int *mday)
{
	time += DATIME_D2000*86400000000LL;
	if (time < 0) return "year must >= 0";
	*usec = time%1000000, *msec = time/1000%1000;
	*s = time/1000000%60, *m = time/60000000LL%60, *h = time/3600000000LL%24;
	int dtime = (int)(time / 86400000000LL);
	*wday = (dtime+6)%7;
	int year = dtime/365, day;
	while ((day = 365*year+(year+3)/4-(year+3)/100+(year+3)/400) > dtime)
		year--;
	dtime -= day;
	int *dm = year%400==0 || (year%100 && year%4==0) ? DATIME_DMLEAP : DATIME_DM;
	int mon = dtime/28+1;
	while ((day = dm[mon]) > dtime)
		mon--;
	*d = dtime-day+1, *M = mon, *y = year;
	*yday = dtime+1, *mday = day+1;
	return NULL;
}

// -1|3 to|toUnit,from,fromUnit +1 to|value
// *11 'year','month','day','hour','min','sec','msec','usec','wday','yday','mday'
static int datime_time(lua_State *lua)
{
	long long time = 0, unit;
	if (isnum(lua, 2))
		time = tolong(lua, 2),
		unit = d2l(1000000 * luaL_optnumber(lua, 3, 0.001)),
		unit > 1 && (time = d2l((double)time * unit));
	else if (totype(lua, 2) == LUA_TTABLE)
	{
		int y = (rawgetk(lua, 2, upx(1)), popint(lua));
		int M = (rawgetk(lua, 2, upx(2)), popint(lua));
		int d = (rawgetk(lua, 2, upx(3)), popint(lua));
		int h = (rawgetk(lua, 2, upx(4)), popint(lua));
		int m = (rawgetk(lua, 2, upx(5)), popint(lua));
		int s = (rawgetk(lua, 2, upx(6)), popint(lua));
		int usec = (rawgetk(lua, 2, upx(8)), popifz(lua, 1)
				? rawgetk(lua, 2, upx(7)), popint(lua)*1000 : popint(lua));
		const char *err = packTime(&time, y, M, d, h, m, s, usec);
		err && error(lua, err);
	}
	else
		return error(lua, "bad argument #2 (number or table expected, got %s)",
			luaL_typename(lua, 2));

	if (isnum(lua, 1) || isnil(lua, 1))
		return unit = d2l(1000000 * luaL_optnumber(lua, 1, 0.001)),
			pushn(lua, unit > 1 ? time / unit : time), 1;
	else if (totype(lua, 1) == LUA_TTABLE)
	{
		int y, M, d, h, m, s, msec, usec, wday, yday, mday;
		y = M = d = h = m = s = msec = usec = wday = yday = mday = 0;
		const char *err = unpackTime(time, &y, &M, &d, &h, &m, &s,
			&msec, &usec, &wday, &yday, &mday);
		err && error(lua, err);
		pushi(lua, y), rawsetk(lua, 1, upx(1));
		pushi(lua, M), rawsetk(lua, 1, upx(2));
		pushi(lua, d), rawsetk(lua, 1, upx(3));
		pushi(lua, h), rawsetk(lua, 1, upx(4));
		pushi(lua, m), rawsetk(lua, 1, upx(5));
		pushi(lua, s), rawsetk(lua, 1, upx(6));
		pushi(lua, msec), rawsetk(lua, 1, upx(7));
		pushi(lua, usec), rawsetk(lua, 1, upx(8));
		pushi(lua, wday), rawsetk(lua, 1, upx(9));
		pushi(lua, yday), rawsetk(lua, 1, upx(10));
		pushi(lua, mday), rawsetk(lua, 1, upx(11));
		return push(lua, 1), 1;
	}
	else
		return error(lua, "bad argument #1 (number or table expected, got %s)",
			luaL_typename(lua, 1));
}

static void datime(lua_State *lua, int G)
{
	pushs(lua, "year"), pushs(lua, "month"), pushs(lua, "day"),
		pushs(lua, "hour"), pushs(lua, "min"), pushs(lua, "sec"),
		pushs(lua, "msec"), pushs(lua, "usec"),
		pushs(lua, "wday"), pushs(lua, "yday"), pushs(lua, "mday"),
		pushcc(lua, datime_time, 11), rawsetn(lua, G, "_time");
#if WIN
	_get_timezone((long*)&timediff);
#else
	struct timeval time; struct timezone tz;
	gettimeofday(&time, &tz);
	timediff = -60*tz.tz_minuteswest;
#endif
}
