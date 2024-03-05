#include "mysql.h"

// meta = { __metatable='mysql', __index=methods }
// sqls = { sql=true, 1=buf }.meta{ __mode='k' }
#pragma pack(push)
#pragma pack(4)
typedef struct
{
	short	type;
	bool	cols; // column set
	char	align;
	MYSQL*	my;
	char	err[1024];
	uintptr	prepares; // { statement={ column... }[0=MYSQL_STMT*,1=type...] }
	uintptr exclude; // { column=true }
} Mysql;
#pragma pack(pop)

static void mysqlClose(lua_State *lua, int sqls, Mysql *sql, const char *reason)
{
	if ( !sql->my)
		return;
	if (reason)
		lua_pushfstring(lua, "INFO mysql %p closed: %s", sql, reason), logErr(pops(lua));
	if (sql->prepares)
	{
		lua_refhead(lua, (const void*)sql->prepares);
		for (pushz(lua); lua_next(lua, -2); )
			mysql_stmt_close(*(MYSQL_STMT**)popbody(lua));
		pop(lua, 1);
	}
	mysql_close(sql->my);
	lua_reftable(lua, sql), pushz(lua), rawset(lua, sqls); // sqls[sql]=nil
	sql->my = NULL;
}

static int mysqlFree(lua_State *lua, const void *sqls, long long sql, long long v, unsigned char closing)
{
	int L = gettop(lua)+1;
	getmetai(lua, global, M_mysqls); // L sqls
	mysqlClose(lua, L, (Mysql*)lua_headtobody((void*)(int)sql, NULL, NULL), "free");
	settop(lua, L-1);
	return true;
}

static int mysqlErr(lua_State *lua, int sqls, Mysql *sql, const char *err, MYSQL_STMT *st, int closeSt)
{
	strcpy(sql->err, st ? mysql_stmt_error(st) : mysql_error(sql->my));
	if (st)
		(closeSt & 2) && mysql_stmt_free_result(st), (closeSt & 1) && mysql_stmt_close(st);
	if ( !sql->my->net.vio)
		mysqlClose(lua, sqls, sql, sql->err);
	error(lua, err, sql->err);
	return 0;
}

