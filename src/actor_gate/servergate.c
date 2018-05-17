#include "hive_memory.h"
#include "actor_gate/imap.h"
#include "actor_gate/servergate.h"


#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


#define HEADER_SIZE         2
#define MAX_PKG_SIZE        ((1<<(HEADER_SIZE*8)) - 1)


struct connect_state {
    int id;
    uint16_t  cap;
    uint16_t  size;
    uint8_t*  buffer;
    struct {
        uint8_t header_buffer[HEADER_SIZE];
        uint8_t cap;
    }header_state;
};


struct servergate_context {
    struct imap_context* imap;
    servergate_msg msg_cb;
};


static void _connect_state_free(struct connect_state* state);

struct servergate_context *
servergate_create() {
    assert(HEADER_SIZE == sizeof(uint16_t));
    struct servergate_context* context = (struct servergate_context*)hive_malloc(sizeof(*context));
    context->imap = imap_create();
    return context;
}


static void
_connect_ob(int id, struct connect_state* state) {
    _connect_state_free(state);
}


void
servergate_free(struct servergate_context* context) {
    imap_dump(context->imap, (observer)_connect_ob);    
    imap_free(context->imap);
}


static struct connect_state *
_connect_state_new(int id) {
    struct connect_state* ret = (struct connect_state*)hive_malloc(sizeof(*ret));
    memset(ret, 0, sizeof(*ret));
    ret->id = id;
    return ret;
}


static void
_connect_state_free(struct connect_state* state) {
    if(state->buffer) {
        hive_free(state->buffer);
    }
    hive_free(state);
}


static void
_connect_resolve(struct servergate_context* context, struct connect_state* state, uint8_t* data, size_t size) {
    assert(state->cap + size <= state->size);

    memcpy(state->buffer+ state->cap, data, size);
    state->cap += size;
    if(state->cap == state->size) {
        // complete it!!
        if(context->msg_cb) {
            context->msg_cb(state->id, state->buffer, state->size);
        }else {
            hive_free(state->buffer);
        }
        state->buffer = NULL;
        state->cap = 0;
        state->size = 0;
        state->header_state.cap = 0;
    }
}


void
servergate_add(struct servergate_context* context, int id, uint8_t* data, size_t size) {
    struct imap_context* imap = context->imap;
    struct connect_state* state = (struct connect_state*)imap_query(imap, id);

    if(state == NULL) {
        state = _connect_state_new(id);
        imap_set(imap, id, state);
    }

    while(size > 0) {
        // read header
        if(state->header_state.cap < HEADER_SIZE) {
            assert(state->size == 0);
            assert(state->cap == 0);
            assert(state->buffer == NULL);
            size_t expect = HEADER_SIZE - state->header_state.cap;
            size_t read_sz = 0;
            uint8_t* read_p = NULL;
            if(expect > size) {
                read_p = data;
                read_sz = size;
                data = NULL;
                size = 0;
            } else {
                read_sz = expect;
                read_p = data;
                size -= expect;
                data += expect;
            }
            memcpy(state->header_state.header_buffer + state->header_state.cap, read_p, read_sz);
            state->header_state.cap += read_sz;
            if(read_sz == expect) {
                uint16_t v = 0;
                int i=0;
                // big endian
                for(i=0; i<HEADER_SIZE; i++) {
                    uint32_t b = (uint32_t)state->header_state.header_buffer[i];
                    b = b << (8*(HEADER_SIZE-i-1));
                    v |= b;
                }
                state->size = v;
                state->buffer = (uint8_t*)hive_malloc(v);
            }
        }

        if(size > 0) {
            uint16_t expect_size = state->size - state->cap;
            int real_size = (size>=expect_size)?((int)expect_size):(size);
            _connect_resolve(context, state, data, real_size);
            size -= real_size;
            data += real_size;
        }
    }
}

void
servergate_del(struct servergate_context* context, int id) {
    struct connect_state* state = (struct connect_state*)imap_remove(context->imap, id);
    if(state) {
        _connect_state_free(state);
    }
}


void
servergate_cb(struct servergate_context* context, servergate_msg msg_cb) {
    context->msg_cb = msg_cb;
}
