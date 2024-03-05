/*
** Public Lua/C API.
** Copyright (C) 2005-2013 Mike Pall. See Copyright Notice in luajit.h
**
** Major portions taken verbatim or adapted from the Lua interpreter.
** Copyright (C) 1994-2008 Lua.org, PUC-Rio. See Copyright Notice in lua.h
*/

#define lj_api_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_func.h"
#include "lj_udata.h"
#include "lj_meta.h"
#include "lj_state.h"
#include "lj_bc.h"
#include "lj_frame.h"
#include "lj_trace.h"
#include "lj_vm.h"
#include "lj_strscan.h"
#include "lauxlib.h"
/* -- Common helper functions --------------------------------------------- */

#define api_checknelems(L, n)		api_check(L, (n) <= (L->top - L->base))
#define api_checkvalidindex(L, i)	api_check(L, (i) != niltv(L))

static TValue *index2adr(lua_State *L, int idx)
{
  if (idx > 0) {
    TValue *o = L->base + (idx - 1);
    return o < L->top ? o : niltv(L);
  } else if (idx > LUA_REGISTRYINDEX) {
//    api_check(L, idx != 0 && -idx <= L->top - L->base);
//    return L->top + idx;
	return idx < 0 && L->base <= L->top + idx ? L->top + idx : niltv(L); // fancy
  } else if (idx == LUA_GLOBALSINDEX) {
    TValue *o = &G(L)->tmptv;
    settabV(L, o, tabref(L->env));
    return o;
  } else if (idx == LUA_REGISTRYINDEX) {
    return registry(L);
  } else {
    GCfunc *fn = curr_func(L);
    api_check(L, fn->c.gct == ~LJ_TFUNC && !isluafunc(fn));
    if (idx == LUA_ENVIRONINDEX) {
      TValue *o = &G(L)->tmptv;
      settabV(L, o, tabref(fn->c.env));
      return o;
    } else {
      idx = LUA_GLOBALSINDEX - idx;
      return idx <= fn->c.nupvalues ? &fn->c.upvalue[idx-1] : niltv(L);
    }
  }
}

static TValue *stkindex2adr(lua_State *L, int idx)
{
  if (idx > 0) {
    TValue *o = L->base + (idx - 1);
    return o < L->top ? o : niltv(L);
  } else {
    api_check(L, idx != 0 && -idx <= L->top - L->base);
    return L->top + idx;
  }
}

static GCtab *getcurrenv(lua_State *L)
{
  GCfunc *fn = curr_func(L);
  return fn->c.gct == ~LJ_TFUNC ? tabref(fn->c.env) : tabref(L->env);
}

/* -- Miscellaneous API functions ----------------------------------------- */

LUA_API int lua_status(lua_State *L)
{
  return L->status;
}

LUA_API int lua_checkstack(lua_State *L, int size)
{
  if (size > LUAI_MAXCSTACK || (L->top - L->base + size) > LUAI_MAXCSTACK) {
    return 0;  /* Stack overflow. */
  } else if (size > 0) {
    lj_state_checkstack(L, (MSize)size);
  }
  return 1;
}

LUALIB_API void luaL_checkstack(lua_State *L, int size, const char *msg)
{
  if (!lua_checkstack(L, size))
    lj_err_callerv(L, LJ_ERR_STKOVM, msg);
}

LUA_API void lua_xmove(lua_State *from, lua_State *to, int n)
{
  TValue *f, *t;
  if (from == to) return;
  api_checknelems(from, n);
  api_check(from, G(from) == G(to));
  lj_state_checkstack(to, (MSize)n);
  f = from->top;
  t = to->top = to->top + n;
  while (--n >= 0) copyTV(to, --t, --f);
  from->top = f;
}

/* -- Stack manipulation -------------------------------------------------- */

LUA_API int lua_gettop(lua_State *L)
{
  return (int)(L->top - L->base);
}

LUA_API void lua_settop(lua_State *L, int idx)
{
  if (idx >= 0) {
    api_check(L, idx <= tvref(L->maxstack) - L->base);
    if (L->base + idx > L->top) {
      if (L->base + idx >= tvref(L->maxstack))
	lj_state_growstack(L, (MSize)idx - (MSize)(L->top - L->base));
      do { setnilV(L->top++); } while (L->top < L->base + idx);
    } else {
      L->top = L->base + idx;
    }
  } else {
    api_check(L, -(idx+1) <= (L->top - L->base));
    L->top += idx+1;  /* Shrinks top (idx < 0). */
  }
}

LUA_API void lua_remove(lua_State *L, int idx)
{
  TValue *p = stkindex2adr(L, idx);
  api_checkvalidindex(L, p);
  while (++p < L->top) copyTV(L, p-1, p);
  L->top--;
}

LUA_API void lua_insert(lua_State *L, int idx)
{
  TValue *q, *p = stkindex2adr(L, idx);
  api_checkvalidindex(L, p);
  for (q = L->top; q > p; q--) copyTV(L, q, q-1);
  copyTV(L, p, L->top);
}

LUA_API void lua_replace(lua_State *L, int idx)
{
  api_checknelems(L, 1);
  if (idx == LUA_GLOBALSINDEX) {
    api_check(L, tvistab(L->top-1));
    /* NOBARRIER: A thread (i.e. L) is never black. */
    setgcref(L->env, obj2gco(tabV(L->top-1)));
  } else if (idx == LUA_ENVIRONINDEX) {
    GCfunc *fn = curr_func(L);
    if (fn->c.gct != ~LJ_TFUNC)
      lj_err_msg(L, LJ_ERR_NOENV);
    api_check(L, tvistab(L->top-1));
    setgcref(fn->c.env, obj2gco(tabV(L->top-1)));
    lj_gc_barrier(L, fn, L->top-1);
  } else {
    TValue *o = index2adr(L, idx);
    api_checkvalidindex(L, o);
    copyTV(L, o, L->top-1);
    if (idx < LUA_GLOBALSINDEX)  /* Need a barrier for upvalues. */
      lj_gc_barrier(L, curr_func(L), L->top-1);
  }
  L->top--;
}

LUA_API void lua_pushvalue(lua_State *L, int idx)
{
  copyTV(L, L->top, index2adr(L, idx));
  incr_top(L);
}

/* -- Stack getters ------------------------------------------------------- */

LUA_API int lua_type(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  if (tvisnumber(o)) {
    return LUA_TNUMBER;
#if LJ_64
  } else if (tvislightud(o)) {
    return LUA_TLIGHTUSERDATA;
#endif
  } else if (o == niltv(L)) {
    return LUA_TNONE;
  } else {  /* Magic internal/external tag conversion. ORDER LJ_T */
    uint32_t t = ~itype(o);
#if LJ_64
    int tt = (int)((U64x(75a06,98042110) >> 4*t) & 15u);
#else
    int tt = (int)(((t < 8 ? 0x98042110u : 0x75a06u) >> 4*(t&7)) & 15u);
#endif
    lua_assert(tt != LUA_TNIL || tvisnil(o));
    return tt;
  }
}

LUALIB_API void luaL_checktype(lua_State *L, int idx, int tt)
{
  if (lua_type(L, idx) != tt)
    lj_err_argt(L, idx, tt);
}

LUALIB_API void luaL_checkany(lua_State *L, int idx)
{
  if (index2adr(L, idx) == niltv(L))
    lj_err_arg(L, idx, LJ_ERR_NOVAL);
}

LUA_API const char *lua_typename(lua_State *L, int t)
{
  UNUSED(L);
  return lj_obj_typename[t+1];
}

LUA_API int lua_iscfunction(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return tvisfunc(o) && !isluafunc(funcV(o));
}

