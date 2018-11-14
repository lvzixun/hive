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
#include "atomic.h"
#include "spinlock.h"
#include "socket_mgr.h"
#include "hive_log.h"

#define MAX_SOCKETS_SLOT (1<<16)
#define MAX_SP_EVENT 64
#define MAX_RECV_BUFFER 64*1024*1024

enum socket_type {
    ST_INVALID,
    ST_PREPARE,

    ST_LISTEN,
    ST_CONNECTING,
    ST_CONNECTED,
    ST_FORWARD,

    _ST_COUNT,
};

#define SOCKET_OK    0
#define SOCKET_CLOSE 1
#define SOCKET_BREAK 2
#define SOCKET_ERROR 3

struct buffer_block {
    struct buffer_block* next;
    size_t sz;
    size_t offset;
    uint8_t buffer[0];
};

struct socket {
    int fd;
    int id;
    enum socket_type type;
    uint32_t actor_handle;
    struct spinlock lock;
    struct {
        struct buffer_block* head;
        struct buffer_block* tail;
    }write_buffer;
};


struct socket_mgr_state {
    poll_fd pfd;
    int recvctrl_fd;
    int sendctrl_fd;
    int socket_index;
    struct socket socket_slots[MAX_SOCKETS_SLOT];
    struct event  sp_event[MAX_SP_EVENT];

    char _addr_buffer[2048];
    struct socket_data* _recv_data;

    struct {
        struct socket** slots;
        size_t size;
        size_t idx;
    } prepare_close_sockets;
};



enum request_type {
    REQ_LISTEN,
    REQ_CONNECT,
    REQ_CLOSE,
    REQ_ATTACH,
    REQ_SEND,

    REQ_EXIT,
};

struct request_msgsend {
    struct buffer_block* block;
};

struct request_package {
    enum request_type type;
    int socket_id;
    union {
        struct request_msgsend msgsend;
        uint32_t attach_handle;
    } v;
};

#define sm_log(...) hive_elog("hive socket_mgr", __VA_ARGS__)

#define id2hash(i) ((i)%MAX_SOCKETS_SLOT)
#define get_socket(id) &(state->socket_slots[id2hash(id)])
#define PKG_SIZE sizeof(struct request_package)
#define write_buffer_empty(s) ((s)->write_buffer.tail==NULL)

static void _socket_free(struct socket* s);
static void _buffer_free(struct socket* s);
static const char* _socket_check_error(struct socket* s);

static int _socket_getaddr(struct socket_mgr_state* state, struct socket* s, struct socket_addrinfo* out_addrinfo, const char** out_error);

static void _actor_notify_accept(int server_id, int client_id, uint32_t target_handle);
static void _actor_notify_break(struct socket* s);
static void _actor_notify_error(struct socket_mgr_state* state, struct socket* s, size_t size);
static void _actor_notify_recv(struct socket_mgr_state* state, struct socket* s, size_t size);
static void _actor_notify_connected(struct socket_mgr_state* state, struct socket* s, const char* err);

struct socket_mgr_state*
socket_mgr_create() {
    int i;
    struct socket_mgr_state* state = hive_malloc(sizeof(struct socket_mgr_state));
    state->pfd = sp_create();
    if(sp_invalid(state->pfd)) {
        hive_free(state);
        return NULL;
    }

    // init prepare close sockets queue
    state->prepare_close_sockets.size = MAX_SP_EVENT;
    state->prepare_close_sockets.idx = 0;
    state->prepare_close_sockets.slots = (struct socket**)hive_malloc(sizeof(struct socket*)*state->prepare_close_sockets.size);

    state->_recv_data = (struct socket_data*)hive_malloc(sizeof(struct socket_data) + MAX_RECV_BUFFER);
    state->_recv_data->se = SE_RECIVE;
    state->_recv_data->u.size = 0;

    assert(PKG_SIZE <= 256);

    state->socket_index = 0;
    for(i=0; i<MAX_SOCKETS_SLOT; i++) {
        struct socket* p = &(state->socket_slots[i]);
        p->write_buffer.tail = NULL;
        p->write_buffer.head = NULL;
        p->type = ST_INVALID;
        p->actor_handle = SYS_HANDLE;
        spinlock_init(&p->lock);
    }

    int fd[2];
    if(pipe(fd)) {
        sm_log("open pipe fd is error.\n");
        hive_free(state);
        return NULL;
    }

    if(sp_add(state->pfd, fd[0], NULL)) {
        sm_log("add pipe event poll is error.\n");
        close(fd[0]);
        close(fd[1]);
        hive_free(state);
        return NULL;
    }

    sp_nonblocking(fd[0]);
    state->recvctrl_fd = fd[0];
    state->sendctrl_fd = fd[1];
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

    hive_free(state->_recv_data);

    close(state->sendctrl_fd);
    close(state->recvctrl_fd);

    // free prepare close socket
    hive_free(state->prepare_close_sockets.slots);

    // free socket poll
    sp_release(state->pfd);

    // free state
    hive_free(state);
}


