#include "hive.h"
#include "hive_socket.h"

#include "actor_log.h"
#include "hive_memory.h"
#include "hive_log.h"
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assert.h>

#include "lhive_buffer.h"


struct actor_state {
    lua_State* L;
    uint32_t handle;
};


#define HIVE_LUA_STATE  "__hive_state__"
#define HIVE_LUA_TRACEBACK "__hive_debug_traceback__"
#define HIVE_ACTOR_NAME "__hive_actor_name__"
#define HIVE_ACTOR_METHOD_DISPATCH "hive_dispatch"

static int hive_lib(lua_State* L);
static void _register_lib(lua_State* L);


static int
traceback(lua_State* L) {
    const char *msg = lua_tostring(L, 1);
    if (msg == NULL) {  /* is error object not a string? */
        if (luaL_callmeta(L, 1, "__tostring") &&    /* does it have a metamethod */
            lua_type(L, -1) == LUA_TSTRING) {/* that produces a string? */
            return 1;   /* that is the message */
        } else {
            msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
        }
    }
    luaL_traceback(L, L, msg, 1);   /* append a standard traceback */
    return 1;   /* return the traceback */
}


static void
reg_lua_lib(lua_State *L, lua_CFunction func, const char * libname) {
    luaL_requiref(L, libname, func, 0);
    lua_pop(L, 1);
}


static struct actor_state*
_self_state(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_STATE);
    struct actor_state* state = (struct actor_state*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return state;
}

static uint32_t
_check_handle(lua_State* L, int arg) {
    lua_Integer handle = luaL_checkinteger(L, arg);
    if(handle < 0 || handle > 0xffffffff) {
        luaL_error(L, "error actor handle id:%d", handle);
    }
    return (uint32_t)handle;
}


static void
_throw_error(lua_State* L, lua_State* NL, int ret) {
    const char* err = lua_tostring(NL, -1);
    size_t sz = strlen(err) + 64;
    char* err_buff = (char*)hive_malloc(sz);
    snprintf(err_buff, sz, "LUA_ERROR:[%d] %s\n", ret, err);
    if(!L) {
        hive_panic("bootstrap actor is error:%s", err_buff);
    }
    lua_pushstring(L, err_buff);
    hive_free(err_buff);
    lua_close(NL);
    lua_error(L);
}


static void
_lua_actor_dispatch(uint32_t source, uint32_t self, int type, int session, void* data, size_t sz, void* ud) {
    struct actor_state* state = (struct actor_state*)ud;
    lua_State* L = state->L;
    assert(state->handle == self);

    int n = 5;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_ACTOR_METHOD_DISPATCH);

    lua_pushinteger(L, source);
    lua_pushinteger(L, self);
    lua_pushinteger(L, type);
    lua_pushinteger(L, session);
    if (data == NULL || sz == 0) {
        lua_pushnil(L);
    }else {
        if(type == HIVE_TSOCKET) {
            struct socket_data* sdata = (struct socket_data*)data;
            enum socket_event se = sdata->se;
            lua_pushinteger(L, se);
            switch(se) {
                case SE_BREAK:
                    break;

                case SE_ACCEPT:{
                    int client_id = sdata->u.id;
                    lua_pushinteger(L, client_id);
                    n++;
                    break;
                }

                case SE_CONNECTED:
                    if(sdata->u.size == 0) {
                        lua_pushnil(L);
                    }else {
                        lua_pushlstring(L, (const char*)sdata->data, sdata->u.size);
                    }
                    n++;
                    break;
                    
                case SE_RECIVE:
                case SE_ERROR:
                    lua_pushlstring(L, (const char*)sdata->data, sdata->u.size);
                    n++;
                    break;

                default:
                    hive_panic("invalid socket event:%d", se);
            }
        }else {
            lua_pushlstring(L, (const char*)data, sz);
        }
    }

    int ret = lua_pcall(L, n, 0, top+1);
    if(ret != LUA_OK) {
        actor_log_send(self, HIVE_LOG_ERR, lua_tostring(L, -1));
    }
    lua_settop(L, top);

    if(type == HIVE_TRELEASE) {
        lua_close(L);
    }
}