LUA_API int lua_isnumber(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  return (tvisnumber(o) || (tvisstr(o) && lj_strscan_number(strV(o), &tmp)));
}

LUA_API int lua_isstring(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return (tvisstr(o) || tvisnumber(o));
}

LUA_API int lua_isuserdata(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return (tvisudata(o) || tvislightud(o));
}

LUA_API int lua_rawequal(lua_State *L, int idx1, int idx2)
{
  cTValue *o1 = index2adr(L, idx1);
  cTValue *o2 = index2adr(L, idx2);
  return (o1 == niltv(L) || o2 == niltv(L)) ? 0 : lj_obj_equal(o1, o2);
}

LUA_API int lua_equal(lua_State *L, int idx1, int idx2)
{
  cTValue *o1 = index2adr(L, idx1);
  cTValue *o2 = index2adr(L, idx2);
  if (tvisint(o1) && tvisint(o2)) {
    return intV(o1) == intV(o2);
  } else if (tvisnumber(o1) && tvisnumber(o2)) {
    return numberVnum(o1) == numberVnum(o2);
  } else if (itype(o1) != itype(o2)) {
    return 0;
  } else if (tvispri(o1)) {
    return o1 != niltv(L) && o2 != niltv(L);
#if LJ_64
  } else if (tvislightud(o1)) {
    return o1->u64 == o2->u64;
#endif
  } else if (gcrefeq(o1->gcr, o2->gcr)) {
    return 1;
  } else if (!tvistabud(o1)) {
    return 0;
  } else {
    TValue *base = lj_meta_equal(L, gcV(o1), gcV(o2), 0);
    if ((uintptr_t)base <= 1) {
      return (int)(uintptr_t)base;
    } else {
      L->top = base+2;
      lj_vm_call(L, base, 1+1);
      L->top -= 2;
      return tvistruecond(L->top+1);
    }
  }
}

LUA_API int lua_lessthan(lua_State *L, int idx1, int idx2)
{
  cTValue *o1 = index2adr(L, idx1);
  cTValue *o2 = index2adr(L, idx2);
  if (o1 == niltv(L) || o2 == niltv(L)) {
    return 0;
  } else if (tvisint(o1) && tvisint(o2)) {
    return intV(o1) < intV(o2);
  } else if (tvisnumber(o1) && tvisnumber(o2)) {
    return numberVnum(o1) < numberVnum(o2);
  } else {
    TValue *base = lj_meta_comp(L, o1, o2, 0);
    if ((uintptr_t)base <= 1) {
      return (int)(uintptr_t)base;
    } else {
      L->top = base+2;
      lj_vm_call(L, base, 1+1);
      L->top -= 2;
      return tvistruecond(L->top+1);
    }
  }
}

LUA_API lua_Number lua_tonumber(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  if (LJ_LIKELY(tvisnumber(o)))
    return numberVnum(o);
  else if (tvisstr(o) && lj_strscan_num(strV(o), &tmp))
    return numV(&tmp);
  else
    return 0;
}

LUALIB_API lua_Number luaL_checknumber(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  if (LJ_LIKELY(tvisnumber(o)))
    return numberVnum(o);
  else if (!(tvisstr(o) && lj_strscan_num(strV(o), &tmp)))
    lj_err_argt(L, idx, LUA_TNUMBER);
  return numV(&tmp);
}

LUALIB_API lua_Number luaL_optnumber(lua_State *L, int idx, lua_Number def)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  if (LJ_LIKELY(tvisnumber(o)))
    return numberVnum(o);
  else if (tvisnil(o))
    return def;
  else if (!(tvisstr(o) && lj_strscan_num(strV(o), &tmp)))
    lj_err_argt(L, idx, LUA_TNUMBER);
  return numV(&tmp);
}

LUA_API lua_Integer lua_tointeger(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  lua_Number n;
  if (LJ_LIKELY(tvisint(o))) {
    return intV(o);
  } else if (LJ_LIKELY(tvisnum(o))) {
    n = numV(o);
  } else {
    if (!(tvisstr(o) && lj_strscan_number(strV(o), &tmp)))
      return 0;
    if (tvisint(&tmp))
      return (lua_Integer)intV(&tmp);
    n = numV(&tmp);
  }
#if LJ_64
  return (lua_Integer)n;
#else
  return lj_num2int(n);
#endif
}

LUALIB_API lua_Integer luaL_checkinteger(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  lua_Number n;
  if (LJ_LIKELY(tvisint(o))) {
    return intV(o);
  } else if (LJ_LIKELY(tvisnum(o))) {
    n = numV(o);
  } else {
    if (!(tvisstr(o) && lj_strscan_number(strV(o), &tmp)))
      lj_err_argt(L, idx, LUA_TNUMBER);
    if (tvisint(&tmp))
      return (lua_Integer)intV(&tmp);
    n = numV(&tmp);
  }
#if LJ_64
  return (lua_Integer)n;
#else
  return lj_num2int(n);
#endif
}

LUALIB_API lua_Integer luaL_optinteger(lua_State *L, int idx, lua_Integer def)
{
  cTValue *o = index2adr(L, idx);
  TValue tmp;
  lua_Number n;
  if (LJ_LIKELY(tvisint(o))) {
    return intV(o);
  } else if (LJ_LIKELY(tvisnum(o))) {
    n = numV(o);
  } else if (tvisnil(o)) {
    return def;
  } else {
    if (!(tvisstr(o) && lj_strscan_number(strV(o), &tmp)))
      lj_err_argt(L, idx, LUA_TNUMBER);
    if (tvisint(&tmp))
      return (lua_Integer)intV(&tmp);
    n = numV(&tmp);
  }
#if LJ_64
  return (lua_Integer)n;
#else
  return lj_num2int(n);
#endif
}

LUA_API int lua_toboolean(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return tvistruecond(o);
}

LUA_API const char *lua_tolstring(lua_State *L, int idx, size_t *len)
{
  TValue *o = index2adr(L, idx);
  GCstr *s;
  if (LJ_LIKELY(tvisstr(o))) {
    s = strV(o);
  } else if (tvisnumber(o)) {
    lj_gc_check(L);
    o = index2adr(L, idx);  /* GC may move the stack. */
    s = lj_str_fromnumber(L, o);
    setstrV(L, o, s);
  } else {
    if (len != NULL) *len = 0;
    return NULL;
  }
  if (len != NULL) *len = s->len;
  return strdata(s);
}

LUALIB_API const char *luaL_checklstring(lua_State *L, int idx, size_t *len)
{
  TValue *o = index2adr(L, idx);
  GCstr *s;
  if (LJ_LIKELY(tvisstr(o))) {
    s = strV(o);
  } else if (tvisnumber(o)) {
    lj_gc_check(L);
    o = index2adr(L, idx);  /* GC may move the stack. */
    s = lj_str_fromnumber(L, o);
    setstrV(L, o, s);
  } else {
    lj_err_argt(L, idx, LUA_TSTRING);
  }
  if (len != NULL) *len = s->len;
  return strdata(s);
}

LUALIB_API const char *luaL_optlstring(lua_State *L, int idx,
				       const char *def, size_t *len)
{
  TValue *o = index2adr(L, idx);
  GCstr *s;
  if (LJ_LIKELY(tvisstr(o))) {
    s = strV(o);
  } else if (tvisnil(o)) {
    if (len != NULL) *len = def ? strlen(def) : 0;
    return def;
  } else if (tvisnumber(o)) {
    lj_gc_check(L);
    o = index2adr(L, idx);  /* GC may move the stack. */
    s = lj_str_fromnumber(L, o);
    setstrV(L, o, s);
  } else {
    lj_err_argt(L, idx, LUA_TSTRING);
  }
  if (len != NULL) *len = s->len;
  return strdata(s);
}