static void
_socket_free(struct socket* s) {
    if(s->type != ST_INVALID) {
        int fd = s->fd;
        int ret = close(fd);
        assert(ret == 0);
        _buffer_free(s);
    }
}

static struct socket*
_socket_gen(struct socket_mgr_state* state) {
    int i;
    for(i=0; i<MAX_SOCKETS_SLOT; i++) {
        int id = ATOM_FINC(&state->socket_index);
        if(id<0) {
            id = ATOM_AND(&(state->socket_index), 0x7fffffff);
        }
        struct socket* s = &state->socket_slots[id2hash(id)];
        if(s->type == ST_INVALID) {
            if(ATOM_CAS(&(s->type), ST_INVALID, ST_PREPARE)) {
                s->id = id;
                s->fd = -1;
                return s;
            }else {
                --i;
            }
        }
    }
    return NULL; 
}

static void
_socket_attach(struct socket_mgr_state* state, struct socket* s, uint32_t actor_handle) {
    enum socket_type st = s->type;
    int fd = s->fd;
    if(st != ST_PREPARE) {
        return;
    }
    s->actor_handle = actor_handle;
    s->type = ST_FORWARD;
    sp_add(state->pfd, fd, s);
}

static void
_socket_remove(struct socket_mgr_state* state, struct socket* s) {
    enum socket_type st = s->type;
    int fd = s->fd;
    if(st == ST_INVALID) {
        return;
    }

    spinlock_lock(&s->lock);
    if(st != ST_PREPARE) {
        sp_del(state->pfd, fd);
    }

    // printf("socket remove s:%p st:%d id:%d fd:%d\n", s, st, s->id, s->fd);
    int ret = close(fd);
    assert(ret == 0);
    _buffer_free(s);
    s->id = -1;
    s->actor_handle = SYS_HANDLE;
    s->fd = -1;
    s->type = ST_INVALID;
    spinlock_unlock(&s->lock);
}

static inline void
_insert_close_socket(struct socket_mgr_state* state, struct socket* s) {
    size_t size = state->prepare_close_sockets.size;
    size_t idx = state->prepare_close_sockets.idx;
    struct socket** slots = state->prepare_close_sockets.slots;
    if(idx >= size) {
        size *= 2;
        slots = hive_realloc(slots, size);
        state->prepare_close_sockets.slots = slots;
        state->prepare_close_sockets.size = size;
    }
    slots[state->prepare_close_sockets.idx++] = s;
}

static inline void
_clear_close_socket(struct socket_mgr_state* state) {
    state->prepare_close_sockets.idx = 0;
}

static inline struct buffer_block*
_buffer_new_block(const void* data, size_t size) {
    struct buffer_block* block = (struct buffer_block*)hive_malloc(sizeof(struct buffer_block) + size);
    block->next = NULL;
    block->sz = size;
    block->offset = 0;
    memcpy(block->buffer, data, size);
    return block;
}


static inline void
_buffer_append(struct socket* s, struct buffer_block* block) {
    if(s->write_buffer.tail == NULL) {
        s->write_buffer.tail = block;
    }else {
        s->write_buffer.tail->next = block;
    }

    if(s->write_buffer.head == NULL) {
        s->write_buffer.head = block;
    }
}

static void
_buffer_free(struct socket* s) {
    struct buffer_block* p = s->write_buffer.head;
    while(p) {
        struct buffer_block* next = p->next;
        hive_free(p);
        p = next;
    }
    s->write_buffer.tail = NULL;
    s->write_buffer.head = NULL;
}



