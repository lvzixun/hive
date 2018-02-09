#ifndef _SOCKET_MGR_H_
#define _SOCKET_MGR_H_

struct socket_mgr_state;

#include <unistd.h>
#include <stdint.h>

enum socket_event {
    SE_CONNECTED,
    SE_BREAK,
    SE_ACCEPT,
    SE_RECIVE,
    SE_ERROR,
};


struct socket_data {
    enum socket_event se;
    size_t size;
    uint8_t data[0];
};


struct socket_mgr_state* socket_mgr_create();
void socket_mgr_release(struct socket_mgr_state* state);
void socket_mgr_exit(struct socket_mgr_state* state);

int socket_mgr_connect(struct socket_mgr_state* state, const char* host, uint16_t port, char const** out_err, uint32_t actor_handle);
int socket_mgr_listen(struct socket_mgr_state* state, const char* host, uint16_t port, uint32_t actor_handle);
int socket_mgr_send(struct socket_mgr_state* state, int id, const void* data, size_t size);
int socket_mgr_update(struct socket_mgr_state* state);


#endif