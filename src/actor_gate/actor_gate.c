#include "hive.h"
#include "hive_socket.h"
#include "hive_memory.h"
#include "hive_log.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <netdb.h>


#include "actor_gate/servergate.h"
#include "actor_gate/actor_gate.h"


enum gate_control_type {
    // request enum
    GCT_BIND,
    GCT_CLOSE,

    // response enum
    GCT_PACKAGE_RESPONSE,
    GCT_BIND_RESPONSE,
    GCT_ACCEPT_RESPONSE,
    GCT_BREAK_RESPONSE,
    GCT_ERROR_RESPONSE,
};

struct bind_info {
    char ip[NI_MAXSERV];
    int port;
};


struct gate_msg {
    enum gate_control_type gct;
    union {
        uint8_t bind_data[0];

        struct {
            int id;
        } close;

        struct {
            int client_id;
            uint8_t* data;
            uint16_t sz;
        } package;

        union {
            int bind_ret;
            int accept_id;
            int break_id;
            struct {
                int   error_id;
                size_t size;
                char error_str[0];
            } error_info;
        } connect_response;
    }content;
};

struct actor_gate {
    struct servergate_context* context;
    uint32_t actor_handle;
    uint32_t opaque_handle; 
    int listen_id;
} _ENV_GATE;



#define check_gct(L, msg, _gct) do { \
    enum gate_control_type msg_gct = (msg)->gct; \
    if(msg_gct != (_gct)) { \
        luaL_error((L), "invalid gct type:%d, expect:%d", msg_gct, (_gct)); \
    } \
}while(0)


#define set_const(L, k, v) do { \
    lua_pushinteger(L, (v)); \
    lua_setfield(L, -2, (k)); \
}while(0)



static void
_gate_on_release() {
    int listen_id = _ENV_GATE.listen_id;
    if(listen_id >= 0) {
        hive_socket_close(listen_id);
    }
    servergate_free(_ENV_GATE.context);
}



static void
_gate_msg_cb(int id, uint8_t* data, size_t sz) {
    struct gate_msg msg;
    msg.gct = GCT_PACKAGE_RESPONSE;
    msg.content.package.data = data;
    msg.content.package.sz = sz;
    msg.content.package.client_id = id;
    hive_send(_ENV_GATE.actor_handle, _ENV_GATE.opaque_handle, HIVE_TNORMAL, 0, (void*)&msg, sizeof(msg));
}


static void
_gate_on_create() {
    servergate_cb(_ENV_GATE.context, _gate_msg_cb);
}


static void
_gate_on_control(uint32_t source, int session, void* data, size_t sz) {
    assert(sz >= sizeof(struct gate_msg));
    struct gate_msg* msg = (struct gate_msg*)data;
    struct gate_msg response;
    switch(msg->gct) {
        case GCT_BIND: {
            int ret = -100;  // is also bind
            struct bind_info* bind = (struct bind_info*)msg->content.bind_data;
            if(_ENV_GATE.opaque_handle == 0) {
                int listen_id = hive_socket_listen(bind->ip, bind->port, _ENV_GATE.actor_handle);
                if(listen_id >= 0) {
                    _ENV_GATE.opaque_handle = source;
                    _ENV_GATE.listen_id = listen_id;
                    ret = 0;
                } else {
                     ret = listen_id;
                }
            }
            response.gct = GCT_BIND_RESPONSE;
            response.content.connect_response.bind_ret = ret;
            hive_send(_ENV_GATE.actor_handle, source, HIVE_TNORMAL, 0, (void*)&response, sizeof(response));
        } break;

        case GCT_CLOSE: {
            hive_socket_close(msg->content.close.id);
        } break;

        default:
            assert(false);
    }
}


