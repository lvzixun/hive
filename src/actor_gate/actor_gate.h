#ifndef _ACTOR_GATE_H_
#define _ACTOR_GATE_H_


#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

int lhive_luaopen_gate(lua_State* L);
int lhive_luaopen_gatemsg(lua_State* L);

#endif