static int
_socket_listen(struct socket_mgr_state* state, const char* host, uint16_t port) {
    int fd = -1;
    int reuse = 1;
    int ret = -2;
    struct addrinfo ai_hints = {0};
    struct addrinfo* ai_list = NULL;
    char portstr[16];
    sprintf(portstr, "%d", port);
    ai_hints.ai_protocol = IPPROTO_TCP;
    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_socktype = SOCK_STREAM;

    int status = getaddrinfo(host, portstr, &ai_hints, &ai_list);
    if(status != 0) {
        return -1;
    }

    fd = socket(ai_list->ai_family, ai_list->ai_socktype, 0);
    if(fd<0) {
        ret = -2;
        goto LISTEN_ERROR;
    }

    if(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&reuse, sizeof(reuse)) == -1) {
        ret = -3;
        goto LISTEN_ERROR;
    }

    status = bind(fd, (struct sockaddr *)ai_list->ai_addr, ai_list->ai_addrlen);
    if(status != 0) {
        ret = -4;
        goto LISTEN_ERROR;
    }

    struct socket* s = _socket_gen(state);
    if(!s) {
        ret = -5;
        goto LISTEN_ERROR;    
    }

    sp_nonblocking(fd);
    if(listen(fd, 128) == -1) {
        ret = -6;
        goto LISTEN_ERROR;
    }

    s->type = ST_LISTEN;
    // printf("set_socket s:%p id:%d old_fd:%d new_fd:%d\n", s, s->id, s->fd, fd);
    s->fd = fd;
    freeaddrinfo(ai_list);
    return s->id;

LISTEN_ERROR:
    freeaddrinfo(ai_list);
    if(fd >= 0) {
        close(fd);
    }
    return ret;
}


static void
_request_send(struct socket_mgr_state* state, struct request_package* msg) {
    for(;;) {
        ssize_t n = write(state->sendctrl_fd, msg, PKG_SIZE);
        if(n < 0) {
            if (errno != EINTR) {
                sm_log("[hive] request send error: %s.\n", strerror(errno));
                break;
            }
            continue;
        }
        assert(n == PKG_SIZE);
        return;
    }
}


static void
_request_listen(struct socket_mgr_state* state, int id) {
    struct request_package msg;
    msg.type = REQ_LISTEN;
    msg.socket_id = id;
    _request_send(state, &msg);
}


static void
_request_connect(struct socket_mgr_state* state, int id) {
    struct request_package msg;
    msg.type = REQ_CONNECT;
    msg.socket_id = id;
    _request_send(state, &msg);
}

static void
_request_close(struct socket_mgr_state* state, int id) {
    struct request_package msg;
    msg.type = REQ_CLOSE;
    msg.socket_id = id;
    _request_send(state, &msg);
}

static void
_request_attach(struct socket_mgr_state* state, int id, uint32_t actor_handle) {
    struct request_package msg;
    msg.type = REQ_ATTACH;
    msg.socket_id = id;
    msg.v.attach_handle = actor_handle;
    _request_send(state, &msg);
}

static void
_request_msgsend(struct socket_mgr_state* state, int id, struct buffer_block* block) {
    struct request_package msg;
    msg.type = REQ_SEND;
    msg.socket_id = id;
    msg.v.msgsend.block = block;
    _request_send(state, &msg);
}


int
socket_mgr_listen(struct socket_mgr_state* state, const char* host, uint16_t port, uint32_t actor_handle) {
    int id = _socket_listen(state, host, port);
    if(id >= 0) {
        struct socket* s = get_socket(id);
        assert(s->type == ST_LISTEN);
        s->actor_handle = actor_handle;
        sp_nonblocking(s->fd);
        _request_listen(state, s->id);
    }
    return id;
}