static int
_lhive_start(lua_State* L) {
    luaL_checktype(L, -1, LUA_TFUNCTION);
    lua_setfield(L, LUA_REGISTRYINDEX, HIVE_ACTOR_METHOD_DISPATCH);
    return 0;
}

static int
_lhive_name(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_ACTOR_NAME);
    return 1;
}

static uint32_t
__hive_register(lua_State* L, const char* path, const char* name) {
    lua_State* NL = luaL_newstate();
    struct actor_state* state = (struct actor_state*)lua_newuserdata(NL, sizeof(struct actor_state));
    state->L = NL;
    state->handle = 0;
    lua_setfield(NL, LUA_REGISTRYINDEX, HIVE_LUA_STATE);
    lua_pushstring(NL, name);
    lua_setfield(NL, LUA_REGISTRYINDEX, HIVE_ACTOR_NAME);
    _register_lib(NL);

    int ret = luaL_loadfile(NL, path);
    if(ret != LUA_OK) {
        _throw_error(L, NL, ret);

    } else {
        lua_getfield(NL, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);
        lua_pushvalue(NL, -2);
        int ret = lua_pcall(NL, 0, 1, -2);
        if(ret != LUA_OK) {
            _throw_error(L, NL, ret);
        }
    }

    uint32_t handle = hive_register((char*)name, _lua_actor_dispatch, state);
    state->handle = handle;
    return handle;
}


static int
_lhive_register(lua_State* L) {
    const char* path = lua_tostring(L, 1);
    const char* name = lua_tostring(L, 2);
    if(!path) {
        return 0;
    }

    uint32_t handle = __hive_register(L, path, name);
    if(handle == 0) {
        return 0;
    }
    lua_pushinteger(L, handle);
    return 1;
}


static int
_lhive_exit(lua_State* L) {
    hive_exit();
    return 0;
}


static int
_lhive_unregister(lua_State* L) {
    uint32_t handle = _check_handle(L, 1);
    bool b = hive_unregister(handle);
    lua_pushboolean(L, b);
    return 1;
}


static int
_lhive_send(lua_State* L) {
    uint32_t target = _check_handle(L, 1);
    int session = luaL_optinteger(L, 2, 0);
    size_t sz = 0;
    const char* data = luaL_optlstring(L, 3, NULL, &sz);

    struct actor_state* state = _self_state(L);
    bool b = hive_send(state->handle, target,  HIVE_TNORMAL, session, (void*)data, sz);
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
_lhive_socket_connect(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    uint16_t port = (uint16_t)luaL_checkinteger(L, 2);
    struct actor_state* state = _self_state(L);
    const char* err_str = NULL;
    int id = hive_socket_connect(host, port, state->handle, &err_str);
    if (id<0) {
        lua_pushboolean(L, false);
        lua_pushstring(L, err_str);
        return 2;
    }else {
        lua_pushinteger(L, id);
        return 1;
    }
}


static int
_lhive_socket_addrinfo(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    struct socket_addrinfo addrinfo;
    const char* out_err = NULL;
    int err = hive_socket_addrinfo(id, &addrinfo, &out_err);
    if(err == 0) {
        lua_pushstring(L, addrinfo.ip);
        lua_pushinteger(L, addrinfo.port);
        return 2;
    }else if (err > 0) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, out_err);
        return 2;
    }else {
        lua_pushboolean(L, 0);
        lua_pushfstring(L, "addrinfo error[%d]", err);
        return 2;
    }
    return 0;
}


static int
_lhive_socket_listen(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    uint16_t port = (uint16_t)luaL_checkinteger(L, 2);
    struct actor_state* state = _self_state(L);
    int id = hive_socket_listen(host, port, state->handle);
    lua_pushinteger(L, id);
    return 1;
}


