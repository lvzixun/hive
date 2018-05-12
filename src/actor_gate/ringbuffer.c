#include "hive_memory.h"
#include "actor_gate/imap.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>


#define HEADER_SIZE         2
#define MAX_PKG_SIZE        ((1<<(HEADER_SIZE*8)) - 1)
#define RINGBUFFER_MAX_SIZE 1024*10  // 10K buffer

struct ringbuffer_block {
    ssize_t size;
    int cap;
    int id;
    struct ringbuffer_block* next;
    uint8_t data[0];
};


struct ringbuffer_record {
    uint32_t handle;
    uint32_t cap;
    uint32_t size;
    struct {
        uint8_t header_buffer[HEADER_SIZE];
        uint8_t cap;
    }header_state;
    struct ringbuffer_block* head;
    struct ringbuffer_block* tail;
};


struct ringbuffer_context {
    struct imap_context* imap;
    struct ringbuffer_block* cur_block;
    uint8_t block_data[RINGBUFFER_MAX_SIZE];
    uint8_t temp_data[MAX_PKG_SIZE];
};




#define is_empty_block(b)           ((b)->size<0)
#define BLOCK_HEADER_SIZE           sizeof(struct ringbuffer_block)
#define block_size(data_size)       (sizeof(struct ringbuffer_block)+(data_size))
#define block_next(p, data_size)    (struct ringbuffer_block*)(((uint8_t*)(p))+block_size(data_size))



static struct ringbuffer_context *
_ringbuffer_create() {
    struct ringbuffer_context* ringbuffer = (struct ringbuffer_context*)hive_malloc(sizeof(struct ringbuffer_context));
    ringbuffer->cur_block = (struct ringbuffer_block*)ringbuffer->block_data;
    ringbuffer->cur_block->size = -(RINGBUFFER_MAX_SIZE - BLOCK_HEADER_SIZE);
    ringbuffer->cur_block->cap = 0;
    ringbuffer->cur_block->next = NULL;
    ringbuffer->imap = imap_create();
    return ringbuffer;
}


static inline struct ringbuffer_record *
_record_new(uint32_t handle, int id) {
    struct ringbuffer_record* record = (struct ringbuffer_record*)hive_malloc(sizeof(*record));
    record->tail = NULL;
    record->head = NULL;
    record->handle = handle;
    record->size = 0;
    record->cap = 0;
    return record;
}


static inline void
_record_free(struct ringbuffer_record* p) {
    hive_free(p);
}


static void
_ringbuffer_trigger_package(struct ringbuffer_record* record, uint8_t* data, size_t sz) {

}


static void
_ringbuffer_trigger_close(struct ringbuffer_record* record, int id) {
    uint32_t handle = record->handle;
    struct ringbuffer_block* p = record->head;
    while(p) {
        p->size = 0 - p->size;
        p->cap = 0;
        p = p->next;
    }
    // socket close id
    // notify handle actor close
}


static void
_block_resolve_complete(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record, uint8_t* data, int sz) {
    struct ringbuffer_block* p = record->head;
    uint8_t* tmp = ringbuffer->temp_data;
    size_t real_size = 0;
    while(p) {
        int cap = p->cap;
        memcpy(tmp, p->data, cap);
        tmp += cap;
        real_size += cap;
        p->size = 0 - p->size;  // mark is empty block
        p->cap = 0;
        p = p->next;
    }
    real_size += sz;
    memcpy(tmp, data, sz);
    _ringbuffer_trigger_package(record, tmp, real_size);
    record->head = NULL;
    record->tail = NULL;
    record->cap = 0;
    record->size = 0;
    record->header_state.cap = 0;
}


static inline void
_record_link_block(struct ringbuffer_record* record, struct ringbuffer_block* block) {
    if(record->tail == NULL) {
        assert(record->head == NULL);
        record->tail = block;
        record->head = block;
    }else {
        record->tail->next = block;
    }
    record->cap += block->cap;
}


