#include "hive.h"
#include "hive_memory.h"
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assert.h>


struct actor_state {
    lua_State* L;
    char* bootstrap_path;
    uint32_t handle;
}ACOTR_BS;


#define HIVE_LUA_STATE  "__hive_state__"
#define HIVE_LUA_MODULE "__hive_module__"
#define HIVE_LUA_TRACEBACK "__hive_debug_traceback__"

#define HIVE_ACTOR_METHOD_DISPATCH "dispatch"

static int hive_lib(lua_State* L);

static void
reg_lua_lib(lua_State *L, lua_CFunction func, const char * libname) {
    luaL_requiref(L, libname, func, 0);
    lua_pop(L, 1);
}


static void
_throw_error(lua_State* L, lua_State* NL, int ret) {
    const char* err = lua_tostring(NL, -1);
    size_t sz = strlen(err) + 64;
    char* err_buff = (char*)hive_malloc(sz);
    snprintf(err_buff, sz, "hive register actor error:[%d] %s\n", ret, err);
    lua_pushstring(L, err_buff);
    hive_free(err_buff);
    lua_close(NL);
    lua_error(L);
}


static void
_lua_actor_dispatch(uint32_t source, uint32_t self, int type, int session, void* data, size_t sz, void* ud) {
    struct actor_state* state = (struct actor_state*)ud;
    lua_State* L = state->L;
    state->handle = self;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_MODULE);
    lua_getfield(L, -1, HIVE_ACTOR_METHOD_DISPATCH);

    lua_pushinteger(L, source);
    lua_pushinteger(L, type);
    lua_pushinteger(L, session);
    if (data == NULL || sz == 0) {
        lua_pushnil(L);
    }else {
        lua_pushlstring(L, (const char*)data, sz);
    }

    int ret = lua_pcall(L, 4, 0, top+1);
    if(ret != LUA_OK) {
        fprintf(stderr, "hive actor dispatch error:[%d] %s\n", ret, lua_tostring(L, -1));
    }
    lua_settop(L, top);

    if(type == HIVE_TRELEASE) {
        lua_close(L);
    }
}



static int
_lhive_register(lua_State* L) {
    const char* path = lua_tostring(L, 1);
    const char* name = lua_tostring(L, 2);
    if(!path) {
        return 0;
    }

    lua_State* NL = luaL_newstate();
    struct actor_state* state = (struct actor_state*)lua_newuserdata(NL, sizeof(struct actor_state));
    state->L = NL;
    state->handle = 0;

    // register actor state
    lua_setfield(NL, LUA_REGISTRYINDEX, HIVE_LUA_STATE); 

    // register traceback
    lua_getglobal(NL, "debug");
    lua_getfield(NL, -1, "traceback");
    lua_setfield(NL, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);

    luaL_openlibs(NL);
    hive_lib(NL);
    int ret = luaL_loadfile(NL, path);
    if(ret != LUA_OK) {
        _throw_error(L, NL, ret);

    } else {
        lua_getfield(NL, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);
        lua_pushvalue(NL, -2);
        int ret = lua_pcall(NL, 0, 1, -2);
        if(ret != LUA_OK) {
            _throw_error(L, NL, ret);
        }else {
            if(lua_type(NL, -1) != LUA_TTABLE) {
                lua_pushstring(L, "actor module should be return table");
                _throw_error(L, NL, 0);
            }
            lua_setfield(NL, LUA_REGISTRYINDEX, HIVE_LUA_MODULE);
        }
    }

    uint32_t handle = hive_register((char*)name, _lua_actor_dispatch, state);
    if(handle == 0) {
        lua_pushstring(L, "hive register error");
        _throw_error(L, NL, -1);
    } else {
        lua_pushinteger(L, handle);
    }
    return 1;
}


static int
_lhive_unregister(lua_State* L) {
    uint32_t handle = lua_tointeger(L, 1);
    bool b = hive_unregister(handle);
    lua_pushboolean(L, b);
    return 1;
}


static int
_lhive_send(lua_State* L) {
    uint32_t target = lua_tointeger(L, 1);
    int type = lua_tointeger(L, 2);
    int session = luaL_optinteger(L, 3, 0);
    size_t sz = 0;
    const char* data = luaL_optlstring(L, 4, NULL, &sz);

    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_STATE);
    struct actor_state* state = (struct actor_state*)lua_touserdata(L, -1);
    bool b = hive_send(state->handle, target, type, session, (void*)data, sz);
    lua_pushboolean(L, b);
    return 1;
}


static void
_set_const(lua_State* L, const char* fieldname, int v) {
    int table_idx = lua_gettop(L);
    lua_pushinteger(L, v);
    lua_setfield(L, table_idx, fieldname);
}


static int
hive_lib(lua_State* L) {
    luaL_Reg l[] = {
        {"hive_register", _lhive_register}, 
        {"hive_unregister", _lhive_unregister},
        {"hive_send", _lhive_send},
        {NULL, NULL},
    };
    luaL_newlib(L, l);
    _set_const(L, "HIVE_SYS_HANDLE", SYS_HANDLE);
    _set_const(L, "HIVE_TCREATE", SYS_HANDLE);
    _set_const(L, "HIVE_TRELEASE", HIVE_TRELEASE);
    _set_const(L, "HIVE_TTIMER", HIVE_TTIMER);
    _set_const(L, "HIVE_TSOCKET", HIVE_TSOCKET);
    return 1;
}



static void
_bootstrap_start(uint32_t self) {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    ACOTR_BS.L = L;
    ACOTR_BS.handle = self;
    hive_lib(L);

    // register traceback
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_setfield(L, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);

    char* path = ACOTR_BS.bootstrap_path;
    assert(path);
    int ret = luaL_loadfile(L, path);
    if(ret != LUA_OK) {
        goto BOOTSTRAP_ERROR;
    } else {
        lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);
        lua_pushvalue(L, -2);
        int ret = lua_pcall(L, 0, 1, -2);
        if(ret != LUA_OK) {
            goto BOOTSTRAP_ERROR;
        } else {
            if(lua_type(L, -1) != LUA_TTABLE) {
                lua_pushstring(L, "bootstrap actor module should be return table");
                goto BOOTSTRAP_ERROR;
            }
            lua_setfield(L, LUA_REGISTRYINDEX, HIVE_LUA_MODULE);
        }
    }

BOOTSTRAP_ERROR:
    fprintf(stderr, "hive start bootstrap error: [%d] %s\n", ret, lua_tostring(L, -1));
    hive_unregister(self);
}



static void
_bootstrap_exit(uint32_t self) {
    lua_close(ACOTR_BS.L);
    hive_free(ACOTR_BS.bootstrap_path);
}


void
acotr_bootstrap_setpath(const char* path) {
    assert(path);
    size_t sz = strlen(path);
    char* _path = (char*)hive_malloc(sz+1);
    strcpy(_path, path);
    ACOTR_BS.bootstrap_path = _path;
}


void
acotr_bootstrap_dispatch(uint32_t source, uint32_t self, int type, int session, void* data, size_t sz) {
    switch(type) {
        case HIVE_TCREATE:
            _bootstrap_start(self);
            break;
        case HIVE_TRELEASE:
            _bootstrap_exit(self);
            break;
        default:
            _lua_actor_dispatch(source, self, type, session, data, sz, &ACOTR_BS);
            break;
    }
}