LUALIB_API int luaL_checkoption(lua_State *L, int idx, const char *def,
				const char *const lst[])
{
  ptrdiff_t i;
  const char *s = lua_tolstring(L, idx, NULL);
  if (s == NULL && (s = def) == NULL)
    lj_err_argt(L, idx, LUA_TSTRING);
  for (i = 0; lst[i]; i++)
    if (strcmp(lst[i], s) == 0)
      return (int)i;
  lj_err_argv(L, idx, LJ_ERR_INVOPTM, s);
}

LUA_API size_t lua_objlen(lua_State *L, int idx)
{
  TValue *o = index2adr(L, idx);
  if (tvisstr(o)) {
    return strV(o)->len;
  } else if (tvistab(o)) {
    return (size_t)lj_tab_len(tabV(o));
  } else if (tvisudata(o)) {
    return udataV(o)->len;
  } else if (tvisnumber(o)) {
    GCstr *s = lj_str_fromnumber(L, o);
    setstrV(L, o, s);
    return s->len;
  } else {
    return 0;
  }
}

LUA_API lua_CFunction lua_tocfunction(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  if (tvisfunc(o)) {
    BCOp op = bc_op(*mref(funcV(o)->c.pc, BCIns));
    if (op == BC_FUNCC || op == BC_FUNCCW)
      return funcV(o)->c.f;
  }
  return NULL;
}

LUA_API void *lua_touserdata(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  if (tvisudata(o))
    return uddata(udataV(o));
  else if (tvislightud(o))
    return lightudV(o);
  else
    return NULL;
}

LUA_API lua_State *lua_tothread(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  return (!tvisthread(o)) ? NULL : threadV(o);
}

LUA_API const void *lua_topointer(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  if (tvisudata(o))
    return uddata(udataV(o));
  else if (tvislightud(o))
    return lightudV(o);
  else if (tviscdata(o))
    return cdataptr(cdataV(o));
else if (tvistab(o)) return tabV(o)->end - tabV(o)->intn; // fancy
  else if (tvisgcv(o))
    return gcV(o);
  else
    return NULL;
}

/* -- Stack setters (object creation) ------------------------------------- */

LUA_API void lua_pushnil(lua_State *L)
{
  setnilV(L->top);
  incr_top(L);
}

LUA_API void lua_pushnumber(lua_State *L, lua_Number n)
{
  setnumV(L->top, n);
  if (LJ_UNLIKELY(tvisnan(L->top)))
    setnanV(L->top);  /* Canonicalize injected NaNs. */
  incr_top(L);
}

LUA_API void lua_pushinteger(lua_State *L, lua_Integer n)
{
  setintptrV(L->top, n);
  incr_top(L);
}

LUA_API void lua_pushlstring(lua_State *L, const char *str, size_t len)
{
  GCstr *s;
  lj_gc_check(L);
  s = lj_str_new(L, str, len);
  setstrV(L, L->top, s);
  incr_top(L);
}

LUA_API void lua_pushstring(lua_State *L, const char *str)
{
  if (str == NULL) {
    setnilV(L->top);
  } else {
    GCstr *s;
    lj_gc_check(L);
    s = lj_str_newz(L, str);
    setstrV(L, L->top, s);
  }
  incr_top(L);
}

LUA_API const char *lua_pushvfstring(lua_State *L, const char *fmt,
				     va_list argp)
{
  lj_gc_check(L);
  return lj_str_pushvf(L, fmt, argp);
}

LUA_API const char *lua_pushfstring(lua_State *L, const char *fmt, ...)
{
  const char *ret;
  va_list argp;
  lj_gc_check(L);
  va_start(argp, fmt);
  ret = lj_str_pushvf(L, fmt, argp);
  va_end(argp);
  return ret;
}

LUA_API void lua_pushcclosure(lua_State *L, lua_CFunction f, int n)
{
  GCfunc *fn;
  lj_gc_check(L);
  api_checknelems(L, n);
  fn = lj_func_newC(L, (MSize)n, getcurrenv(L));
  fn->c.f = f;
  L->top -= n;
  while (n--)
    copyTV(L, &fn->c.upvalue[n], L->top+n);
  setfuncV(L, L->top, fn);
  lua_assert(iswhite(obj2gco(fn)));
  incr_top(L);
}

LUA_API void lua_pushboolean(lua_State *L, int b)
{
  setboolV(L->top, (b != 0));
  incr_top(L);
}

LUA_API void lua_pushlightuserdata(lua_State *L, void *p)
{
  setlightudV(L->top, checklightudptr(L, p));
  incr_top(L);
}
//fancy
void (*lua_logtable) ( const char* name, const char* source, int line ) = NULL;
void lua_calltabcb(lua_State *lua, void *t)
{
	lua_Debug d;
	if(!lua_logtable) return;
	if (!lua->base) return;
	if (lua_getstack(lua, 0, &d) && lua_getinfo(lua, "nlS", &d))
		lua_logtable(d.name ? d.name : "", d.source+(d.source[0]=='@'), d.currentline);
}

LUA_API void lua_createtable(lua_State *L, int narray, int nrec)
{
  GCtab *t;
  lj_gc_check(L);
  t = lj_tab_new(L, (uint32_t)(narray > 0 ? narray+1 : 0), hsize2hbits(nrec));
  settabV(L, L->top, t);
  incr_top(L);
}

//fancy
LUA_API void lua_sizetable(lua_State *L, int idx)
{
  GCtab *t;
  lj_gc_check(L);
  t = tabV(index2adr(L, idx));
  lua_pushnumber(L,t->asize);
  lua_pushnumber(L,t->hmask);
}

//fancy
LUA_API void lua_duplicatetable(lua_State *L, int idx)
{
  GCtab *t;
  lj_gc_check(L);
  t = lj_tab_dup(L, tabV(index2adr(L, idx)));
  settabV(L, L->top, t);
  incr_top(L);
}

//fancy
LUA_API void lua_cleartable(lua_State *L, int idx)
{
  /*
  global_State *g = G(L);
  GCtab *t;
  lj_gc_check(L);
  t = tabV(index2adr(L, idx));
  lj_tab_free(g, t);
  settabV(L, L->top, t);
  incr_top(L);
  */
	GCtab *t = tabV(index2adr(L, idx));
	lj_tab_clear(L, t);
	//lj_gc_anybarriert(L, t);
}

LUALIB_API int luaL_newmetatable(lua_State *L, const char *tname)
{
  GCtab *regt = tabV(registry(L));
  TValue *tv = lj_tab_setstr(L, regt, lj_str_newz(L, tname));
  if (tvisnil(tv)) {
    GCtab *mt = lj_tab_new(L, 0, 1);
    settabV(L, tv, mt);
    settabV(L, L->top++, mt);
    lj_gc_anybarriert(L, regt);
    return 1;
  } else {
    copyTV(L, L->top++, tv);
    return 0;
  }
}

LUA_API int lua_pushthread(lua_State *L)
{
  setthreadV(L, L->top, L);
  incr_top(L);
  return (mainthread(G(L)) == L);
}

LUA_API lua_State *lua_newthread(lua_State *L)
{
  lua_State *L1;
  lj_gc_check(L);
  L1 = lj_state_new(L);
  setthreadV(L, L->top, L1);
  incr_top(L);
  return L1;
}

LUA_API void *lua_newuserdata(lua_State *L, size_t size)
{
  GCudata *ud;
  lj_gc_check(L);
  if (size > LJ_MAX_UDATA)
    lj_err_msg(L, LJ_ERR_UDATAOV);
  ud = lj_udata_new(L, (MSize)size, getcurrenv(L));
  setudataV(L, L->top, ud);
  incr_top(L);
  return uddata(ud);
}

