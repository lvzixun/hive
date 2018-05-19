#include "hive.h"
#include "hive_socket.h"
#include "hive_memory.h"
#include "hive_log.h"

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include <assert.h>


#include "actor_gate/servergate.h"
#include "actor_gate/actor_gate.h"


struct actor_gate {
    struct servergate_context* context;
    uint32_t acotr_handle;
} _ENV_GATE;


static void
_gate_on_release() {
    servergate_free(_ENV_GATE.context);
}


static void
_gate_msg_cb(int id, uint8_t* data, size_t sz) {
}


static void
_gate_on_create() {
    _ENV_GATE.servergate_cb(_ENV_GATE.context, _gate_msg_cb);
}


static void
_gate_on_control(uint32_t source, int session, void* data, size_t sz) {
    assert(sz == sizeof(struct gate_msg));
    struct gate_msg* msg = (struct gate_msg*)data;
    struct gate_msg response;
    switch(msg->gct) {
        case GCT_BIND: {
            int ret = hive_socket_listen(msg->content.bind.ip, msg->content.bind.port, _ENV_GATE.acotr_handle);
            response.gct = GCT_BIND_RESPONSE;
            response.connect_response.bind_ret = ret;
            hive_send(_ENV_GATE.acotr_handle, source, HIVE_TNORMAL, session, (void*)&response, sizeof(response));
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

        } break;

        case HIVE_TSOCKET: {
            struct socket_data* sdata = (struct socket_data*)data;
            enum socket_event se = sdata->se;
            switch(se) {
                case SE_BREAK:
                    break;

                case SE_ACCEPT:
                    break;

                case SE_RECIVE:
                    break;

                case SE_ERROR:
                    break;

                default:
                    hive_panic("invalid socket event:%d", se);
            };
        } break;
    }
}


void 
actor_gate_init() {
    _ENV_GATE.context = servergate_create();
    _ENV_GATE.acotr_handle = hive_register("server_gate", _actor_gate_dispatch, NULL, NULL, 0);
    assert(_ENV_GATE.handle > 0);
}


int
actor_gate_bind(const char* ip, uint16_t port) {

}