static int
_socket_connect(struct socket_mgr_state* state, const char* host, uint16_t port, char const** out_err) {
    struct addrinfo ai_hints = {0};
    struct addrinfo* ai_list = NULL;
    char portstr[16];
    sprintf(portstr, "%d", port);
    ai_hints.ai_protocol = IPPROTO_TCP;
    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_socktype = SOCK_STREAM;

    *out_err = NULL;
    int status = getaddrinfo(host, portstr, &ai_hints, &ai_list);
    if(status != 0) {
        *out_err = gai_strerror(status);
        return -1;
    }

    int fd = -1;
    struct addrinfo *ai_ptr = NULL;
    for(ai_ptr=ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
        fd = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
        if(fd < 0) {
            continue;
        }
        sp_nonblocking(fd);
        status = connect(fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
        if(status != 0 && errno != EINPROGRESS) {
            close(fd);
            fd = -1;
        } else {
            break;
        }
    }

    int ret = 0;
    if(fd < 0) {
        *out_err = strerror(errno);
        ret = -2;
        goto CONNECT_ERROR;
    }

    struct socket* s = _socket_gen(state);
    if(!s) {
        ret = -3;
        goto CONNECT_ERROR;
    }

    s->fd = fd;
    s->type = (status==0)?(ST_CONNECTED):(ST_CONNECTING);
    freeaddrinfo(ai_list);
    return s->id;

CONNECT_ERROR:
    if(fd>0) {
        close(fd);
    }

    if(ai_list) {
        freeaddrinfo(ai_list);
    }
    return ret;
}


int
socket_mgr_connect(struct socket_mgr_state* state, const char* host, uint16_t port, char const** out_err, uint32_t actor_handle) {
    int id = _socket_connect(state, host, port, out_err);
    if(id >= 0 ) {
        struct socket* s = get_socket(id);
        assert(s->type == ST_CONNECTING || s->type == ST_CONNECTED);
        s->actor_handle = actor_handle;
        _request_connect(state, id);
    }
    return id;
}



int
socket_mgr_close(struct socket_mgr_state* state, int id) {
    if(id < 0) {
        return -1;
    }
    struct socket* s = get_socket(id);
    if(s->type == ST_INVALID || s->id != id) {
        return -2;
    }
    _request_close(state, id);
    return 0;
}


int
socket_mgr_attach(struct socket_mgr_state* state, int id, uint32_t actor_handle) {
    if(id < 0) {
        return -1;
    }
    struct socket* s = get_socket(id);
    if(s->type != ST_PREPARE || s->id != id) {
        return -2;
    }
    _request_attach(state, id, actor_handle);
    return 0;
}


void
socket_mgr_exit(struct socket_mgr_state* state) {
    struct request_package msg;
    msg.type = REQ_EXIT;
    msg.socket_id = -1;
    _request_send(state, &msg);
}


int
socket_mgr_addrinfo(struct socket_mgr_state* state, int id, struct socket_addrinfo* out_addrinfo, const char** out_error) {
    if(id <0) {
        return -1;
    }

    struct socket* s = get_socket(id);
    if(s->type == ST_INVALID || s->id != id) {
        return -2;
    }

    spinlock_lock(&s->lock);
    if(s->id != id) {
        spinlock_unlock(&s->lock);
        return -2;
    }
    int err = _socket_getaddr(state, s, out_addrinfo, out_error);
    spinlock_unlock(&s->lock);
    return err;
}

int
socket_mgr_send(struct socket_mgr_state* state, int id, const void* data, size_t size) {
    if(id < 0 || data == NULL || size == 0) {
        return -1;
    }
    struct socket* s = get_socket(id);
    enum socket_type st = s->type;
    if((st != ST_FORWARD && st != ST_CONNECTING && st != ST_CONNECTED) || s->id != id) {
        return -2;
    }

    ssize_t n = 0;
    if(spinlock_trylock(&s->lock)) {
        if(s->id != id) {
            spinlock_unlock(&s->lock);
            return -2;
        }

        if(write_buffer_empty(s) && st == ST_FORWARD) {
            int fd = s->fd;
            n = write(fd, data, size);
            if(n < 0) {
                n = 0;
            }else if(n < size) {
                n = size - n;
            }else if (n == size) {
                spinlock_unlock(&s->lock);
                return 0;
            }
        }
        spinlock_unlock(&s->lock);
    }

    assert(n>=0 && ((size_t)n)<size);
    struct buffer_block* block = _buffer_new_block( (void*)((uint8_t*)data+n), size - (size_t)n);
    _request_msgsend(state, id, block);
    return 0;
}



static void
_socket_do_send(struct socket_mgr_state* state, struct socket* s) {
    int fd = s->fd;
    struct buffer_block* p = s->write_buffer.head;
    size_t offset = 0;
    while(p) {
        struct buffer_block* next = p->next;
        offset = p->offset;
        void* data = p->buffer + offset;
        size_t sz = p->sz - offset;
        int n = write(fd, data, sz);
        // printf("do_send s:%p id:%d fd:%d size:%zd n:%d\n", s, s->id, s->fd, sz, n);
        if(n<0) {
            break;
        }else if(n<sz) {
            offset = (sz-n) + offset;
            break;
        }
        hive_free(p);
        p = next;
    }

    if(p) {
        p->offset = offset;
        sp_write(state->pfd, fd, s, true);
    }else {
        s->write_buffer.tail = NULL;
        sp_write(state->pfd, fd, s, false);
    }
    s->write_buffer.head = p;
}


static int
_socket_getaddr(struct socket_mgr_state* state, struct socket* s, struct socket_addrinfo* out_addrinfo, const char** out_error) {
    int fd = s->fd;
    struct sockaddr addr;
    socklen_t len = sizeof(addr);
    int err = getsockname(fd, &addr, &len);
    if(err < 0) {
        *out_error = strerror(errno);
        return 1;
    }

    char str_port[NI_MAXSERV];
    err = getnameinfo(&addr, len, out_addrinfo->ip, sizeof(out_addrinfo->ip), 
        str_port, sizeof(str_port), NI_NUMERICHOST | NI_NUMERICSERV);
    if(err != 0) {
        *out_error = gai_strerror(err);
        return 1;
    }
    
    out_addrinfo->port = atoi(str_port);
    return 0;
}




static void
_socket_do_listen(struct socket_mgr_state* state, struct socket* s) {
    int fd = s->fd;
    int client_fd = accept(fd, NULL, NULL);
    if(client_fd < 0) {
        sm_log("accept from socket id:%d is error[%d]:%s", s->id, errno, strerror(errno));
        return;
    }

    struct socket* cs = _socket_gen(state);
    if(cs == NULL) {
        sm_log("socket id poll is full.");
        close(client_fd);
        return;
    }

    cs->fd = client_fd;
    cs->actor_handle = SYS_HANDLE;
    cs->type = ST_PREPARE;
    sp_nonblocking(client_fd);
    _actor_notify_accept(s->id, cs->id, s->actor_handle);
}


static int
_socket_do_recv(struct socket_mgr_state* state, struct socket* s) {
    int fd = s->fd;
    int ret = SOCKET_OK;

    for(;;) {
        ssize_t n = read(fd, state->_recv_data->data, MAX_RECV_BUFFER);
        // printf("_socket_do_recv id:%d fd:%d n:%zd\n", s->id, fd, n);
        if(n < 0) {
            int err = errno;
            if(err == EAGAIN) {
                break;
            }else if(err == EINTR) {
                continue;
            }else {
                int len = snprintf((char*)state->_recv_data->data, MAX_RECV_BUFFER, "recv error[%d]: %s", err, strerror(err));
                assert(len > 0);
                // printf("recv_error:%s s:%p id:%d fd:%d\n", (char*)state->_recv_data->data, s, s->id, s->fd);
                _actor_notify_error(state, s, (size_t)(len+1));
                _socket_remove(state, s);
                ret = SOCKET_ERROR;
                break;
            }
        }else if (n == 0) {
            _actor_notify_break(s);
            // printf("recv_break s:%p id:%d fd:%d\n", s, s->id, s->fd);
            _socket_remove(state, s);
            ret = SOCKET_BREAK;
            break;
        }else {
            _actor_notify_recv(state, s, (size_t)n);
        }
    }
    return ret;
}


static bool
_socket_do_connect(struct socket_mgr_state* state, struct socket* s) {
    const char* error_str = _socket_check_error(s);
    if(error_str) {
        _actor_notify_connected(state, s, error_str);
        _socket_remove(state, s);
        return false;
    }else {
        s->type = ST_FORWARD;
        if(write_buffer_empty(s)) {
            sp_write(state->pfd, s->fd, s, false);
        }
        _actor_notify_connected(state, s, NULL);
        return true;
    }
}


static const char*
_socket_check_error(struct socket* s) {
    int err = 0;
    socklen_t len = sizeof(err);
    int code = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &err, &len);
    const char* error_str = NULL;
    if(code < 0 || err) {
        if(code >= 0) {
            error_str = strerror(err);
        } else {
            error_str = strerror(errno);
        }
    }
    return error_str;
}