LUA_API void lua_concat(lua_State *L, int n)
{
  api_checknelems(L, n);
  if (n >= 2) {
    n--;
    do {
      TValue *top = lj_meta_cat(L, L->top-1, -n);
      if (top == NULL) {
	L->top -= n;
	break;
      }
      n -= (int)(L->top - top);
      L->top = top+2;
      lj_vm_call(L, top, 1+1);
      L->top--;
      copyTV(L, L->top-1, L->top);
    } while (--n > 0);
  } else if (n == 0) {  /* Push empty string. */
    setstrV(L, L->top, &G(L)->strempty);
    incr_top(L);
  }
  /* else n == 1: nothing to do. */
}

/* -- Object getters ------------------------------------------------------ */

LUA_API void lua_gettable(lua_State *L, int idx)
{
  cTValue *v, *t = index2adr(L, idx);
  api_checkvalidindex(L, t);
  v = lj_meta_tget(L, t, L->top-1);
  if (v == NULL) {
    L->top += 2;
    lj_vm_call(L, L->top-2, 1+1);
    L->top -= 2;
    v = L->top+1;
  }
  copyTV(L, L->top-1, v);
}

LUA_API void lua_getfield(lua_State *L, int idx, const char *k)
{
  cTValue *v, *t = index2adr(L, idx);
  TValue key;
  api_checkvalidindex(L, t);
  setstrV(L, &key, lj_str_newz(L, k));
  v = lj_meta_tget(L, t, &key);
  if (v == NULL) {
    L->top += 2;
    lj_vm_call(L, L->top-2, 1+1);
    L->top -= 2;
    v = L->top+1;
  }
  copyTV(L, L->top, v);
  incr_top(L);
}

LUA_API void lua_rawget(lua_State *L, int idx)
{
  cTValue *t = index2adr(L, idx);
  api_check(L, tvistab(t));
  copyTV(L, L->top-1, lj_tab_get(L, tabV(t), L->top-1));
}

LUA_API void lua_rawgeti(lua_State *L, int idx, int n)
{
  cTValue *v, *t = index2adr(L, idx);
  api_check(L, tvistab(t));
  v = lj_tab_getint(tabV(t), n);
  if (v) {
    copyTV(L, L->top, v);
  } else {
    setnilV(L->top);
  }
  incr_top(L);
}

LUA_API int lua_getmetatable(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  GCtab *mt = NULL;
  if (tvistab(o))
    mt = tabref(tabV(o)->metatable);
  else if (tvisudata(o))
    mt = tabref(udataV(o)->metatable);
  else
    mt = tabref(basemt_obj(G(L), o));
  if (mt == NULL)
    return 0;
  settabV(L, L->top, mt);
  incr_top(L);
  return 1;
}

LUALIB_API int luaL_getmetafield(lua_State *L, int idx, const char *field)
{
  if (lua_getmetatable(L, idx)) {
    cTValue *tv = lj_tab_getstr(tabV(L->top-1), lj_str_newz(L, field));
    if (tv && !tvisnil(tv)) {
      copyTV(L, L->top-1, tv);
      return 1;
    }
    L->top--;
  }
  return 0;
}

LUA_API void lua_getfenv(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  api_checkvalidindex(L, o);
  if (tvisfunc(o)) {
    settabV(L, L->top, tabref(funcV(o)->c.env));
  } else if (tvisudata(o)) {
    settabV(L, L->top, tabref(udataV(o)->env));
  } else if (tvisthread(o)) {
    settabV(L, L->top, tabref(threadV(o)->env));
  } else {
    setnilV(L->top);
  }
  incr_top(L);
}

LUA_API int lua_next(lua_State *L, int idx)
{
  cTValue *t = index2adr(L, idx);
  int more;
  api_check(L, tvistab(t));
  more = lj_tab_next(L, tabV(t), L->top-1);
  if (more) {
    incr_top(L);  /* Return new key and value slot. */
  } else {  /* End of traversal. */
    L->top--;  /* Remove key slot. */
  }
  return more;
}

LUA_API const char *lua_getupvalue(lua_State *L, int idx, int n)
{
  TValue *val;
  const char *name = lj_debug_uvnamev(index2adr(L, idx), (uint32_t)(n-1), &val);
  if (name) {
    copyTV(L, L->top, val);
    incr_top(L);
  }
  return name;
}

LUA_API void *lua_upvalueid(lua_State *L, int idx, int n)
{
  GCfunc *fn = funcV(index2adr(L, idx));
  n--;
  api_check(L, (uint32_t)n < fn->l.nupvalues);
  return isluafunc(fn) ? (void *)gcref(fn->l.uvptr[n]) :
			 (void *)&fn->c.upvalue[n];
}

LUA_API void lua_upvaluejoin(lua_State *L, int idx1, int n1, int idx2, int n2)
{
  GCfunc *fn1 = funcV(index2adr(L, idx1));
  GCfunc *fn2 = funcV(index2adr(L, idx2));
  n1--; n2--;
  api_check(L, isluafunc(fn1) && (uint32_t)n1 < fn1->l.nupvalues);
  api_check(L, isluafunc(fn2) && (uint32_t)n2 < fn2->l.nupvalues);
  setgcrefr(fn1->l.uvptr[n1], fn2->l.uvptr[n2]);
  lj_gc_objbarrier(L, fn1, gcref(fn1->l.uvptr[n1]));
}

LUALIB_API void *luaL_checkudata(lua_State *L, int idx, const char *tname)
{
  cTValue *o = index2adr(L, idx);
  if (tvisudata(o)) {
    GCudata *ud = udataV(o);
    cTValue *tv = lj_tab_getstr(tabV(registry(L)), lj_str_newz(L, tname));
    if (tv && tvistab(tv) && tabV(tv) == tabref(ud->metatable))
      return uddata(ud);
  }
  lj_err_argtype(L, idx, tname);
  return NULL;  /* unreachable */
}

/* -- Object setters ------------------------------------------------------ */

LUA_API void lua_settable(lua_State *L, int idx)
{
  TValue *o;
  cTValue *t = index2adr(L, idx);
  api_checknelems(L, 2);
  api_checkvalidindex(L, t);
  o = lj_meta_tset(L, t, L->top-2);
  if (o) {
    /* NOBARRIER: lj_meta_tset ensures the table is not black. */
    copyTV(L, o, L->top-1);
    L->top -= 2;
  } else {
    L->top += 3;
    copyTV(L, L->top-1, L->top-6);
    lj_vm_call(L, L->top-3, 0+1);
    L->top -= 3;
  }
}

LUA_API void lua_setfield(lua_State *L, int idx, const char *k)
{
  TValue *o;
  TValue key;
  cTValue *t = index2adr(L, idx);
  api_checknelems(L, 1);
  api_checkvalidindex(L, t);
  setstrV(L, &key, lj_str_newz(L, k));
  o = lj_meta_tset(L, t, &key);
  if (o) {
    L->top--;
    /* NOBARRIER: lj_meta_tset ensures the table is not black. */
    copyTV(L, o, L->top);
  } else {
    L->top += 3;
    copyTV(L, L->top-1, L->top-6);
    lj_vm_call(L, L->top-3, 0+1);
    L->top -= 2;
  }
}

LUA_API void lua_rawset(lua_State *L, int idx)
{
  GCtab *t = tabV(index2adr(L, idx));
  TValue *dst, *key;
  api_checknelems(L, 2);
  key = L->top-2;
  dst = lj_tab_set(L, t, key);
  copyTV(L, dst, key+1);
  lj_gc_anybarriert(L, t);
  L->top = key;
}

