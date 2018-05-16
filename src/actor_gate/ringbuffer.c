#include "hive_memory.h"
#include "actor_gate/imap.h"
#include "actor_gate/ringbuffer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>


#define HEADER_SIZE         2
#define MAX_PKG_SIZE        ((1<<(HEADER_SIZE*8)) - 1)
#define RINGBUFFER_MAX_SIZE 1024*10  // 10K buffer

struct ringbuffer_block {
    ssize_t size;
    int id;
    struct ringbuffer_block* next;
    uint8_t data[0];
};


struct ringbuffer_record {
    int id;
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
    ringbuffer_trigger_close close_cb;
    ringbuffer_trigger_package package_cb;
};




#define is_empty_block(b)           ((b)->size<0)
#define BLOCK_HEADER_SIZE           sizeof(struct ringbuffer_block)
#define BLOCK_MAXSZ                 (RINGBUFFER_MAX_SIZE - BLOCK_HEADER_SIZE) 
#define block_size(data_size)       (sizeof(struct ringbuffer_block)+(data_size))
#define block_next(p, data_size)    (struct ringbuffer_block*)(((uint8_t*)(p))+block_size(data_size))
#define block_end(ringbuffer)       ((struct ringbuffer_block*)((ringbuffer)->block_data + sizeof((ringbuffer)->block_data)))
#define block_head(ringbuffer)      ((struct ringbuffer_block*)((ringbuffer)->block_data))

static void _dump_ringbuffer(struct ringbuffer_context * ringbuffer);


struct ringbuffer_context *
ringbuffer_create() {
    struct ringbuffer_context* ringbuffer = (struct ringbuffer_context*)hive_malloc(sizeof(struct ringbuffer_context));
    ringbuffer->cur_block = (struct ringbuffer_block*)ringbuffer->block_data;
    ringbuffer->cur_block->size = -BLOCK_MAXSZ;
    ringbuffer->cur_block->next = NULL;
    ringbuffer->close_cb = NULL;
    ringbuffer->package_cb = NULL;
    ringbuffer->imap = imap_create();
    return ringbuffer;
}


void
ringbuffer_free(struct ringbuffer_context* ringbuffer) {
    // free imap value todo !!
    imap_free(ringbuffer->imap);

    // free ringbuffer context
    hive_free(ringbuffer);
}


void
ringbuffer_cb(struct ringbuffer_context* ringbuffer, ringbuffer_trigger_close close_cb, ringbuffer_trigger_package package_cb) {
    ringbuffer->close_cb = close_cb;
    ringbuffer->package_cb = package_cb;
}


static inline struct ringbuffer_record *
_record_new(int id) {
    struct ringbuffer_record* record = (struct ringbuffer_record*)hive_malloc(sizeof(*record));
    record->tail = NULL;
    record->head = NULL;
    record->size = 0;
    record->id = id;
    record->cap = 0;
    record->header_state.cap = 0;
    return record;
}


static inline void
_record_free(struct ringbuffer_record* p) {
    hive_free(p);
}


static void
_ringbuffer_trigger_package(struct ringbuffer_context* ringbuffer, int id, uint8_t* data, size_t sz) {
    if(ringbuffer->package_cb) {
        ringbuffer->package_cb(id, data, sz);
    }
}


static void
_ringbuffer_trigger_close(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record) {
    struct ringbuffer_block* p = record->head;
    int id = p->id;
    while(p) {
        p->size = 0 - p->size;
        p = p->next;
    }

    struct ringbuffer_record* rp  = imap_remove(ringbuffer->imap, id);
    assert(rp == record);
    _record_free(rp);

    if(ringbuffer->close_cb) {
        ringbuffer->close_cb(id);
    }
}


static void
_block_resolve_complete(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record, uint8_t* data, int sz) {
    assert(record->cap + sz == record->size);
    struct ringbuffer_block* p = record->head;
    uint8_t* tmp = ringbuffer->temp_data;
    size_t real_size = 0;
    while(p) {
        int cap = p->size;
        memcpy(tmp, p->data, cap);
        tmp += cap;
        real_size += cap;
        p->size = 0 - p->size;  // mark is empty block
        p = p->next;
    }
    real_size += sz;
    memcpy(tmp, data, sz);
    _ringbuffer_trigger_package(ringbuffer, record->id, ringbuffer->temp_data, real_size);
    record->head = NULL;
    record->tail = NULL;
    record->cap = 0;
    record->size = 0;
    record->header_state.cap = 0;
}


static inline void
_record_link_block(struct ringbuffer_record* record, struct ringbuffer_block* block) {
    assert(block->next == NULL);
    if(record->tail == NULL) {
        assert(record->head == NULL);
        record->tail = block;
        record->head = block;
    }else {
        record->tail->next = block;
    }
    record->cap += block->size;
}


static bool
_block_collect(struct ringbuffer_context* ringbuffer, int id, size_t expect_cap) {
    size_t collect_sz = 0;
    struct ringbuffer_block* end_block = block_end(ringbuffer);
    struct ringbuffer_block* cur_p = ringbuffer->cur_block;
    struct ringbuffer_block* p = cur_p;

    if(expect_cap >= BLOCK_MAXSZ) {
        return false;
    }

    while(p<end_block) {
        if(!is_empty_block(p)) {
            // force close timeout socket
            int cur_id = p->id;
            struct ringbuffer_record* cur_record = imap_query(ringbuffer->imap, cur_id);
            assert(cur_record);
            // close timeout socket
            _ringbuffer_trigger_close(ringbuffer, cur_record);
            // the package is to big
            if(cur_id == id) {
                return false;
            }
        }

        assert(is_empty_block(p));
        struct ringbuffer_block* next_p = block_next(p, -p->size);
        if(p!=cur_p) {
            ssize_t sz = (ssize_t)block_size(-p->size);
            cur_p->size -= sz;
        }
        collect_sz = -cur_p->size;
        if(collect_sz>= expect_cap) {
            return true;
        }
        p = next_p;
    }

    ringbuffer->cur_block = block_head(ringbuffer);
    return _block_collect(ringbuffer, id, expect_cap);
}