static void
_block_collect(struct ringbuffer_context* ringbuffer, struct ringbuffer_block* block) {
    struct ringbuffer_block* end_block = ringbuffer->block_data + sizeof(ringbuffer->block_data);
    assert(is_empty_block(block));
    struct ringbuffer_block* p = block_next(block, -block->size);
    while(block<end_block && is_empty_block(p)) {
        block->size -= block_size(-p->size);
        p = block_next(p, -p->size);
    }
}


static void
_block_resolve_slice(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record, int id, uint8_t* data, int sz) {
    struct ringbuffer_block* cur_p = ringbuffer->cur_block;

    if(!is_empty_block(cur_p)) {
        // force close timeout socket
        struct ringbuffer_record* cur_record = imap_query(ringbuffer, p->id);
        assert(cur_record);
        _ringbuffer_trigger_close(cur_record);
        // is current socket id ?
        if(p->id == id) {
            return;
        }
    }

    assert(is_empty_block(cur_p));
    ssize_t b_sz = 0 - cur_p->size;
    if(sz + block_size(8) <= b_sz) {
        memcpy(cur_p->data, data, sz);
        cur_p->size = sz;
        cur_p->cap = sz;
        cur_p->next = NULL;
        cur_p->id = id;
        _record_link_block(record, cur_p);
        struct ringbuffer_block* np = block_next(cur_p, sz);
        np->size = -(b_sz - sz - sizeof(struct ringbuffer_block));
        np->cap = 0;
        np->next = NULL;
        ringbuffer->cur_block = np;
    } else {
        _block_collect(ringbuffer, cur_p);
    }
}


static struct ringbuffer_block *
_block_new(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record, int id, uint8_t* data, int sz) {
    uint32_t expect_size = record->size;
    uint32_t new_cap = record->cap + sz;

    // get complete package
    if(new_cap == expect_size) {
        _block_resolve_complete(ringbuffer, record, data, sz);

    } else if (new_cap < expect_size) {
        _block_resolve_slice(ringbuffer, record, id, data, sz);

    } else {
        assert(false);
    }

    return NULL;
}


static void
_ringbuffer_add(struct ringbuffer_context* ringbuffer, uint32_t source, int id, uint8_t* data, int sz) {
    struct imap_context* imap = ringbuffer->imap;
    struct ringbuffer_record * record = (struct ringbuffer_record*)imap_query(imap, id);
    // is first add?
    if(record == NULL) {
        record = _record_new(source);
        imap_set(imap, id, record);
    }

    while(sz > 0) {
        // read header
        if(record->header_state.cap < HEADER_SIZE) {
            assert(record->size == 0);
            assert(record->cap == 0);
            int expect = HEADER_SIZE - record->header_state.cap;
            int read_sz = 0;
            uint8_t* read_p = NULL;
            if(expect > sz) {
                read_p = data;
                read_sz = sz;
                data = NULL;
                sz = 0;
            } else {
                read_sz = expect;
                read_p = data;
                sz -= expect;
                data += expect;
            }
            memcpy(record->header_state.header_buffer + record->header_state.cap, read_p, read_sz);
            record->header_state.cap += read_sz;
            if(read_sz == expect) {
                uint32_t v = 0;
                int i=0;
                // big endian
                for(i=0; i<HEADER_SIZE; i++) {
                    uint32_t b = (uint32_t)record->header_state.header_buffer[i];
                    b = b << (8*(HEADER_SIZE-i-1));
                    v |= b;
                }
                record->size = v;
                record->header_state.cap = 0;
            }
        }

        // read data
        assert(sz >= 0);
        if(sz > 0) {
            uint32_t expect_size = record->size;
            int real_size = (sz>=expect_size)?((int)expect_size):(sz);
            struct ringbuffer_block* nb = _block_new(ringbuffer, record, data, real_size);
            if(!nb) {
                break;
            }
            assert(nb->cap == real_size);
            sz -= real_size;
            data += real_size;
        }
    }
}