LUA_API void lua_rawseti(lua_State *L, int idx, int n)
{
  GCtab *t = tabV(index2adr(L, idx));
  TValue *dst, *src;
  api_checknelems(L, 1);
  dst = lj_tab_setint(L, t, n);
  src = L->top-1;
  copyTV(L, dst, src);
  lj_gc_barriert(L, t, dst);
  L->top = src;
}

LUA_API int lua_setmetatable(lua_State *L, int idx)
{
  global_State *g;
  GCtab *mt;
  cTValue *o = index2adr(L, idx);
  api_checknelems(L, 1);
  api_checkvalidindex(L, o);
  if (tvisnil(L->top-1)) {
    mt = NULL;
  } else {
    api_check(L, tvistab(L->top-1));
    mt = tabV(L->top-1);
  }
  g = G(L);
  if (tvistab(o)) {
    setgcref(tabV(o)->metatable, obj2gco(mt));
    if (mt)
      lj_gc_objbarriert(L, tabV(o), mt);
  } else if (tvisudata(o)) {
    setgcref(udataV(o)->metatable, obj2gco(mt));
    if (mt)
      lj_gc_objbarrier(L, udataV(o), mt);
  } else {
    /* Flush cache, since traces specialize to basemt. But not during __gc. */
    if (lj_trace_flushall(L))
      lj_err_caller(L, LJ_ERR_NOGCMM);
    if (tvisbool(o)) {
      /* NOBARRIER: basemt is a GC root. */
      setgcref(basemt_it(g, LJ_TTRUE), obj2gco(mt));
      setgcref(basemt_it(g, LJ_TFALSE), obj2gco(mt));
    } else {
      /* NOBARRIER: basemt is a GC root. */
      setgcref(basemt_obj(g, o), obj2gco(mt));
    }
  }
  L->top--;
  return 1;
}

LUA_API int lua_setfenv(lua_State *L, int idx)
{
  cTValue *o = index2adr(L, idx);
  GCtab *t;
  api_checknelems(L, 1);
  api_checkvalidindex(L, o);
  api_check(L, tvistab(L->top-1));
  t = tabV(L->top-1);
  if (tvisfunc(o)) {
    setgcref(funcV(o)->c.env, obj2gco(t));
  } else if (tvisudata(o)) {
    setgcref(udataV(o)->env, obj2gco(t));
  } else if (tvisthread(o)) {
    setgcref(threadV(o)->env, obj2gco(t));
  } else {
    L->top--;
    return 0;
  }
  lj_gc_objbarrier(L, gcV(o), t);
  L->top--;
  return 1;
}

LUA_API const char *lua_setupvalue(lua_State *L, int idx, int n)
{
  cTValue *f = index2adr(L, idx);
  TValue *val;
  const char *name;
  api_checknelems(L, 1);
  name = lj_debug_uvnamev(f, (uint32_t)(n-1), &val);
  if (name) {
    L->top--;
    copyTV(L, val, L->top);
    lj_gc_barrier(L, funcV(f), L->top);
  }
  return name;
}

/* -- Calls --------------------------------------------------------------- */

LUA_API void lua_call(lua_State *L, int nargs, int nresults)
{
  api_check(L, L->status == 0 || L->status == LUA_ERRERR);
  api_checknelems(L, nargs+1);
  lj_vm_call(L, L->top - nargs, nresults+1);
}

LUA_API int lua_pcall(lua_State *L, int nargs, int nresults, int errfunc)
{
  global_State *g = G(L);
  uint8_t oldh = hook_save(g);
  ptrdiff_t ef;
  int status;
  api_check(L, L->status == 0 || L->status == LUA_ERRERR);
  api_checknelems(L, nargs+1);
  if (errfunc == 0) {
    ef = 0;
  } else {
    cTValue *o = stkindex2adr(L, errfunc);
    api_checkvalidindex(L, o);
    ef = savestack(L, o);
  }
  status = lj_vm_pcall(L, L->top - nargs, nresults+1, ef);
  if (status) hook_restore(g, oldh);
  return status;
}

static TValue *cpcall(lua_State *L, lua_CFunction func, void *ud)
{
  GCfunc *fn = lj_func_newC(L, 0, getcurrenv(L));
  fn->c.f = func;
  setfuncV(L, L->top, fn);
  setlightudV(L->top+1, checklightudptr(L, ud));
  cframe_nres(L->cframe) = 1+0;  /* Zero results. */
  L->top += 2;
  return L->top-1;  /* Now call the newly allocated C function. */
}

LUA_API int lua_cpcall(lua_State *L, lua_CFunction func, void *ud)
{
  global_State *g = G(L);
  uint8_t oldh = hook_save(g);
  int status;
  api_check(L, L->status == 0 || L->status == LUA_ERRERR);
  status = lj_vm_cpcall(L, func, ud, cpcall);
  if (status) hook_restore(g, oldh);
  return status;
}

LUALIB_API int luaL_callmeta(lua_State *L, int idx, const char *field)
{
  if (luaL_getmetafield(L, idx, field)) {
    TValue *base = L->top--;
    copyTV(L, base, index2adr(L, idx));
    L->top = base+1;
    lj_vm_call(L, base, 1+1);
    return 1;
  }
  return 0;
}

/* -- Coroutine yield and resume ------------------------------------------ */

LUA_API int lua_yield(lua_State *L, int nresults)
{
  void *cf = L->cframe;
  global_State *g = G(L);
  if (cframe_canyield(cf)) {
    cf = cframe_raw(cf);
    if (!hook_active(g)) {  /* Regular yield: move results down if needed. */
      cTValue *f = L->top - nresults;
      if (f > L->base) {
	TValue *t = L->base;
	while (--nresults >= 0) copyTV(L, t++, f++);
	L->top = t;
      }
      L->cframe = NULL;
      L->status = LUA_YIELD;
      return -1;
    } else {  /* Yield from hook: add a pseudo-frame. */
      TValue *top = L->top;
      hook_leave(g);
      top->u64 = cframe_multres(cf);
      setcont(top+1, lj_cont_hook);
      setframe_pc(top+1, cframe_pc(cf)-1);
      setframe_gc(top+2, obj2gco(L));
      setframe_ftsz(top+2, (int)((char *)(top+3)-(char *)L->base)+FRAME_CONT);
      L->top = L->base = top+3;
#if LJ_TARGET_X64
      lj_err_throw(L, LUA_YIELD);
#else
      L->cframe = NULL;
      L->status = LUA_YIELD;
      lj_vm_unwind_c(cf, LUA_YIELD);
#endif
    }
  }
  lj_err_msg(L, LJ_ERR_CYIELD);
  return 0;  /* unreachable */
}

LUA_API int lua_resume(lua_State *L, int nargs)
{
  if (L->cframe == NULL && L->status <= LUA_YIELD)
    return lj_vm_resume(L, L->top - nargs, 0, 0);
  L->top = L->base;
  setstrV(L, L->top, lj_err_str(L, LJ_ERR_COSUSP));
  incr_top(L);
  return LUA_ERRRUN;
}

/* -- GC and memory management -------------------------------------------- */

