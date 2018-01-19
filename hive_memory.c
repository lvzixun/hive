#ifdef DEBUG_MEMORY

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "spinlock.h"

#define HEAD_SIZE  sizeof(int)
#define MAX_MEMORY_SLOT_COUNT 0xffff

#define _head(p) *(((int*)(p))-1)
#define _set_head(p, c) (_head(p) = (c))
#define _point(p) (((int*)(p))-1)

struct memory_slot {
    const char* file;
    int line;
    void* p;
    size_t size;
};

struct {
    struct spinlock lock;
    struct memory_slot buffer[MAX_MEMORY_SLOT_COUNT];
    int last_free_idx;
} MEMORY_CONTEXT;  // lock will be init


#define context_lock()          spinlock_lock(&MEMORY_CONTEXT.lock)
#define context_unlock()        spinlock_unlock(&MEMORY_CONTEXT.lock)


static inline int
_get_idx() {
    int i=0;
    int ret = MEMORY_CONTEXT.last_free_idx;
    if(MEMORY_CONTEXT.buffer[ret].size) {
        printf("[hive_memory] need more memory slot.\n");
        exit(1);
    }

    for(i=ret+1; i<MAX_MEMORY_SLOT_COUNT; i++) {
        if(MEMORY_CONTEXT.buffer[i].size == 0) {
            MEMORY_CONTEXT.last_free_idx = i;
            break;
        }
    }
    return ret;
}


void*
hive_memory_malloc(size_t size, const char* file, int line) {
    int* ret = malloc(size + HEAD_SIZE);
    
    context_lock();
    int free_idx = _get_idx();
    *ret = free_idx;
    struct memory_slot* slot_p = &(MEMORY_CONTEXT.buffer[free_idx]);
    assert(slot_p->size == 0);
    slot_p->size = size;
    context_unlock();

    slot_p->file = file;
    slot_p->p = (void*)(ret+1);
    slot_p->line = line;
    return slot_p->p;
}


void*
hive_memory_calloc(size_t count, size_t size, const char* file, int line) {
    size_t c_size = count*size;
    void* ret = hive_memory_malloc(c_size, file, line);
    memset(ret, 0, c_size);
    return ret;
}


void
hive_memory_free(void* p) {
    assert(p);
    int idx = _head(p);
    assert(idx>=0 && idx<MAX_MEMORY_SLOT_COUNT &&
        MEMORY_CONTEXT.buffer[idx].p == p &&
        MEMORY_CONTEXT.buffer[idx].size > 0);

    free(_point(p));

    struct memory_slot* slot_p = &(MEMORY_CONTEXT.buffer[idx]);
    context_lock();
    slot_p->p = NULL;
    slot_p->size = 0;
    context_unlock();
}


void*
hive_memory_realloc(void* p, size_t size, const char* file, int line) {
    int idx = _head(p);
    assert(idx >=0 && idx < MAX_MEMORY_SLOT_COUNT);
    assert(MEMORY_CONTEXT.buffer[idx].size >0);

    int* ret = realloc(_point(p), size+HEAD_SIZE);
    *ret = idx;

    struct memory_slot* slot_p = &(MEMORY_CONTEXT.buffer[idx]);
    slot_p->size = size;
    slot_p->line = line;
    slot_p->file = file;
    slot_p->p = (void*)(ret+1);

    return slot_p->p;
}



// for test
void 
hive_memroy_dump() {
    int i=0;
    printf("-------memory check----------\n");
    for(i=0; i<MAX_MEMORY_SLOT_COUNT; i++) {
        struct memory_slot* slot_p = &(MEMORY_CONTEXT.buffer[i]);
        size_t size = slot_p->size;
        if(size) {
            const char* file = slot_p->file;
            int line = slot_p->line;
            void* point = slot_p->p;
            printf("[memory leakly]: index: %d point: %p leakly size %zd   @file:  %s:%d\n", i, point, size, file, line);
        }
    }
}

#endif

