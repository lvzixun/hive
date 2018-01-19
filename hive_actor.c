#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "hive_memory.h"
#include "spinlock.h"
#include "rwlock.h"

#include "hive_mq.h"
#include "hive_actor.h"

struct hive_actor_context {
    char* name;
    uint32_t handle;
    hive_actor_cb cb;
    bool is_progress;
    bool is_release;
    struct hive_message_queue* q;

    struct hive_actor_context* next;
    struct hive_actor_context* prev;
};


#define DEFAULT_ACTOR_CAP  4
#define MAX_PROGRESS_ACTOR_COUNT 0x10000


struct {
    struct hive_actor_context* progress[MAX_PROGRESS_ACTOR_COUNT];
    bool flags[MAX_PROGRESS_ACTOR_COUNT];
    size_t head;
    size_t tail;

    struct {
        struct rwlock lock;
        struct hive_actor_context** list;
        uint32_t handle_index;
        size_t size;
    } actors;
} ACTOR_MGR;

#define GP(i) ((i)%MAX_PROGRESS_ACTOR_COUNT)
#define ACTORS ACTOR_MGR.actors
#define actors_rlock() rwlock_rlock(&ACTORS.lock)
#define actors_runlock() rwlock_runlock(&ACTORS.lock)
#define actors_wlock() rwlock_wlock(&ACTORS.lock)
#define actors_wunlock() rwlock_wunlock(&ACTORS.lock)

#define hande2hash(handle) ((handle)&(ACTORS.size-1))

static struct hive_actor_context* _actor_new(char* name, uint32_t handle, hive_actor_cb cb);
static void _actor_free(struct hive_actor_context* actor);
static struct hive_actor_context* _actor_query(uint32_t handle);


static int
_actor_progress_push(struct hive_actor_context* actor) {
    uint32_t tail = __sync_fetch_and_add(&ACTOR_MGR.tail, 1);
    assert(GP(tail+1) != GP(ACTOR_MGR.head));
    tail = GP(tail);
    actor->is_progress = true;
    ACTOR_MGR.progress[tail] = actor;
    __sync_synchronize();
    ACTOR_MGR.flags[tail] = true;
    return 0;
}


static struct hive_actor_context*
_actor_progress_pop() {
    uint32_t old_head = ACTOR_MGR.head;
    uint32_t head = GP(ACTOR_MGR.head);
    if(GP(ACTOR_MGR.tail) == head) {
        return NULL;
    }

    if(!ACTOR_MGR.flags[head]) {
        return NULL;
    }

    __sync_synchronize();
    struct hive_actor_context* actor = ACTOR_MGR.progress[head];
    if(!__sync_bool_compare_and_swap(&ACTOR_MGR.head, old_head, old_head+1)) {
        return NULL;
    }

    ACTOR_MGR.flags[head] = false;
    return actor;
}


void
hive_actor_init() {
    ACTOR_MGR.head = 0;
    ACTOR_MGR.tail = 0;

    size_t sz = sizeof(struct hive_actor_context*)*DEFAULT_ACTOR_CAP;
    ACTORS.list = (struct hive_actor_context**)hive_malloc(sz);
    memset(ACTORS.list, 0, sz);
    ACTORS.size = DEFAULT_ACTOR_CAP;
    ACTORS.handle_index = 1;
    rwlock_init(&ACTORS.lock);
}


uint32_t
hive_actor_new(char* name, hive_actor_cb cb) {
    actors_wlock();
    for(;;) {
        uint32_t i=0;
        for(i=0; i<ACTORS.size; i++) {
            uint32_t handle = i+ACTORS.handle_index;
            uint32_t hash = hande2hash(handle);
            if(ACTORS.list[hash] == NULL) {
                struct hive_actor_context* actor = _actor_new(name, handle, cb);
                ACTORS.list[hash] = actor;
                ACTORS.handle_index = handle+1;
                actors_wunlock();

                return handle;
            }
        }

        // need expand actors list
        assert(ACTORS.size*2 < 0x8000000);
        size_t sz = sizeof(struct hive_actor_context*)*ACTORS.size*2;
        struct hive_actor_context** new_list = (struct hive_actor_context**)hive_malloc(sz);
        memset(new_list, 0, sz);
        for(i=0; i<ACTORS.size; i++) {
            struct hive_actor_context* v = ACTORS.list[i];
            uint32_t handle = v->handle;
            uint32_t hash = hande2hash(handle);
            assert(new_list[hash] == NULL);
            new_list[hash] = v;
        }

        hive_free(ACTORS.list);
        ACTORS.size *= 2;
        ACTORS.list = new_list;
    }
}


int
hive_actor_free(uint32_t handle) {
    int ret = 0;
    struct hive_actor_context* actor = NULL;
    actors_rlock();
    actor = _actor_query(handle);
    if (!actor) {
        ret = -1; // invalid actor handle
    } else {
        actor->is_release = true;
    }
    actors_runlock();
    return ret;    
}



int
hive_actor_send(uint32_t source, uint32_t target, int type, void* data, size_t size) {
    int ret = 0;
    actors_rlock();
    struct hive_actor_context* src_actor = _actor_query(source);
    struct hive_actor_context* dst_actor = _actor_query(target);
    if (src_actor == NULL || dst_actor == NULL) {
        ret = -1;
    } else {
        struct hive_message msg = {
            .source = source,
            .type = type,
            .size = size,
            .data = (unsigned char*)data,
        };
       hive_mq_push(dst_actor->q, &msg);
    }
    actors_runlock();
    return ret;
}



static struct hive_actor_context*
_actor_query(uint32_t handle) {
    struct hive_actor_context* ret = NULL;
    actors_rlock();

    uint32_t hash = hande2hash(handle);
    struct hive_actor_context* actor = ACTORS.list[hash];
    if(actor && actor->handle == handle) {
        ret = actor;
    }

    actors_runlock();
    return ret;
}


static struct hive_actor_context*
_actor_new(char* name, uint32_t handle, hive_actor_cb cb) {
    struct hive_actor_context* actor = (struct hive_actor_context*)hive_malloc(sizeof(struct hive_actor_context));
    actor->q = hive_mq_new();
    actor->next = NULL;
    actor->prev = NULL;
    actor->cb = cb;

    char* p = NULL;
    if(name) {
        size_t len = strlen(name);
        p = hive_malloc(len+1);
        p[len] = 0;
        strcpy(p, name);
    }
    actor->name = p;
    return actor;
}


static void
_actor_free(struct hive_actor_context* actor) {
    if(actor) {
        hive_mq_free(actor->q);
        if(actor->name) {
            hive_free(actor->q);
        }
        hive_free(actor);
    }    
}