LUA_API int lua_gc(lua_State *L, int what, int data)
{
  global_State *g = G(L);
  int res = 0;
  switch (what) {
  case LUA_GCSTOP:
    g->gc.threshold = LJ_MAX_MEM;
    break;
  case LUA_GCRESTART:
    g->gc.threshold = data == -1 ? (g->gc.total/100)*g->gc.pause : g->gc.total;
    break;
  case LUA_GCCOLLECT:
    lj_gc_fullgc(L);
    break;
  case LUA_GCCOUNT:
    res = (int)(g->gc.total >> 10);
    break;
  case LUA_GCCOUNTB:
    res = (int)(g->gc.total & 0x3ff);
    break;
  case LUA_GCSTEP: {
    MSize a = (MSize)data << 10;
    g->gc.threshold = (a <= g->gc.total) ? (g->gc.total - a) : 0;
    while (g->gc.total >= g->gc.threshold)
      if (lj_gc_step(L)) {
	res = 1;
	break;
      }
    break;
  }
  case LUA_GCSETPAUSE:
    res = (int)(g->gc.pause);
    g->gc.pause = (MSize)data;
    break;
  case LUA_GCSETSTEPMUL:
    res = (int)(g->gc.stepmul);
    g->gc.stepmul = (MSize)data;
    break;
  default:
    res = -1;  /* Invalid option. */
  }
  return res;
}

LUA_API lua_Alloc lua_getallocf(lua_State *L, void **ud)
{
  global_State *g = G(L);
  if (ud) *ud = g->allocd;
  return g->allocf;
}

LUA_API void lua_setallocf(lua_State *L, lua_Alloc f, void *ud)
{
  global_State *g = G(L);
  g->allocd = ud;
  g->allocf = f;
}

// fancy

LUA_API void lua_rawgetk(lua_State *lua, int table, int key)
{
	cTValue *t = index2adr(lua, table);
	api_checkvalidindex(lua, t);
	copyTV(lua, lua->top, lj_tab_get(lua, tabV(t), index2adr(lua, key)));
	incr_top(lua);
}
LUA_API void lua_rawgetn(lua_State *lua, int table, const char *key)
{
	cTValue *t = index2adr(lua, table), *v;
	api_checkvalidindex(lua, t);
	v = lj_tab_getstr(tabV(t), lj_str_newz(lua, key));
	copyTV(lua, lua->top, v ? v : niltv(lua));
	incr_top(lua);
}
LUA_API void lua_rawsetk(lua_State *lua, int table, int key)
{
	GCtab *t = tabV(index2adr(lua, table));
	api_checknelems(lua, 1);
	copyTV(lua, lj_tab_set(lua, t, index2adr(lua, key)), lua->top-1);
	lj_gc_anybarriert(lua, t);
	lua->top--;
}
LUA_API void lua_rawsetn(lua_State *lua, int table, const char *key)
{
	GCtab *t = tabV(index2adr(lua, table));
	api_checknelems(lua, 1);
	t->nomm = 0; // see lj_tab_set
	copyTV(lua, lj_tab_setstr(lua, t, lj_str_newz(lua, key)), lua->top-1);
	lj_gc_anybarriert(lua, t);
	lua->top--;
}
LUA_API void lua_rawsetv(lua_State *lua, int table, int value)
{
	GCtab *t = tabV(index2adr(lua, table));
	api_checknelems(lua, 1);
	copyTV(lua, lj_tab_set(lua, t, lua->top-1), index2adr(lua, value));
	lj_gc_anybarriert(lua, t);
	lua->top--;
}
LUA_API void lua_rawsetkv(lua_State *lua, int table, int key, int value)
{
	GCtab *t = tabV(index2adr(lua, table));
	copyTV(lua, lj_tab_set(lua, t, index2adr(lua, key)), index2adr(lua, value));
	lj_gc_anybarriert(lua, t);
}
LUA_API void lua_rawsetiv(lua_State *lua, int table, int key, int value)
{
	GCtab *t = tabV(index2adr(lua, table));
	copyTV(lua, lj_tab_setint(lua, t, key), index2adr(lua, value));
	lj_gc_anybarriert(lua, t);
}
LUA_API void lua_rawsetnv(lua_State *lua, int table, const char *key, int value)
{
	GCtab *t = tabV(index2adr(lua, table));
	t->nomm = 0; // see lj_tab_set
	copyTV(lua, lj_tab_setstr(lua, t, lj_str_newz(lua, key)), index2adr(lua, value));
	lj_gc_anybarriert(lua, t);
}

// fancy: gettable settable setfield
LUA_API void lua_gettablek(lua_State *lua, int table, int key)
{
	cTValue *v, *t = index2adr(lua, table);
	api_checkvalidindex(lua, t);
	copyTV(lua, lua->top, index2adr(lua, key));
	incr_top(lua);
	v = lj_meta_tget(lua, t, lua->top-1);
	if (v == NULL) {
		lua->top += 2;
		lj_vm_call(lua, lua->top-2, 1+1);
		lua->top -= 2;
		v = lua->top+1;
	}
	copyTV(lua, lua->top-1, v);
}
LUA_API void lua_gettablei(lua_State *lua, int table, int key)
{
	cTValue *v, *t = index2adr(lua, table);
	api_checkvalidindex(lua, t);
	setintptrV(lua->top, key);
	incr_top(lua);
	v = lj_meta_tget(lua, t, lua->top-1);
	if (v == NULL) {
		lua->top += 2;
		lj_vm_call(lua, lua->top-2, 1+1);
		lua->top -= 2;
		v = lua->top+1;
	}
	copyTV(lua, lua->top-1, v);
}
LUA_API void lua_settablek(lua_State *lua, int table, int key)
{
	TValue *o;
	cTValue *t = index2adr(lua, table);
	api_checknelems(lua, 1);
	api_checkvalidindex(lua, t);
	o = lj_meta_tset(lua, t, index2adr(lua, key));
	if (o) {
		lua->top--;
		/* NOBARRIER: lj_meta_tset ensures the table is not black. */
		copyTV(lua, o, lua->top);
	} else {
		lua->top += 3;
		copyTV(lua, lua->top-1, lua->top-6);
		lj_vm_call(lua, lua->top-3, 0+1);
		lua->top -= 2;
	}
}
LUA_API void lua_settablei(lua_State *lua, int table, int key)
{
	TValue *o, k;
	cTValue *t = index2adr(lua, table);
	api_checknelems(lua, 1);
	api_checkvalidindex(lua, t);
	setintptrV(&k, key);
	o = lj_meta_tset(lua, t, &k);
	if (o) {
		lua->top--;
		/* NOBARRIER: lj_meta_tset ensures the table is not black. */
		copyTV(lua, o, lua->top);
	} else {
		lua->top += 3;
		copyTV(lua, lua->top-1, lua->top-6);
		lj_vm_call(lua, lua->top-3, 0+1);
		lua->top -= 2;
	}
}
LUA_API void lua_settablev(lua_State *lua, int table, int value)
{
	TValue *o;
	cTValue *t = index2adr(lua, table);
	api_checknelems(lua, 1);
	api_checkvalidindex(lua, t);
	copyTV(lua, lua->top, index2adr(lua, value));
	incr_top(lua);
	o = lj_meta_tset(lua, t, lua->top-2);
	if (o) {
		/* NOBARRIER: lj_meta_tset ensures the table is not black. */
		copyTV(lua, o, lua->top-1);
		lua->top -= 2;
	} else {
		lua->top += 3;
		copyTV(lua, lua->top-1, lua->top-6);
		lj_vm_call(lua, lua->top-3, 0+1);
		lua->top -= 3;
	}
}
LUA_API void lua_settablekv(lua_State *lua, int table, int key, int value)
{
	TValue *o;
	cTValue *t = index2adr(lua, table);
	api_checkvalidindex(lua, t);
	copyTV(lua, lua->top, index2adr(lua, value));
	incr_top(lua);
	o = lj_meta_tset(lua, t, index2adr(lua, key));
	if (o) {
		lua->top--;
		/* NOBARRIER: lj_meta_tset ensures the table is not black. */
		copyTV(lua, o, lua->top);
	} else {
		lua->top += 3;
		copyTV(lua, lua->top-1, lua->top-6);
		lj_vm_call(lua, lua->top-3, 0+1);
		lua->top -= 2;
	}
}
LUA_API void lua_settableiv(lua_State *lua, int table, int key, int value)
{
	TValue *o, k;
	cTValue *t = index2adr(lua, table);
	api_checkvalidindex(lua, t);
	copyTV(lua, lua->top, index2adr(lua, value));
	incr_top(lua);
	setintptrV(&k, key);
	o = lj_meta_tset(lua, t, &k);
	if (o) {
		lua->top--;
		/* NOBARRIER: lj_meta_tset ensures the table is not black. */
		copyTV(lua, o, lua->top);
	} else {
		lua->top += 3;
		copyTV(lua, lua->top-1, lua->top-6);
		lj_vm_call(lua, lua->top-3, 0+1);
		lua->top -= 2;
	}
}
LUA_API void lua_setfieldv(lua_State *lua, int table, const char *key, int value)
{
	TValue *o, k;
	cTValue *t = index2adr(lua, table);
	api_checkvalidindex(lua, t);
	copyTV(lua, lua->top, index2adr(lua, value));
	incr_top(lua);
	setstrV(lua, &k, lj_str_newz(lua, key));
	o = lj_meta_tset(lua, t, &k);
	if (o) {
		lua->top--;
		/* NOBARRIER: lj_meta_tset ensures the table is not black. */
		copyTV(lua, o, lua->top);
	} else {
		lua->top += 3;
		copyTV(lua, lua->top-1, lua->top-6);
		lj_vm_call(lua, lua->top-3, 0+1);
		lua->top -= 2;
	}
}