static int
_lhive_socket_attach(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    uint32_t actor_handle = _check_handle(L, 2);
    int ret = hive_socket_attach(id, actor_handle);
    lua_pushinteger(L, ret);
    return 1;
}


static int
_lhive_socket_send(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    size_t size;
    const char* s = luaL_checklstring(L, 2, &size);
    int ret = hive_socket_send(id, (const void*)s, size);
    lua_pushinteger(L, ret);
    return 1;
}


static int
_lhive_socket_close(lua_State* L) {
    int id = luaL_checkinteger(L, 1);
    int ret = hive_socket_close(id);
    lua_pushinteger(L, ret);
    return 1;
}

static int
_lhive_log(lua_State* L) {
    struct actor_state* state = _self_state(L);
    int level = luaL_checkinteger(L, 1);
    const char* s = luaL_checkstring(L, 2);
    actor_log_send(state->handle, level, s);
    return 0;
}


static int
hive_lib(lua_State* L) {
    luaL_Reg l[] = {
        {"hive_register", _lhive_register}, 
        {"hive_unregister", _lhive_unregister},
        {"hive_start", _lhive_start},
        {"hive_exit", _lhive_exit},
        {"hive_send", _lhive_send},
        {"hive_log", _lhive_log},
        {"hive_name", _lhive_name},

        {"hive_socket_connect", _lhive_socket_connect},
        {"hive_socket_listen", _lhive_socket_listen},
        {"hive_socket_addrinfo", _lhive_socket_addrinfo},
        {"hive_socket_attach", _lhive_socket_attach},
        {"hive_socket_send", _lhive_socket_send},
        {"hive_socket_close", _lhive_socket_close},
        {NULL, NULL},
    };
    luaL_newlib(L, l);
    _set_const(L, "HIVE_SYS_HANDLE", SYS_HANDLE);
    _set_const(L, "HIVE_TCREATE", HIVE_TCREATE);
    _set_const(L, "HIVE_TRELEASE", HIVE_TRELEASE);
    _set_const(L, "HIVE_TTIMER", HIVE_TTIMER);
    _set_const(L, "HIVE_TSOCKET", HIVE_TSOCKET);
    _set_const(L, "HIVE_TNORMAL", HIVE_TNORMAL);
    _set_const(L, "SE_CONNECTED", SE_CONNECTED);
    _set_const(L, "SE_BREAK", SE_BREAK);
    _set_const(L, "SE_ACCEPT", SE_ACCEPT);
    _set_const(L, "SE_RECIVE", SE_RECIVE);
    _set_const(L, "SE_ERROR", SE_ERROR);
    _set_const(L, "HIVE_LOG_DBG", HIVE_LOG_DBG);
    _set_const(L, "HIVE_LOG_INF", HIVE_LOG_INF);
    _set_const(L, "HIVE_LOG_ERR", HIVE_LOG_ERR);
    return 1;
}


static char inject_source[] = "package.path = './hive_lua/?.lua;' .. package.path";


static void
_register_lib(lua_State* L) {
    // register traceback
    lua_pushcfunction(L, traceback);
    lua_setfield(L, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);

    // open base lib
    luaL_openlibs(L);

    // inject source
    if(luaL_dostring(L, inject_source)) {
        hive_panic("inject source error: %s", lua_tostring(L, -1));
    }

    // register hive lib
    reg_lua_lib(L, hive_lib, "hive.c");

    // register base lib
    reg_lua_lib(L, lhive_luaopen_buffer, "buffer.c");
}


void
hive_bootstrap_init(const char* bootstrap_path) {
    if(bootstrap_path == NULL) {
        bootstrap_path = "examples/bootstrap.lua";
    }
    
    uint32_t handle = __hive_register(NULL, bootstrap_path, "bootstrap");
    if(handle == 0) {
        hive_panic("invalid bootstrap actor from `%s`", bootstrap_path);
    }
}