static void
_actor_notify_recv(struct socket_mgr_state* state, struct socket* s, size_t size) {
    state->_recv_data->u.size = size;
    state->_recv_data->se = SE_RECIVE;
    hive_send(SYS_HANDLE, s->actor_handle, HIVE_TSOCKET, s->id, (void*)state->_recv_data, sizeof(struct socket_data)+size);
}


static void
_actor_notify_break(struct socket* s) {
    struct socket_data data;
    data.se = SE_BREAK;
    data.u.size = 0;
    hive_send(SYS_HANDLE, s->actor_handle, HIVE_TSOCKET, s->id, (void*)&data, sizeof(data));
}

static void
_actor_notify_connected(struct socket_mgr_state* state, struct socket* s, const char* err) {
    if(err == NULL) {
        struct socket_data data;
        data.se = SE_CONNECTED;
        data.u.size = 0;
        hive_send(SYS_HANDLE, s->actor_handle, HIVE_TSOCKET, s->id, (void*)&data, sizeof(data));
    } else {
        strncpy((char*)state->_recv_data->data, err, MAX_RECV_BUFFER-1);
        size_t size = strlen((char*)state->_recv_data->data)+1;
        state->_recv_data->u.size = size;
        state->_recv_data->se = SE_CONNECTED;
        hive_send(SYS_HANDLE, s->actor_handle, HIVE_TSOCKET, s->id, (void*)state->_recv_data, sizeof(struct socket_data)+size);
    }
}