LUA_API void *lua_totable(lua_State *lua, int table)
{
	cTValue *o = index2adr(lua, table);
	if (tvistab(o))
		return tabV(o)->end - tabV(o)->intn;
	return NULL;
}

LUA_API void lua_refstr(lua_State *lua, const void *str)
{
	setstrV(lua, lua->top, (GCstr*)str-1);
	incr_top(lua);
}
LUA_API void lua_reffunc(lua_State *lua, const void *func)
{
	setfuncV(lua, lua->top, (GCfunc*)func);
	incr_top(lua);
}
LUA_API void lua_reftable(lua_State *lua, const void *table)
{
	//settabV(lua, lua->top, ((GCtab**)table)[-1]); //fancy
	settabV(lua, lua->top, *(uint32_t*)((char*)table-4));
	incr_top(lua);
}
LUA_API void lua_refudata(lua_State *lua, const void *udata)
{
	setudataV(lua, lua->top, (GCudata*)udata-1);
	incr_top(lua);
}
LUA_API const void *lua_tohead(lua_State *lua, int index)
{
	cTValue *o = index2adr(lua, index);
	GCobj *gc = tvisnil(o) ? NULL : gcV(o);
	switch (gc ? ~gc->gch.gct : LJ_TNIL)
	{
	case LJ_TSTR: case LJ_TFUNC: case LJ_TTAB: case LJ_TUDATA:
		return gc;
	}
	return NULL;
}
LUA_API void lua_refhead(lua_State *lua, const void *gc)
{
	if (gc)
		setgcV(lua, lua->top, (GCobj*)gc, ~((GCobj*)gc)->gch.gct);
	else
		setnilV(lua->top);
	incr_top(lua);
}
LUA_API const void *lua_bodytohead(const void *body, int type, size_t *bodylen)
{
	const void *h;
	int n;
	switch (body ? type : LUA_TNIL)
	{
	case LUA_TSTRING: h = (GCstr*)body-1, n = ((GCstr*)h)->len; break;
	case LUA_TFUNCTION: h = body, n = 0; break;
	case LUA_TTABLE: h = (GCtab*)*(uint32_t*)((char*)body-4), n = ((GCtab*)h)->intn<<2; break;
	case LUA_TUSERDATA: h = (GCudata*)body-1, n = ((GCudata*)h)->len; break;
	default: h = NULL, n = 0;
	}
	if (bodylen) *bodylen = (size_t)n;
	return h;
}
LUA_API const void *lua_headtobody(const void *head, int *type, size_t *bodylen)
{
	GChead *o = (GChead*)head;
	const void *b;
	int t, n;
	switch (o ? ~(o->gct) : LJ_TNIL)
	{
	case LJ_TSTR: t = LUA_TSTRING, b = (GCstr*)o+1, n = ((GCstr*)o)->len; break;
	case LJ_TFUNC: t = LUA_TFUNCTION, b = o, n = 0; break;
	case LJ_TTAB: t = LUA_TTABLE, b = ((GCtab*)o)->end - ((GCtab*)o)->intn, n = ((GCtab*)o)->intn<<2; break;
	case LJ_TUDATA: t = LUA_TUSERDATA, b = (GCudata*)o+1, n = ((GCudata*)o)->len; break;
	default: t = LUA_TNONE, b = NULL, n = 0;
	}
	if (type) *type = t;
	if (bodylen) *bodylen = (size_t)n;
	return b;
}

LUA_API void lua_setweaking(lua_State *lua, int table,
	int (*func)(lua_State *l, const void *t, long long k, long long v, uint8_t closing))
{
	tabV(index2adr(lua, table))->weaking = func;
}

// +1 nil|read
LUA_API void lua_getmetaread(lua_State *lua, int table)
{
	GCtab *t = tabV(index2adr(lua, table));
	if (gcref(t->mread))
		settabV(lua, lua->top, tabref(t->mread));
	else
		setnilV(lua->top);
	incr_top(lua);
}
// -1 nil|read
LUA_API void lua_setmetaread(lua_State *lua, int table)
{
	GCtab *t = tabV(index2adr(lua, table));
	GCtab *m = tabV(lua->top - 1);
	setgcref(t->mread, obj2gco(m));
	lj_gc_objbarriert(lua, t, m);
	lua->top--;
}

LUA_API void *lua_createbody(lua_State *lua, int narr, int nrec, size_t len, unsigned refn)
{
	GCtab *t;
	lua_assert(len <= 65535 && (len&3)==0 && refn < len>>2);
	lj_gc_check(lua);
	t = lj_tab_newb(lua, (uint32_t)(narr > 0 ? narr+1 : 0), hsize2hbits(nrec), len>>2, refn);
	settabV(lua, lua->top, t);
	incr_top(lua);
	return t->end - t->intn;
}
LUA_API void *lua_marktable(lua_State *lua, int table)
{
	GCtab *t = tabV(index2adr(lua, table));
	lj_gc_anybarriert(lua, t);
	return t->end - t->intn;
}
LUA_API void *lua_markhead(lua_State *lua, const void *table)
{
	GCtab *t = (GCtab*)table;
	lj_gc_anybarriert(lua, t);
	return t->end - t->intn;
}
LUA_API void *lua_markbody(lua_State *lua, const void *table)
{
	//GCtab *t = ((GCtab**)table)[-1]; //fancy
	GCtab *t = (GCtab*)*(uint32_t*)((char*)table-4);
	lj_gc_anybarriert(lua, t);
	return t;
}

LUA_API size_t lua_userdatasize(const void *body)
{
	return (size_t)((GCudata*)body)[-1].size;
}

LUA_API int lua_userdatalen(const void *body, size_t len)
{
	GCudata *u = (GCudata*)body-1;
	if (len <= (size_t)u->size)
		return u->len = len, 1;
	return 0;
}

