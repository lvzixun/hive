#ifndef _ACTOR_GATE_H_
#define _ACTOR_GATE_H_

#include <unistd.h>
#include <stdint.h>
#include <netdb.h>


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


struct gate_msg {
    enum gate_control_type gct;
    union {
        struct {
            char ip[NI_MAXHOST];
            int port;
        } bind;

        struct {
            int id;
        } close;

        struct {
            uint8_t* data;
            uint16_t sz;
        } package;

        struct {
            int bind_ret;
            int accept_id
        } connect_response;
    }content;
};



void actor_gate_init();

#endif