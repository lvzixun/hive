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

#define unused(v)  ((void)v)

struct hive_env {
    int thread;
    bool staring;
    bool exit;
}ENV;


void
hive_init() {
    ENV.thread = 4;
    ENV.staring = false;
    ENV.exit = false;
    hive_actor_init();
}


void
hive_exit() {
    ENV.exit = true;
    // notify timer thread exit
    // todo it!

    // notify socket thread exit
    // todo it

    // send exit message to all actors
    hive_actor_exit();
}


static void*
_thread_socket(void* p) {
    unused(p);
    usleep(2000);
    // todo it!
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
        usleep(1500);

        if(ENV.exit) {
            break;
        }
    }
    return NULL;
}

static void
_create_thread(pthread_t* thread, void*(*progress)(void*), void* arg) {
    if(pthread_create(thread, NULL, progress, arg)) {
        fprintf(stderr, "create thread failed");
        exit(1);
    }
}

int
hive_start() {
    pthread_t pid[ENV.thread+2];
    int len = sizeof(pid)/sizeof(pid[0]);
    if(ENV.staring) {
        fprintf(stderr, "hive is running");
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


    // free actors chain
    hive_actor_free();
    return 0;
}


bool
hive_register(char* name, hive_actor_cb cb) {
    uint32_t handle = hive_actor_create(name, cb);
    return handle == 0;
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


int 
main(int argc, char const *argv[]) {
    hive_init();

    // regisger bootstrap actor
    // todo it!!

    hive_start();

    //  for debug memory leak
    #ifdef DEBUG_MEMORY
        hive_memdump();
    #endif
    return 0;
}