#define logkeygc(o, k, force) { if ((o) != NULL && (void*)(o) != (r)) { \
	TValue otv, rtv, *set; \
	setgcV(lua, &otv, obj2gco(o), ~obj2gco(o)->gch.gct); set = lj_tab_set(lua, ks, &otv); \
	if (force || tvisnil(set)) { \
		setgcV(lua, set, obj2gco(k), ~obj2gco(k)->gch.gct); \
		if (r != NULL) { \
			setgcV(lua, &rtv, r, ~r->gch.gct); \
			copyTV(lua, lj_tab_set(lua, rs, &otv), &rtv); \
	}}}}
#define logkeytv(o, k, force) { if ((o) != NULL && (void*)(o) != (r)) { \
	TValue otv, rtv, *set; \
	setgcV(lua, &otv, obj2gco(o), ~obj2gco(o)->gch.gct); set = lj_tab_set(lua, ks, &otv); \
	if (force || tvisnil(set)) { \
		tvislightud(k) ? setstrV(lua, set, udata) : copyTV(lua, set, (k)); \
		if (r != NULL) { \
			setgcV(lua, &rtv, r, ~r->gch.gct); \
			copyTV(lua, lj_tab_set(lua, rs, &otv), &rtv); \
	}}}}
LUA_API int lua_logrefs(lua_State *lua)
{
	GCtab *ks, *rs, *ks0, *rs0;
	GCstr *root, *stack, *proto, *cfunc, *udata, *meta, *mread, *env, *ext, *key;
	GCobj *R, *r;
	TValue ktv;
	int i, nt = 0, nu = 0, nf = 0, np = 0, nth = 0;

	ks0 = tvistab(index2adr(lua, 1)) ? tabV(index2adr(lua, 1)) : NULL;
	rs0 = tvistab(index2adr(lua, 2)) ? tabV(index2adr(lua, 2)) : NULL;

	ks = lj_tab_new(lua, 0, hsize2hbits(1024*1024)); settabV(lua, lua->top++, ks);
	rs = lj_tab_new(lua, 0, hsize2hbits(1024*1024)); settabV(lua, lua->top++, rs);
	root = lj_str_newlit(lua, "(ROOT)"); setstrV(lua, lua->top++, root);
	stack = lj_str_newlit(lua, "(STACK)"); setstrV(lua, lua->top++, stack);
	proto = lj_str_newlit(lua, "(PROTO)"); setstrV(lua, lua->top++, proto);
	cfunc = lj_str_newlit(lua, "(CFUNC)"); setstrV(lua, lua->top++, cfunc);
	udata = lj_str_newlit(lua, "(UDATA)"); setstrV(lua, lua->top++, udata);
	meta = lj_str_newlit(lua, "(META)"); setstrV(lua, lua->top++, meta);
	mread = lj_str_newlit(lua, "(MREAD)"); setstrV(lua, lua->top++, mread);
	env = lj_str_newlit(lua, "(ENV)"); setstrV(lua, lua->top++, env);
	ext = lj_str_newlit(lua, "(EXT)"); setstrV(lua, lua->top++, ext);
	key = lj_str_newlit(lua, "(KEY)"); setstrV(lua, lua->top++, key);
	lj_gc_fullgc(lua);
	lj_gc_anybarriert(lua, rs);	lj_gc_anybarriert(lua, ks);

	for (R = gcref(G(lua)->gc.root); (r = R); R = gcref(R->gch.nextgc))
		if (r->gch.gct == ~LJ_TTAB) {
			GCtab *t = gco2tab(r);
			cTValue *mode;
			int weak = 0;
			nt++;
			if (t == ks || t == rs || t == ks0 || t == rs0)
				continue;
			logkeygc(tabref(t->metatable), meta, 0)
			logkeygc(tabref(t->mread), mread, 0)
			for (i = -t->refn; i < 0; i++)
				logkeygc(gcref(t->end[i]), ext, 0)
			mode = lj_meta_fastg(G(lua), tabref(t->metatable), MM_mode);
			if (mode && tvisstr(mode)) {
				const char *modestr = strVdata(mode);
				int c;
				while ((c = *modestr++))
					if (c == 'k') weak |= LJ_GC_WEAKKEY;
					else if (c == 'v') weak |= LJ_GC_WEAKVAL;
			}
			for (i = 0; i < t->asize; i++)
				if (tvisgcv(arrayslot(t, i))) {
					setintV(&ktv, i);
					logkeytv(gcV(arrayslot(t, i)), &ktv, !(weak & LJ_GC_WEAKVAL))
				}
			if (t->hmask > 0) {
				Node *node = noderef(t->node);
				for (i = 0; i <= t->hmask; i++) {
					TValue *k = &node[i].key, *v = &node[i].val;
					if (tvisgcv(v))
						logkeytv(gcV(v), k, !tvislightud(k) && !(weak & LJ_GC_WEAKVAL))
					if ( !tvisnil(v) && tvisgcv(k))
						logkeygc(gcV(k), key, !(weak & LJ_GC_WEAKKEY))
				}
			}
		}
		else if (r->gch.gct == ~LJ_TUDATA) {
			nu++;
			r = obj2gco(udata);
			logkeygc(tabref(gco2ud(R)->metatable), meta, 0);
			logkeygc(tabref(gco2ud(R)->env), meta, 0);
		}
		else if (r->gch.gct == ~LJ_TFUNC) {
			GCfunc *fn = gco2func(r);
			nf++;
			if (isluafunc(fn)) {
				GCproto *pt = funcproto(fn);
				GCstr *k;
				logkeygc(tabref(fn->c.env), env, 0)
				logkeygc(pt, proto, 0)
				for (i = 0; i < fn->l.nupvalues; i++) {
					cTValue *v = uvval(&gcref(fn->l.uvptr[i])->uv);
					if (tvisgcv(v)) {
						k = lj_str_newz(lua, lj_debug_uvname(pt, i));
						logkeygc(gcV(v), k, 1)
					}
				}
			} else {
				r = obj2gco(cfunc);
				logkeygc(tabref(fn->c.env), env, 0)
				for (i = 0; i < fn->c.nupvalues; i++)
					if (tvisgcv(&fn->c.upvalue[i])) {
						setintV(&ktv, i);
						logkeytv(gcV(&fn->c.upvalue[i]), &ktv, 0)
					}
			}
		}
		else if (r->gch.gct == ~LJ_TPROTO) {
			np++;
			logkeygc(proto_chunkname(&r->pt), proto, 0)
			for (i = -(int)r->pt.sizekgc; i < 0; i++)
				logkeygc(proto_kgc(&r->pt, i), proto, 0)
		}
		else if (r->gch.gct == ~LJ_TTHREAD) {
			lua_State *th = gco2th(r);
			TValue *o, *frame, *top = th->top-1, *bot = tvref(th->stack);
			nth += th != mainthread(G(lua));
			for (o = bot+1; o <= top; o++)
				if (tvisgcv(o))
					logkeygc(gcV(o), stack, 0)
			for (frame = th->base-1; frame > bot; frame = frame_prev(frame)) {
				GCfunc *fn = frame_func(frame);
				TValue *ftop = frame;
				if (isluafunc(fn)) ftop += funcproto(fn)->framesize;
				if (ftop > top) top = ftop;
				logkeygc(fn, stack, 0)
			}
			logkeygc(tabref(th->env), env, 0)
		}

	r = NULL;
	logkeygc(tabref(mainthread(G(lua))->env), root, 1)
	logkeygc(gcV(&G(lua)->registrytv), root, 1)
	for (i = 0; i < GCROOT_MAX; i++)
		logkeygc(gcref(G(lua)->gcroot[i]), root, 1)
	lua->top -= 10;
//	printf("debug.logrefs: %d %d %d %d %d\n", nt, nu, nf, np, nth);
	return 2;
}
#undef logkeygc
#undef logkeytv