static void
_actor_notify_error(struct socket_mgr_state* state, struct socket* s, size_t size) {
    state->_recv_data->u.size = size;
    state->_recv_data->se = SE_ERROR;
    hive_send(SYS_HANDLE, s->actor_handle, HIVE_TSOCKET, s->id, (void*)state->_recv_data, sizeof(struct socket_data)+size);
}


static void
_actor_notify_accept(int server_id, int client_id, uint32_t target_handle) {
    struct socket_data data;
    data.u.id = client_id;
    data.se = SE_ACCEPT;
    hive_send(SYS_HANDLE, target_handle, HIVE_TSOCKET, server_id, (void*)&data, sizeof(data));
}


static int
_socket_request_ctrl(struct socket_mgr_state* state, struct request_package* msg) {
    enum request_type type = msg->type;
    if(type == REQ_EXIT) {
        return -1;
    }

    int id = msg->socket_id;
    assert(id >= 0);
    struct socket* s = get_socket(id);

    // invalid socket id
    if(s->type == ST_INVALID || s->id != id) {
        return SOCKET_OK;
    }

    // printf("request_ctrl s:%p id:%d fd:%d type:%d\n", s, s->id, s->fd, type);
    switch(type) {
        case REQ_LISTEN: {
            sp_add(state->pfd, s->fd, s);
            break;
        }

        case REQ_CONNECT: {
            sp_add(state->pfd, s->fd, s);
            if(s->type == ST_CONNECTED) {
                s->type = ST_FORWARD;
                if(!write_buffer_empty(s)) {
                    sp_write(state->pfd, s->fd, s, true);
                }
                _actor_notify_connected(state, s, NULL);
            }else if(s->type == ST_CONNECTING) {
                sp_write(state->pfd, s->fd, s, true);
            }
            break;
        }

        case REQ_ATTACH: {
            spinlock_lock(&s->lock);
            _socket_attach(state, s, msg->v.attach_handle);
            spinlock_unlock(&s->lock);
            break;
        }

        case REQ_CLOSE: {
            _socket_remove(state, s);
            _insert_close_socket(state, s);
            return SOCKET_CLOSE;
        }

        case REQ_SEND: {
            struct buffer_block* block = msg->v.msgsend.block;
            if(s->type == ST_INVALID) {
                hive_free(block);
            }else {
                _buffer_append(s, block);
                sp_write(state->pfd, s->fd, s, true);
            }
            break;
        }

        default: {
            hive_panic("socket_mgr: invalid request type:%d.\n", type);
        }
    }

    return SOCKET_OK;
}


static int
_socket_do_ctrl(struct socket_mgr_state* state) {
    int fd = state->recvctrl_fd;
    struct request_package msg;

    for(;;) {
        int n = read(fd, &msg, PKG_SIZE);
        if(n < 0) {
            int err = errno;
            if(err == EAGAIN) {
                break;
            }else if (err == EINTR) {
                continue;
            }else {
                hive_panic("socket pipe control read a error:%d", err);
            }
        }else if(n == 0){
            hive_panic("socket pip control read request len is 0");
        }else if (n == PKG_SIZE) {
            int ret = _socket_request_ctrl(state, &msg);
            if(ret != SOCKET_OK) {
                return ret;
            }
        }else {
            hive_panic("socket pip control read request lens: %d is invalid", n);
        }
    }
    return SOCKET_OK;
}