// -6 host,user,password,db,port,timeout +1 sql *1 sqls
static int mysql_mysql(lua_State *lua)
{
	checkArg(lua, 1, "#1", LUA_TSTRING);
	checkArg(lua, 2, "#2", LUA_TSTRING);
	checkArg(lua, 3, "#3", LUA_TSTRING);
	checkArg(lua, 4, "#4", LUA_TSTRING);
	checkArg(lua, 5, "#5", LUA_TNUMBER);
	checkArg(lua, 6, "#6", LUA_TNUMBER);
	int L = gettop(lua)+1;
	Mysql *sql = (Mysql*)lua_createbody(lua, 4, 4, sizeof(Mysql), 2); // L sql
	getmetai(lua, global, M_mysqlmeta), setmeta(lua, L); // sql.meta=meta
	sql->type = M_mysqls, sql->my = mysql_init(NULL); 
	sql->cols = false;
	pushb(lua, true), rawsetk(lua, upx(1), L); // sqls[sql]=true
	int timeout = range(roundint(lua, 6), 1, 86400);
	mysql_options(sql->my, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
	mysql_options(sql->my, MYSQL_OPT_READ_TIMEOUT, &timeout);
	mysql_options(sql->my, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
	if ( !mysql_real_connect(sql->my, tostr(lua, 1), tostr(lua, 2), tostr(lua, 3),
		tostr(lua, 4), touint(lua, 5), NULL, CLIENT_MULTI_STATEMENTS))
		mysqlClose(lua, upx(1), sql, NULL), error(lua, "mysql init error: %s", sql->err);
	mysql_set_character_set(sql->my, "utf8");
	lua_markbody(lua, sql), newtablen(lua, 0, 100), sql->prepares = (uintptr) pophead(lua);
	return push(lua, L), 1;
}

// -1 sql +0 *1 sqls
static int mysql_Close(lua_State *lua)
{
	Mysql *sql = (Mysql*)totab(lua, 1);
	if ( !sql || sql->type != M_mysqls)
		return errArgT(lua, "#1", "mysql", totype(lua, 1));
	mysqlClose(lua, upx(1), sql, NULL);
	return 0;
}

// -1 sql +1 isclosed
static int mysql_closed(lua_State *lua)
{
	Mysql *sql = (Mysql*)totab(lua, 1);
	if ( !sql || sql->type != M_mysqls)
		return errArgT(lua, "#1", "mysql", totype(lua, 1));
	return pushb(lua, !sql->my), 1;
}

// -2 sql,statements +0 *1 sqls
static int mysql_runs(lua_State *lua)
{
	Mysql *sql = (Mysql*)totab(lua, 1);
	if ( !sql || sql->type != M_mysqls)
		return errArgT(lua, "#1", "mysql", totype(lua, 1));
	sql->my || error(lua, "mysql closed");
	checkArg(lua, 2, "#2", LUA_TSTRING);
	if (mysql_real_query(sql->my, tostr(lua, 2), (unsigned long)tolen(lua, 2)))
		mysqlErr(lua, upx(1), sql, "mysql error: %s", NULL, 0);
	MYSQL_RES *re;
	do
		if ((re = mysql_store_result(sql->my)))
			mysql_free_result(re);
	while (!mysql_next_result(sql->my));
	return 0;
}

static void mysqlEnc(lua_State *lua, int index, MYSQL_BIND *b)
{
	if (isnil(lua, index))
		b->buffer_type = MYSQL_TYPE_NULL;
	else if (isbool(lua, index))
		b->buffer_type = MYSQL_TYPE_TINY, *(char*)b->buffer = tobool(lua, index), b->buffer_length = 1;
	else if (isnum(lua, index))
	{
		double V = tonum(lua, index); long long v = (long long)V;
		if (v == V)
			b->buffer_type = MYSQL_TYPE_LONGLONG, *(long long*)b->buffer = v;
		else
			b->buffer_type = MYSQL_TYPE_DOUBLE, *(double *)b->buffer = V;
		b->buffer_length = 8;
	}
	else if (isstr(lua, index))
		b->buffer_type = MYSQL_TYPE_STRING,
		b->buffer = (void*)tostr(lua, index), b->buffer_length = (unsigned long)tolen(lua, index);
	else if (toudata(lua, index))
	{
		const char *s = tobytes(lua, index, (size_t*)&b->buffer_length, NULL);
		if ( !s)
			error(lua, "bad parameter #%d (bytes expected, got %s)", index, tonamex(lua, index));
		b->buffer_type = MYSQL_TYPE_BLOB, b->buffer = (void*)s;
	}
	else
		error(lua, "bad parameter #%d (boolean/number/bytes expected, got %s)",
			index, tonamex(lua, index));
}
static void mysqlDec(lua_State *lua, int sqls, Mysql *sql, MYSQL_STMT *st, MYSQL_BIND *b, int c)
{
	const char *err;
	if (*b->is_null)
		pushz(lua);
	else switch (b->buffer_type)
	{
	case MYSQL_TYPE_TINY: case MYSQL_TYPE_SHORT: case MYSQL_TYPE_LONG:
		pushi(lua, *(int*)b->buffer); break;
	case MYSQL_TYPE_LONGLONG:
		pushn(lua, *(long long*)b->buffer); break;
	case MYSQL_TYPE_FLOAT:
		pushn(lua, *(float*)b->buffer); break;
	case MYSQL_TYPE_DOUBLE:
		pushn(lua, *(double*)b->buffer); break;
	case MYSQL_TYPE_STRING: case MYSQL_TYPE_VAR_STRING:
	{
		if (*b->length > 262144)
			err = mysql_stmt_error(st), mysql_stmt_free_result(st),
			error(lua, "sql error: column too long %u", *b->length);
		char data[262144];
		b->buffer_length = *b->length, b->buffer = data;
		if (mysql_stmt_fetch_column(st, b, c-1, 0))
			mysqlErr(lua, sqls, sql, "sql error: %s", st, 2);
		pushsl(lua, data, *b->length); break;
	}
	case MYSQL_TYPE_BLOB:
	{
		if (*b->length > 6*1024*1024)
			err = mysql_stmt_error(st), mysql_stmt_free_result(st),
			error(lua, "sql error: column too long %u", *b->length);
		char *data = newbdata(lua, *b->length);
		b->buffer_length = *b->length, b->buffer = data;
		if (mysql_stmt_fetch_column(st, b, c-1, 0))
			mysqlErr(lua, sqls, sql, "sql error: %s", st, 2);
		break;
	}
	default:
		pushz(lua);
	}
}

// -2* sql,statement,param... +1|2 data|rown,insertid *1 sqls
static int mysql_run(lua_State *lua)
{
	Mysql *sql = (Mysql*)totab(lua, 1);
	if ( !sql || sql->type != M_mysqls)
		return errArgT(lua, "#1", "mysql", totype(lua, 1));
	sql->my || error(lua, "mysql closed");
	checkArg(lua, 2, "#2", LUA_TSTRING);
	int L = gettop(lua);
	sql->exclude ? lua_refhead(lua, (const void*)sql->exclude) : pushz(lua); // L+1 exclude
	bool cols = sql->cols;
	sql->cols = false;
	lua_markbody(lua, sql), sql->exclude = NULL;
	lua_refhead(lua, (const void*)sql->prepares), rawgetk(lua, L+2, 2); // L+2 prepares L+3 prepare

	int cn = (int)tolen(lua, L+3), *cts = (int*)totab(lua, L+3);
	MYSQL_STMT *st; const char *err;
	if (cts)	
		st = (MYSQL_STMT*)cts[0];
	else
	{
		st = mysql_stmt_init(sql->my);
		st || mysqlErr(lua, upx(1), sql, "sql error: %s", NULL, 0);
		if (mysql_stmt_prepare(st, tostr(lua, 2), (unsigned long)tolen(lua, 2)))
			mysqlErr(lua, upx(1), sql, "sql error: %s", st, 1);
		MYSQL_RES *res = mysql_stmt_result_metadata(st);
		if (mysql_stmt_errno(st))
			mysqlErr(lua, upx(1), sql, "sql error: %s", st, 1);
		if (res)
		{
			cn = mysql_num_fields(res);
			if (cn >= 128)
				mysql_stmt_free_result(st), mysql_stmt_close(st),
				error(lua, "sql error: too many columns %d", cn);
			cts = (int*)lua_createbody(lua, cn, 0, 4+4*cn, 0), lua_replace(lua, L+3); // L+3 prepare
			MYSQL_FIELD *c;
			for (int i = 1; i <= cn; i++)
			{
				c = mysql_fetch_field_direct(res, i-1);
				pushs(lua, c->name), rawseti(lua, L+3, i); // prepare[i] = column
				switch (c->type)
				{
				case MYSQL_TYPE_TINY: case MYSQL_TYPE_SHORT: case MYSQL_TYPE_LONG:
				case MYSQL_TYPE_LONGLONG: case MYSQL_TYPE_FLOAT: case MYSQL_TYPE_DOUBLE:
					cts[i] = c->type; break;
				case MYSQL_TYPE_STRING: case MYSQL_TYPE_VAR_STRING:
					cts[i] = c->charsetnr == 63 ? MYSQL_TYPE_BLOB // binary
						: c->type; break;
				case MYSQL_TYPE_BLOB:
					cts[i] =c->charsetnr != 63 ? MYSQL_TYPE_VAR_STRING // text
						: c->type; break;
				default:
					mysql_stmt_free_result(st), mysql_stmt_close(st);
					error(lua, "sql error: unknown type of column %d", i);
				}
			}
			mysql_stmt_free_result(st);
		}
		else // no result set
			cts = (int*)lua_createbody(lua, 0, 0, 4, 0), lua_replace(lua, L+3); // L+3 prepare
		cts[0] = (uintptr)st;
		rawsetkv(lua, L+2, 2, L+3); // prepares[statement] = prepare
	}

	int pn = mysql_stmt_param_count(st);
	if (pn >= 128)
		error(lua, "sql error: too many parameters %d", pn);
	if (pn != L-2)
		error(lua, "bad argument (%d parameters expected, got %d)", pn, L-2);
	long long pis[128], cis[128];

	memset(st->params, 0, pn * sizeof(MYSQL_BIND));
	for (int p = 1; p <= pn; p++)
	{
		MYSQL_BIND *b = st->params+p-1;
		b->buffer = pis+p;
		mysqlEnc(lua, p+2, b);
	}
	if (mysql_stmt_bind_param(st, st->params))
		mysqlErr(lua, upx(1), sql, "sql error: %s", st, 0);
	if (cn > 0)
	{
		memset(st->bind, 0, cn * sizeof(MYSQL_BIND));
		if (mysql_stmt_bind_result(st, st->bind))
			mysqlErr(lua, upx(1), sql, "sql error: %s", st, 0);
	}
	if (mysql_stmt_execute(st))
		mysqlErr(lua, upx(1), sql, "sql error: %s", st, 0);
	if (cn == 0)
		return pushn(lua, mysql_stmt_affected_rows(st)), pushn(lua, mysql_stmt_insert_id(st)), 2;

	int D = gettop(lua)+1, c;
	if (cols)
		for (newtablen(lua, 0, cn), c = 1; c <= cn; c++) // D data
			isnil(lua, L+1) || (rawgeti(lua, L+3, c), rawget(lua, L+1), popz(lua)) // !exclude[prepare[c]]
			? newtablen(lua, 8, 0), rawgeti(lua, L+3, c), rawsetv(lua, D, -2) : pushz(lua); // D+c cols
	else
		newtablen(lua, 8, 0); // D data
	for (int r = 1; ; r++)
	{
		memset(st->bind, 0, cn * sizeof(MYSQL_BIND));
		for (c = 1; c <= cn; c++)
		{
			MYSQL_BIND *b = st->bind+c-1;
#if WIN
			b->buffer_type = (enum_field_types)cts[c];
#else
			b->buffer_type = (enum_field_types)cts[c];
#endif
			b->buffer = cis+c, cis[c] = 0;
			b->buffer_length = cts[c]==MYSQL_TYPE_STRING || cts[c]==MYSQL_TYPE_BLOB ? 0 : 8;
		}
		mysql_stmt_bind_result(st, st->bind);
		int fail = mysql_stmt_fetch(st);
		if (fail == MYSQL_NO_DATA)
			break;
		if (fail && fail != MYSQL_DATA_TRUNCATED)
			mysqlErr(lua, upx(1), sql, "sql error: %s", st, 2);
		if ( !cols)
			newtablen(lua, 0, cn); // row
		for (c = 1; c <= cn; c++)
		{
			MYSQL_BIND *b = st->bind+c-1;
			if ((int)b->buffer_type != cts[c])
				err = mysql_stmt_error(st), mysql_stmt_free_result(st),
				error(lua, "sql error: different column type %d %d", cts[c], b->buffer_type);
			if (isnil(lua, L+1) || (rawgeti(lua, L+3, c), rawget(lua, L+1), popz(lua))) // !exclude[prepare[c]]
			{
				cols || (rawgeti(lua, L+3, c), 0); // column name
				mysqlDec(lua, upx(1), sql, st, b, c);
				cols ? rawseti(lua, D+c, r) : rawset(lua, -3);
			}
		}
		if ( !cols)
			rawseti(lua, D, r);
	}
	return push(lua, D), 1;
}

// -1 sql +1 sql
static int mysql_columns(lua_State *lua)
{
	Mysql *sql = (Mysql*)totab(lua, 1);
	if ( !sql || sql->type != M_mysqls)
		return errArgT(lua, "#1", "mysql", totype(lua, 1));
	sql->cols = true;
	return push(lua, 1), 1;
}
// -1|2 sql,cleanPrepare +1 sql
static int mysql_default(lua_State *lua)
{
	Mysql *sql = (Mysql*)totab(lua, 1);
	if ( !sql || sql->type != M_mysqls)
		return errArgT(lua, "#1", "mysql", totype(lua, 1));
	sql->cols = false;
	lua_markbody(lua, sql), sql->exclude = NULL;
	if (tobool(lua, 2) && sql->prepares)
	{
		lua_refhead(lua, (const void*)sql->prepares);
		for (pushz(lua); lua_next(lua, -2); )
			mysql_stmt_close(*(MYSQL_STMT**)popbody(lua));
		pop(lua, 1);
		newtablen(lua, 0, 100), sql->prepares = (uintptr) pophead(lua);
	}
	return push(lua, 1), 1;
}
// -1* sql,colomn... +1 sql
static int mysql_exclude(lua_State *lua)
{
	Mysql *sql = (Mysql*)totab(lua, 1);
	if ( !sql || sql->type != M_mysqls)
		return errArgT(lua, "#1", "mysql", totype(lua, 1));
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

static void mysql(lua_State *lua, int M, int G)
{
	int L = gettop(lua)+1;
	newtablen(lua, 4, 0), newmetaweak(lua, 0, 0, 10, "k"), setmeta(lua, L); // L sqls
	lua_setweaking(lua, L, mysqlFree), rawsetiv(lua, M, M_mysqls, L); // M_mysqls=sqls
	newmetameta(lua, 0, 2, 0, "mysql"); // L+1 meta
	rawsetiv(lua, M, M_mysqlmeta, L+1); // M_sqlmeta=meta
	newtable(lua), rawsetnv(lua, L+1, "__index", L+2); // L+2 __index

	push(lua, L), pushcc(lua, mysql_mysql, 1), rawsetn(lua, G, "_mysql");
	push(lua, L), pushcc(lua, mysql_Close, 1), rawsetn(lua, L+2, "close");
	push(lua, L), pushcc(lua, mysql_runs, 1), rawsetn(lua, L+2, "runs");
	push(lua, L), pushcc(lua, mysql_run, 1), rawsetn(lua, L+2, "run");
	pushc(lua, mysql_closed), rawsetn(lua, L+2, "closed");
	pushc(lua, mysql_columns), rawsetn(lua, L+2, "columns");
	pushc(lua, mysql_default), rawsetn(lua, L+2, "default");
	pushc(lua, mysql_exclude), rawsetn(lua, L+2, "exclude");
}