static void 
_actor_gate_dispatch(uint32_t source, uint32_t self, int type, int session, void* data, size_t sz, void* ud) {
    switch(type) {
        case HIVE_TRELEASE: {
            _gate_on_release();
        } break;

        case HIVE_TCREATE: {
            _gate_on_create();
        } break;

        case HIVE_TNORMAL: {
            _gate_on_control(source, session, data, sz);
        } break;

        case HIVE_TSOCKET: {
            int client_id = session;
            struct socket_data* sdata = (struct socket_data*)data;
            enum socket_event se = sdata->se;
            switch(se) {
                case SE_BREAK: {
                    struct gate_msg msg;
                    msg.gct = GCT_BREAK_RESPONSE;
                    msg.content.connect_response.break_id = client_id;
                    servergate_del(_ENV_GATE.context, client_id);
                    hive_send(_ENV_GATE.actor_handle, _ENV_GATE.opaque_handle, HIVE_TNORMAL, 0, &msg, sizeof(msg));
                } break;

                case SE_ACCEPT: {
                    struct gate_msg msg;
                    msg.gct = GCT_ACCEPT_RESPONSE;
                    client_id = sdata->u.id;
                    msg.content.connect_response.accept_id = client_id;
                    hive_socket_attach(client_id, _ENV_GATE.actor_handle);
                    hive_send(_ENV_GATE.actor_handle, _ENV_GATE.opaque_handle, HIVE_TNORMAL, 0, &msg, sizeof(msg));
                } break;

                case SE_RECIVE: {
                    servergate_add(_ENV_GATE.context, client_id, sdata->data, sdata->u.size);
                } break;

                case SE_ERROR: {
                    uint8_t* error_str = sdata->data;
                    size_t size = sdata->u.size;
                    uint8_t msg_buffer[sizeof(struct gate_msg) + size];
                    struct gate_msg* msg = (struct gate_msg*)msg_buffer;
                    msg->gct = GCT_ERROR_RESPONSE;
                    msg->content.connect_response.error_info.error_id = client_id;
                    msg->content.connect_response.error_info.size = size;
                    memcpy(msg->content.connect_response.error_info.error_str, error_str, size);
                    servergate_del(_ENV_GATE.context, client_id);
                    hive_send(_ENV_GATE.actor_handle, _ENV_GATE.opaque_handle, HIVE_TNORMAL, 0, msg_buffer, sizeof(msg_buffer));
                } break;

                default:
                    hive_panic("invalid socket event:%d", se);
            };
        } break;
    }
}


static void 
actor_gate_init() {
    if(_ENV_GATE.context) {
        return;
    }
    _ENV_GATE.context = servergate_create();
    _ENV_GATE.actor_handle = hive_register("server_gate", _actor_gate_dispatch, NULL, NULL, 0);
    _ENV_GATE.opaque_handle = 0;
    _ENV_GATE.listen_id = -1;
}


static int
actor_gate_bind(uint32_t source, const char* ip, size_t sz, uint16_t port) {
    if(_ENV_GATE.listen_id >= 0) {
        return -102;
    }

    struct bind_info info;
    if(sz >= sizeof(info.ip)) {
        return -101;
    }

    memset(&info, 0, sizeof(info));
    uint8_t buffer[sizeof(struct gate_msg) + sizeof(struct bind_info)] = {0};
    struct gate_msg* msg = (struct gate_msg*)buffer;
    msg->gct = GCT_BIND;
    info.port = port;
    memcpy(info.ip, ip, sz);
    memcpy(msg->content.bind_data, (uint8_t*)&info, sizeof(info));
    hive_send(source, _ENV_GATE.actor_handle, HIVE_TNORMAL, 0, buffer, sizeof(buffer));
    return 0;
}


static void
actor_gate_close(uint32_t source, int client_id) {
    struct gate_msg msg = {0};
    msg.content.close.id = client_id;
    msg.gct = GCT_CLOSE;
    hive_send(source, _ENV_GATE.actor_handle, HIVE_TNORMAL, 0, &msg, sizeof(msg));
}


// lua bind api
static int
_lgate_start(lua_State* L) {
    uint32_t source = (uint32_t)lua_tointeger(L, 1);
    size_t sz = 0;
    const char* ip = lua_tolstring(L, 2, &sz);
    uint16_t port = (uint16_t)lua_tointeger(L, 3);
    int ret = actor_gate_bind(source, ip, sz, port);
    lua_pushinteger(L, ret);
    return 1;
}


static int
_lgate_close(lua_State* L) {
    uint32_t source = (uint32_t)lua_tointeger(L, 1);
    int client_id = lua_tointeger(L, 2);
    actor_gate_close(source, client_id);
    return 0;
}


