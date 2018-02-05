#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "hive.h"
#include "socket_poll.h"
#include "hive_memory.h"

#define MAX_SOCKETS_SLOT (1<<16)
#define MAX_SP_EVENT 64


enum socket_type {
    ST_INVALID,
    ST_LISTEN,
    ST_CONNECT,
    ST_FORWARD,
};


struct buffer_block {
    struct buffer_block* next;
    size_t sz;
    uint8_t buffer[0];
};

struct socket {
    int fd;
    enum socket_type type;
    uint32_t actor_handle;
    struct buffer_block* write_buffer;
};


struct socket_mgr_state {
    poll_fd pfd;
    int recvctrl_fd;
    int sendctrl_fd;
    int socket_index;
    struct socket socket_slots[MAX_SOCKETS_SLOT];
    struct event  sp_event[MAX_SP_EVENT];
};



static void _socket_free(struct socket* s);

struct socket_mgr_state*
socket_mgr_create() {
    int i;
    struct socket_mgr_state* state = hive_malloc(sizeof(struct socket_mgr_state));
    state->pfd = sp_create();
    state->socket_index = 0;
    for(i=0; i<MAX_SOCKETS_SLOT; i++) {
        struct socket* p = &(state->socket_slots[i]);
        p->write_buffer = NULL;
        p->type = ST_INVALID;
        p->actor_handle = SYS_HANDLE;
    }
    return state;
}


void 
socket_mgr_release(struct socket_mgr_state* state) {
    int i;
    // free socket
    for(i=0; i<MAX_SOCKETS_SLOT; i++) {
        struct socket* p = &(state->socket_slots[i]);
        _socket_free(p);
    }

    // free socket poll
    sp_release(state->pfd);

    // fress state
    hive_free(state);
}


static void
_socket_free(struct socket* s) {

}


static int
_socket_listen(struct socket_mgr_state* state, const char* host, uint16_t port) {
    int fd = -1;
    int reuse = 1;
    struct addrinfo ai_hints = {0};
    struct addrinfo* ai_list = NULL;
    char portstr[16];
    sprintf(portstr, "%d", port);
    ai_hints.ai_family = IPPROTO_TCP;
    ai_hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(host, portstr, &ai_hints, &ai_list);
    if(status != 0) {
        return -1;
    }

    fd = socket(ai_list->ai_family, ai_list->ai_socktype, 0);
    if(fd<0) {
        goto LISTEN_ERROR;
    }

    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&reuse, sizeof(reuse));
    if(ret == -1) {
        goto LISTEN_ERROR;
    }

    status = bind(fd, (struct sockaddr *)ai_list->ai_addr, ai_list->ai_addrlen);
    if(status != 0) {
        goto LISTEN_ERROR;
    }

    freeaddrinfo(ai_list);
    return fd;

LISTEN_ERROR:
    freeaddrinfo(ai_list);
    if(fd >= 0) {
        close(fd);
    }
    return -2;
}



static void
_socket_connect(struct socket_mgr_state* state, const char* host, uint16_t port) {
}

