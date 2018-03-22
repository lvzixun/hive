#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include "hive.h"
#include "hive_actor.h"
#include "hive_memory.h"
#include "hive_bootstrap.h"
#include "hive_log.h"
#include "socket_mgr.h"

#define unused(v)  ((void)v)

static struct hive_env {
    int thread;
    bool staring;
    bool exit;
    struct socket_mgr_state* sm_state;
}ENV;


void
hive_init() {
    hive_actor_init();
    ENV.thread = 4;
    ENV.staring = false;
    ENV.exit = false;
    ENV.sm_state = socket_mgr_create();
    assert(ENV.sm_state);
}


void
hive_exit() {
    ENV.exit = true;
    // notify timer thread exit
    // todo it!

    // notify socket thread exit
    socket_mgr_exit(ENV.sm_state);

    // send exit message to all actors
    hive_actor_exit();
}


static void*
_thread_socket(void* p) {
    unused(p);
    for(;;) {
        int ret = socket_mgr_update(ENV.sm_state);
        if(ret < 0) {
            break;
        }
    }
    return NULL;
}


static void*
_thread_timer(void* p) {
    unused(p);
    usleep(1500);
    // todo it!
    return NULL;
}


static void*
_thread_worker(void* p) {
    unused(p);
    for(;;) {
        int ret = hive_actor_dispatch();
        unused(ret);
        usleep(100);

        if(ENV.exit && ret == 0) {
            break;
        }
    }
    return NULL;
}

static void
_create_thread(pthread_t* thread, void*(*progress)(void*), void* arg) {
    if(pthread_create(thread, NULL, progress, arg)) {
        hive_panic("create thread failed");
    }
}

int
hive_start() {
    pthread_t pid[ENV.thread+2];
    int len = sizeof(pid)/sizeof(pid[0]);
    if(ENV.staring) {
        hive_printf("hive is running");
        return 1;
    }

    _create_thread(&pid[0], _thread_socket, NULL);
    _create_thread(&pid[1], _thread_timer, NULL);
    int i=0;
    for(i=2; i<len; i++) {
        pthread_t* thread = &pid[i];
        _create_thread(thread, _thread_worker, NULL);
    }

    for(i=0; i<len; i++) {
        pthread_join(pid[i], NULL);
    }

    // free socker manger
    socket_mgr_release(ENV.sm_state);

    // free actors chain
    hive_actor_free();
    return 0;
}


// ---------------- hive actor api ----------------  

uint32_t
hive_register(char* name, hive_actor_cb cb, void* ud) {
    return hive_actor_create(name, cb, ud);
}

bool
hive_unregister(uint32_t handle) {
    int ret = hive_actor_release(handle);
    return ret == 0;
}

bool
hive_send(uint32_t source, uint32_t target, int type, int session, void* data, size_t size) {
    int ret = hive_actor_send(source, target, type, session, data, size);
    return ret == 0;
}


// ---------------- hive socket api ----------------  
int 
hive_socket_connect(const char* host, uint16_t port, uint32_t actor_handle, char const** out_error) {
    int id = socket_mgr_connect(ENV.sm_state, host, port, out_error, actor_handle);
    return id;
}

int 
hive_socket_listen(const char* host, uint16_t port, uint32_t actor_handle) {
    return socket_mgr_listen(ENV.sm_state, host, port, actor_handle);
}


int 
hive_socket_send(int id, const void* data, size_t size) {
    return socket_mgr_send(ENV.sm_state, id, data, size);
}

int
hive_socket_addrinfo(int id, struct socket_addrinfo* out_addrinfo, const char** out_error) {
    return socket_mgr_addrinfo(ENV.sm_state, id, out_addrinfo, out_error);
}

int
hive_socket_close(int id) {
    return socket_mgr_close(ENV.sm_state, id);
}

int 
hive_socket_attach(int id, uint32_t actor_handle) {
    return socket_mgr_attach(ENV.sm_state, id, actor_handle);
}

int 
main(int argc, char const *argv[]) {
    hive_init();

    // start bootstrap actor
    hive_bootstrap_init((argc>1)?(argv[1]):(NULL));

    hive_start();

    //  for debug memory leak
    #ifdef DEBUG_MEMORY
        hive_memdump();
    #endif
    return 0;
}