static int
_lmsg_type(lua_State* L) {
    const struct gate_msg* msg = (const struct gate_msg*)lua_tostring(L, 1);
    lua_pushinteger(L, msg->gct);
    return 1;
}


static int
_lmsg_send(lua_State* L) {
    int client_id = lua_tointeger(L, 1);
    uint8_t head[2] = {0};
    size_t sz = 0;
    const uint8_t* s = (const uint8_t*)lua_tolstring(L, 2, &sz);
    if(sz > 0xffff) {
        luaL_error(L, "msg package is to big:%lu", sz);
    }
    head[1] = sz & 0xff;
    head[0] = (sz >> 8) & 0xff;

    hive_socket_send(client_id, head, 2);
    hive_socket_send(client_id, s, sz);
    return 0;
}


static int
_lmsg_package(lua_State* L) {
    const struct gate_msg* msg = (const struct gate_msg*)lua_tostring(L, 1);
    check_gct(L, msg, GCT_PACKAGE_RESPONSE);
    uint8_t* data = msg->content.package.data;
    lua_pushinteger(L, msg->content.package.client_id);
    lua_pushlstring(L, (const char*)data, msg->content.package.sz);
    hive_free(data);
    return 2;
}


static int
_lmsg_bindret(lua_State* L) {
    const struct gate_msg* msg = (const struct gate_msg*)lua_tostring(L, 1);
    check_gct(L, msg, GCT_BIND_RESPONSE);
    lua_pushinteger(L, msg->content.connect_response.bind_ret);
    return 1;
}


static int
_lmsg_accept(lua_State* L) {
    const struct gate_msg* msg = (const struct gate_msg*)lua_tostring(L, 1);
    check_gct(L, msg, GCT_ACCEPT_RESPONSE);
    lua_pushinteger(L, msg->content.connect_response.accept_id);
    return 1;
}


static int
_lmsg_break(lua_State* L) {
    const struct gate_msg* msg = (const struct gate_msg*)lua_tostring(L, 1);
    check_gct(L, msg, GCT_BREAK_RESPONSE);
    lua_pushinteger(L, msg->content.connect_response.break_id);
    return 1;
}


static int
_lmsg_close(lua_State* L) {
    const struct gate_msg* msg = (const struct gate_msg*)lua_tostring(L, 1);
    check_gct(L, msg, GCT_CLOSE);
    lua_pushinteger(L, msg->content.close.id);
    return 1;   
}


static int
_lmsg_error(lua_State* L) {
    const struct gate_msg* msg = (const struct gate_msg*)lua_tostring(L, 1);
    check_gct(L, msg, GCT_ERROR_RESPONSE);
    lua_pushinteger(L, msg->content.connect_response.error_info.error_id);
    lua_pushlstring(L, msg->content.connect_response.error_info.error_str, msg->content.connect_response.error_info.size);
    return 2;
}


int
lhive_luaopen_gatemsg(lua_State* L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        {"msg_type", _lmsg_type},
        {"msg_send", _lmsg_send},
        {"msg_package", _lmsg_package},
        {"msg_bindret", _lmsg_bindret},
        {"msg_accept", _lmsg_accept},
        {"msg_break", _lmsg_break},
        {"msg_close", _lmsg_close},
        {"msg_error", _lmsg_error},
        {NULL, NULL},
    };

    luaL_newlib(L, l);

    set_const(L, "MT_PACKAGE", GCT_PACKAGE_RESPONSE);
    set_const(L, "MT_BINDRET", GCT_BIND_RESPONSE);
    set_const(L, "MT_ACCEPT", GCT_ACCEPT_RESPONSE);
    set_const(L, "MT_BREAK", GCT_BREAK_RESPONSE);
    set_const(L, "MT_ERROR", GCT_ERROR_RESPONSE);
    return 1;
}


int
lhive_luaopen_gate(lua_State* L) {
    luaL_checkversion(L);
    luaL_Reg l[] = {
        // control api
        {"start", _lgate_start},
        {"close", _lgate_close},
        {NULL, NULL},
    };

    actor_gate_init();
    luaL_newlib(L, l);

    set_const(L, "GATE_HANDLE", _ENV_GATE.actor_handle);
    return 1;
}


