#include "hive.h"
#include "hive_socket.h"
#include "hive_memory.h"
#include "hive_log.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

enum imap_status {
    IS_NONE,
    IS_EXIST,
    IS_REMOVE,
};

struct imap_slot {
    int key;
    void* value;
    enum imap_status status;
    struct imap_slot* next;
};

struct imap_context {
    struct imap_slot* slots;
    size_t size;
    size_t count;
    struct imap_slot* lastfree;
};


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


struct actor_gate {
    struct ringbuffer_context* ringbuffer;
    uint32_t acotr_handle;    
};

#define is_empty_block(b) ((b)->size<0)
#define BLOCK_HEADER_SIZE sizeof(struct ringbuffer_block)
#define DEFAULT_IMAP_SLOT_SIZE 8

static struct imap_context * _imap_create();
static void _imap_set(struct imap_context* imap, int key, void* handle);
static void * _imap_query(struct imap_context* imap, int key);


static struct ringbuffer_context *
_ringbuffer_create() {
    struct ringbuffer_context* ringbuffer = (struct ringbuffer_context*)hive_malloc(sizeof(struct ringbuffer_context));
    ringbuffer->cur_block = (struct ringbuffer_block*)ringbuffer->block_data;
    ringbuffer->cur_block->size = -(RINGBUFFER_MAX_SIZE - BLOCK_HEADER_SIZE);
    ringbuffer->cur_block->cap = 0;
    ringbuffer->cur_block->next = NULL;
    ringbuffer->imap = _imap_create();
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
_ringbuffer_trigger_close(struct ringbuffer_record* record) {

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
    _ringbuffer_trigger(record, tmp, real_size);
    record->head = NULL;
    record->tail = NULL;
    record->cap = 0;
    record->size = 0;
    record->header_state.cap = 0;
}


static void
_block_resolve_slice(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record, int id, uint8_t* data, int sz) {
    if(is_empty_block(ringbuffer->cur_block)) {

    } else {
        struct ringbuffer_record* cur_record = _imap_query(ringbuffer, p->id);
        assert(cur_record);
        _ringbuffer_trigger_close(cur_record);
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
    struct ringbuffer_record * record = (struct ringbuffer_record*)_imap_query(imap, id);
    if(record == NULL) {
        record = _record_new(source);
        _imap_set(imap, id, record);
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
            if(expect <= sz) {
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


static struct imap_context *
_imap_create() {
    struct imap_context* imap = (struct imap_context*)hive_malloc(sizeof(*imap));
    imap->slots = (struct imap_slot*)hive_calloc(DEFAULT_IMAP_SLOT_SIZE, sizeof(struct imap_slot));
    imap->size = DEFAULT_IMAP_SLOT_SIZE;
    imap->count = 0;
    imap->lastfree = imap->slots + imap->size;
    return imap;
}


static void
_imap_free(struct imap_context* imap) {
    hive_free(imap->slots);
    hive_free(imap);
}


static inline int
_imap_hash(struct imap_context* imap, int key) {
    int hash = key % (int)(imap->size);
    return hash;
}


static void
_imap_rehash(struct imap_context* imap) {
    size_t new_sz = DEFAULT_IMAP_SLOT_SIZE;
    struct imap_slot* old_slots = imap->slots;
    size_t old_count = imap->count;
    size_t old_size = imap->size;
    while(new_sz <= imap->count) {
        new_sz *= 2;
    }

    struct imap_slot* new_slots = (struct imap_slot*)hive_calloc(new_sz, sizeof(struct imap_slot));
    imap->lastfree = new_slots + new_sz;
    imap->size = new_sz;
    imap->slots = new_slots;
    imap->count = 0;

    size_t i=0;
    for(i=0; i<old_size; i++) {
        struct imap_slot* p = &(old_slots[i]);
        enum imap_status status = p->status;
        if(status == IS_EXIST) {
            _imap_set(imap, p->key, p->value);
        }
    }

    assert(old_count == imap->count);
    hive_free(old_slots);
}


static void *
_imap_query(struct imap_context* imap, int key) {
    int hash = _imap_hash(imap, key);
    struct imap_slot* p = &(imap->slots[hash]);
    if(p->status != IS_NONE) {
        while(p) {
            if(p->key == key && p->status == IS_EXIST) {
                return p->value;
            }
            p = p->next;
        }
    }
    return NULL;
}

static struct imap_slot *
_imap_getfree(struct imap_context* imap) {
    while(imap->lastfree > imap->slots) {
        imap->lastfree--;
        if(imap->lastfree->status == IS_NONE) {
            return imap->lastfree;
        }
    }
    return NULL;
}



static void
_imap_set(struct imap_context* imap, int key, void* value) {
    assert(value);
    int hash = _imap_hash(imap, key);
    struct imap_slot* p = &(imap->slots[hash]);
    if(p->status == IS_EXIST) {
        struct imap_slot* np = p;
        while(np) {
            if(np->key == key && np->status == IS_EXIST) {
                p->value = value;
                return;
            }
            np = np->next;
        }

        np = _imap_getfree(imap);
        if(np == NULL) {
            _imap_rehash(imap);
            _imap_set(imap, key, value);
            return;
        }

        int main_hash = _imap_hash(imap, p->key);
        np->next = p->next;
        p->next = np;
        if(main_hash == hash) {
            p = np;
        }else {
            np->key = p->key;
            np->value = p->value;
        }
    }

    imap->count++;
    p->status = IS_EXIST;
    p->key = key;
    p->value = value;
}


static void *
_imap_remove(struct imap_context* imap, int key) {
    struct imap_slot* p = _imap_query(imap, key);
    if(p) {
        imap->count--;
        p->status = IS_REMOVE;
        return p->value;
    }
    return NULL;
}


void actor_gate_init() {
    // todo it!!
}