static bool
_block_resolve_slice(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record, int id, uint8_t* data, int sz) {
    size_t expect_cap = sz + block_size(8);
    bool b = _block_collect(ringbuffer, id, expect_cap);
    if(!b) {
        return false;
    }

    // insert new block
    struct ringbuffer_block* cur_p = ringbuffer->cur_block;
    assert(is_empty_block(cur_p));

    ssize_t b_sz = 0 - cur_p->size;
    memcpy(cur_p->data, data, sz);
    cur_p->size = sz;
    cur_p->next = NULL;
    cur_p->id = id;
    _record_link_block(record, cur_p);
    struct ringbuffer_block* np = block_next(cur_p, sz);
    np->size = -(b_sz - sz - BLOCK_HEADER_SIZE);
    np->next = NULL;
    ringbuffer->cur_block = np;
    assert(block_next(np, -np->size) <= block_end(ringbuffer));
    return true;
}


static bool
_block_resolve(struct ringbuffer_context* ringbuffer, struct ringbuffer_record* record, int id, uint8_t* data, int sz) {
    uint32_t expect_size = record->size;
    uint32_t new_cap = record->cap + sz;

    // get complete package
    if(new_cap == expect_size) {
        _block_resolve_complete(ringbuffer, record, data, sz);
        return true;
    } else if (new_cap < expect_size) {
        return _block_resolve_slice(ringbuffer, record, id, data, sz);

    } else {
        assert(false);
    }
    return false;
}


void
ringbuffer_add(struct ringbuffer_context* ringbuffer, int id, uint8_t* data, int sz) {
    struct imap_context* imap = ringbuffer->imap;
    struct ringbuffer_record * record = (struct ringbuffer_record*)imap_query(imap, id);

    // is first add?
    if(record == NULL) {
        record = _record_new(id);
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
            }
        }

        // read data
        assert(sz >= 0);
        if(sz > 0) {
            uint32_t expect_size = record->size - record->cap;
            assert(expect_size > 0);
            int real_size = (sz>=expect_size)?((int)expect_size):(sz);
            bool b = _block_resolve(ringbuffer, record, id, data, real_size);
            if(!b) {
                break;
            }
            sz -= real_size;
            data += real_size;
        }
    }
}


/*
static void
_dump_ringbuffer(struct ringbuffer_context * ringbuffer) {
    struct ringbuffer_block* head = block_head(ringbuffer);
    struct ringbuffer_block* end = block_end(ringbuffer);
    struct ringbuffer_block* cur = ringbuffer->cur_block;
    int i=0;
    while(head<end) {
        char is_cur = (head == cur)?('*'):(' ');
        printf("[block %d] addr:%p size:%zd id:%d next:%p  %c\n", i, head, head->size, head->id, head->next, is_cur);
        size_t sz = (head->size < 0) ?((size_t)(-head->size)):((size_t)head->size);
        head = block_next(head, sz);
        i++;
    }

    assert(head == end);
}

static void
_trigger_close(int id) {
    printf("id close!!!\n");
}

static void
_trigger_package(int id, uint8_t* data, size_t sz) {
    size_t i=0;
    printf("package id:%d sz:%zu data:", id, sz);
    for(i=0; i<sz; i++) {
        printf(" %d", data[i]);
    }
    printf("\n");
}


int main(int argc, char const *argv[]) {
    struct ringbuffer_context* ringbuffer = ringbuffer_create();

    ringbuffer_cb(ringbuffer, _trigger_close, _trigger_package);

    uint8_t tmp1[] = {0x00, 0x05, 11, 12, 13, 14, 15};
    ringbuffer_add(ringbuffer, 1, tmp1, sizeof(tmp1));

    uint8_t tmp2[] = {0x00, 0x04, 21, 22, 23, 24, 0x00};
    ringbuffer_add(ringbuffer, 1, tmp2, sizeof(tmp2));

    uint8_t tmp3[] = {0x02, 31, 32, 0x00, 0x03, 41, 42, 43};
    ringbuffer_add(ringbuffer, 1, tmp3, sizeof(tmp3));

    uint8_t tmp4[] = {0x00, 0x02, 51, 52, 0x00, 0x03, 61, 62, 63};
    ringbuffer_add(ringbuffer, 1, tmp4, sizeof(tmp4));

    uint8_t tmp5[] = {0x00, 0x09, 71, 72, 73, 74};
    ringbuffer_add(ringbuffer, 1, tmp5, sizeof(tmp5));

    uint8_t tmp6[] = {75, 76, 77, 78, 79};
    ringbuffer_add(ringbuffer, 1, tmp6, sizeof(tmp6));

    uint8_t tmp7[] = {0x00, 0x20, 81, 82, 83, 84, 85, 86};
    ringbuffer_add(ringbuffer, 1, tmp7, sizeof(tmp7));
    _dump_ringbuffer(ringbuffer);
    printf("------------\n");

    uint8_t tmp8[] = {0x00, 0x06, 91, 92, 93, 94, 95};
    ringbuffer_add(ringbuffer, 2, tmp8, sizeof(tmp8));

    uint8_t tmp9[] = {96};
    ringbuffer_add(ringbuffer, 2, tmp9, sizeof(tmp9));

    _dump_ringbuffer(ringbuffer);

    ringbuffer_free(ringbuffer);
    return 0;
}
*/