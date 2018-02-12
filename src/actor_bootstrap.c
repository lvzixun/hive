#include "hive.h"
#include "hive_socket.h"

#include "hive_memory.h"
#include "hive_log.h"
#include <string.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assert.h>


struct actor_state {
    lua_State* L;
    char* bootstrap_path;
    uint32_t handle;
}ACTOR_BS;


#define HIVE_LUA_STATE  "__hive_state__"
#define HIVE_LUA_MODULE "__hive_module__"
#define HIVE_LUA_TRACEBACK "__hive_debug_traceback__"

#define HIVE_ACTOR_METHOD_DISPATCH "dispatch"
#define bs_log(...) hive_elog("hive actor_bootstrap", __VA_ARGS__)

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
    assert(state->handle == self);

    int n = 5;
    int top = lua_gettop(L);
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);
    lua_getfield(L, LUA_REGISTRYINDEX, HIVE_LUA_MODULE);
    lua_getfield(L, -1, HIVE_ACTOR_METHOD_DISPATCH);

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
        bs_log("hive actor dispatch error:[%d] %s\n", ret, lua_tostring(L, -1));
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
    lua_setfield(NL, LUA_REGISTRYINDEX, HIVE_LUA_STATE);
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
        state->handle = handle;
        lua_pushinteger(L, handle);
    }
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
    int id = hive_socket_connect(host, port, state->handle);
    lua_pushinteger(L, id);
    return 1;
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
hive_lib(lua_State* L) {
    luaL_Reg l[] = {
        {"hive_register", _lhive_register}, 
        {"hive_unregister", _lhive_unregister},
        {"hive_exit", _lhive_exit},
        {"hive_send", _lhive_send},

        {"hive_socket_connect", _lhive_socket_connect},
        {"hive_socket_listen", _lhive_socket_listen},
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
    _set_const(L, "SE_BREAK", SE_BREAK);
    _set_const(L, "SE_ACCEPT", SE_ACCEPT);
    _set_const(L, "SE_RECIVE", SE_RECIVE);
    _set_const(L, "SE_ERROR", SE_ERROR);
    return 1;
}

static void
_register_lib(lua_State* L) {
    // register traceback
    lua_pushcfunction(L, traceback);
    lua_setfield(L, LUA_REGISTRYINDEX, HIVE_LUA_TRACEBACK);

    // open base lib
    luaL_openlibs(L);

    // register hive lib
    reg_lua_lib(L, hive_lib, "hive");

    // register socket lib
}


static void
_bootstrap_start(uint32_t self) {
    lua_State* L = luaL_newstate();
    ACTOR_BS.L = L;
    ACTOR_BS.handle = self;
    lua_pushlightuserdata(L, (void*)&ACTOR_BS);
    lua_setfield(L, LUA_REGISTRYINDEX, HIVE_LUA_STATE);
    _register_lib(L);

    char* path = ACTOR_BS.bootstrap_path;
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
    return;

BOOTSTRAP_ERROR:
    bs_log("hive start bootstrap error: [%d] %s\n", ret, lua_tostring(L, -1));
    hive_unregister(self);
}



static void
_bootstrap_exit(uint32_t self) {
    lua_close(ACTOR_BS.L);
    hive_free(ACTOR_BS.bootstrap_path);
}


static void
_bootstrap_setpath(const char* path) {
    assert(path);
    size_t sz = strlen(path);
    char* _path = (char*)hive_malloc(sz+1);
    strcpy(_path, path);
    ACTOR_BS.bootstrap_path = _path;
}


static void
_bootstrap_dispatch(uint32_t source, uint32_t self, int type, int session, void* data, size_t sz, void* ud) {
    switch(type) {
        case HIVE_TCREATE:
            _bootstrap_start(self);
            break;
        case HIVE_TRELEASE:
            _bootstrap_exit(self);
            break;
        default:
            _lua_actor_dispatch(source, self, type, session, data, sz, &ACTOR_BS);
            break;
    }
}

void
actor_bootstrap_init() {
    _bootstrap_setpath("hive_lua/bootstrap.lua");
    hive_register("bootstrap", _bootstrap_dispatch, NULL);
}
