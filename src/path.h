#pragma once
#include "stdint.h"
#define API extern "C"

API void* LoadPath(const char* resname);
API void FreePath(void* handle);
API const double* FindPath(void* handle, double startx, double starty, double endx, double endy, int32_t remote, int32_t* size);
API const double* FindCross(void* handle, double startx, double starty, double facex, double facey);
API int32_t CheckPath(void* handle, double startx, double starty, double endx, double endy);
API const double* FindNear(void* handle, double x, double y);

static int metapath;
int path_new(lua_State* L)
{
	const char* resname = tostr(L, -1);
	void* path = LoadPath(resname);
	void** udata = (void**)newudata(L, sizeof(void*));
	*udata = path;
	lua_getref(L, metapath);
	lua_setmetatable(L, -2);
	return 1;
}

int path_findpath(lua_State* L)
{
	void* handle = *((void**)toudata(L, 1));
	double startx = tonum(L, 2);
	double starty = tonum(L, 3);
	double endx = tonum(L, 4);
	double endy = tonum(L, 5);
	bool remote = tobool(L, 6);
	int32_t size;
	const double* data = FindPath(handle, startx, starty, endx, endy, remote ? 1 : 0, &size);
	if (data == 0) return 0;
	lua_newtable(L);
	for (int i = 0; i < size; i++)
		pushn(L, data[i]), rawseti(L, -2, i+1 );
	return 1;
}

int path_findcross(lua_State* L)
{
	void* handle = *((void**)toudata(L, 1));
	double startx = tonum(L, 2);
	double starty = tonum(L, 3);
	double facex = tonum(L, 4);
	double facey = tonum(L, 5);
	const double* data = FindCross(handle, startx, starty, facex, facey);
	if (data == 0) return 0;
	pushn(L, data[0]);
	pushn(L, data[1]);
	return 2;
}

int path_checkpath(lua_State* L)
{
	void* handle = *((void**)toudata(L, 1));
	double startx = tonum(L, 2);
	double starty = tonum(L, 3);
	double endx = tonum(L, 4);
	double endy = tonum(L, 5);
	int32_t r = CheckPath(handle, startx, starty, endx, endy);
	pushb(L, r == 1);
	return 1;
}

int path_findnear(lua_State* L)
{
	void* handle = *((void**)toudata(L, 1));
	double x = tonum(L, 2);
	double y = tonum(L, 3);
	const double* data = FindNear(handle, x, y);
	if (data == 0) return 0;
	pushn(L, data[0]);
	pushn(L, data[1]);
	return 2;
}

int path_free(lua_State* L)
{
	void* path = *((void**)toudata(L, 1));
	FreePath(path);
	return 0;
}

static int luaopen_path(lua_State* L)
{
	newtablen(L, 0, 1);
	pushs(L, "new"), pushc(L, path_new), lua_settable(L, -3);
	lua_setglobal(L, "_PathFinder");
	newtablen(L, 0, 4);
	pushs(L, "FindPath"), pushc(L, path_findpath), lua_settable(L, -3);
	pushs(L, "FindCross"), pushc(L, path_findcross), lua_settable(L, -3);
	pushs(L, "CheckPath"), pushc(L, path_checkpath), lua_settable(L, -3);
	pushs(L, "FindNear"), pushc(L, path_findnear), lua_settable(L, -3);
	pushs(L, "__gc"), pushc(L, path_free), lua_settable(L, -3);
	pushs(L, "__index"), lua_pushvalue(L, -2), lua_settable(L, -3);
	metapath = lua_ref(L, -1);
	return 0;
}