static void
_socket_event_clear(struct socket_mgr_state* state, int idx, int n, struct socket* close_s) {
    int i;
    for(i=idx+1; i<n; i++) {
        struct event* e = &(state->sp_event[i]);
        struct socket* s = (struct socket*)e->s;
        if(s == close_s) {
            e->s = NULL;
            e->write = false;
            e->read = false;
        }
    }
}


int
socket_mgr_update(struct socket_mgr_state* state) {
    int n = sp_wait(state->pfd, state->sp_event, MAX_SP_EVENT);
    if(n <= 0) {
        int err = errno;
        if(err == EINTR) {
            return 0;
        }
        hive_panic("socket_mgr update sp_wait error:%d errno:%d\n", n, err);
    }

    /* // for teset kqueue and epoll
    {
        int i, j;
        for(i=0; i<n; i++) {
            struct event* e1 = &(state->sp_event[i]);
            struct socket* s1 = (struct socket*)e1->s;
            for(j=0; j<n; j++) {
                if(i == j) continue;
                struct event* e2 = &(state->sp_event[j]);
                struct socket* s2 = (struct socket*)e2->s;
                if(s1 == s2) {
                    printf("duplicate_event [%d] read:%d write:%d --> [%d] read:%d write:%d\n",
                        i, e1->read, e1->write, j, e2->read, e2->write);
                }
            }
        }
    }
    */

    int idx;
    for(idx=0; idx<n; idx++) {
        struct event* e = &(state->sp_event[idx]);
        struct socket* s = (struct socket*)e->s;
        // printf("[%d] count:%d event:%p s:%p id:%d fd:%d read:%d write:%d\n", idx, n, e, s, 
        //     (s)?(s->id):(-1), (s)?(s->fd):(-1), e->read, e->write);

        // pipe control request event
        if(s == NULL) {
            if(e->error) {
                hive_panic("socket pipe control is error.");
            }else if(e->read) {
                _clear_close_socket(state);
                int ret = _socket_do_ctrl(state);
                if(ret < 0) {
                    _clear_close_socket(state);
                    return ret;
                }else if(ret == SOCKET_CLOSE) {
                    // remove closed socket
                    size_t i;
                    for(i=0; i<state->prepare_close_sockets.idx; i++) {
                        struct socket* close_s = state->prepare_close_sockets.slots[i];
                        _socket_event_clear(state, idx, n, close_s);
                    }
                }else if(ret != SOCKET_OK){
                    hive_panic("invalid socket ctrl type:%d", ret);
                }
                _clear_close_socket(state);
                continue;
            }else {
                continue;
            }
        }

        // socket event
        enum socket_type stype = s->type;

        // check kqueue eof event
        if (e->eof) {
            _actor_notify_break(s);
            _socket_remove(state, s);
            continue;
        }

        // check socket connect
        if (s->type == ST_CONNECTING) {
            if(!_socket_do_connect(state, s)) {
                continue;
            }
            stype = s->type;
        }

        if(e->read) {
            switch(stype) {
                case ST_LISTEN: {
                    _socket_do_listen(state, s);
                    break;
                }

                case ST_FORWARD: {
                    int ret = _socket_do_recv(state, s);
                    if(ret != SOCKET_OK && e->write) {
                        continue;
                    }
                    break;
                }

                default:{
                    hive_panic("invalid  socket type: %d recv read event.", stype);
                    break;
                }   
            }
        }

        if(e->write) {
            switch(stype) {
                case ST_FORWARD: {
                    if(spinlock_trylock(&s->lock)) {
                        _socket_do_send(state, s);
                        if(write_buffer_empty(s)) {
                            sp_write(state->pfd, s->fd, s, false);
                        }
                        spinlock_unlock(&s->lock);
                    }
                    break;
                }

                default:{
                    hive_panic("invalid  socket type: %d recv write event.", stype);
                    break;
                }
            }
        }

        if(e->error) {
            const char* error_str = _socket_check_error(s);
            if(error_str == NULL) {
                error_str = "unknow error";
            }
            strncpy((char*)state->_recv_data->data, error_str, MAX_RECV_BUFFER-1);
            _actor_notify_error(state, s, strlen((char*)state->_recv_data->data)+1);
            _socket_remove(state, s);
        }
    }

    return 0;
}
