#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/time.h>

#include "spinlock.h"
#include "hive_memory.h"
#include "hive.h"
#include "actor_log.h"


struct user_data {
    int session;
    uint32_t handle;
};

struct timer_node {
    uint32_t expire;
    struct user_data data;
    struct timer_node* next;
};

struct timer_list {
    struct timer_node* head;
    struct timer_node* tail;
};

#define NEAR_SHIFT 8
#define NEAR       (1<<NEAR_SHIFT)
#define NEAR_MASK  (NEAR-1)

#define LEVEL_SHITF 6
#define LEVEL       (1<<LEVEL_SHITF)
#define LEVEL_MASK  (LEVEL-1)


struct timer_state {
    struct spinlock lock;
    struct timer_list near_wheel[NEAR];
    struct timer_list level_wheel[4][LEVEL];
    uint32_t cur_time;
    uint64_t last_real_time;
    int session;
};


static uint64_t
_gettime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t t_ms = (uint64_t)(tv.tv_sec * 1000);
    t_ms += tv.tv_usec / 1000;
    return t_ms / 10;  // 10 ms
}


struct timer_state *
hive_timer_create() {
    struct timer_state* ret = (struct timer_state*)hive_malloc(sizeof(*ret));
    memset(ret, 0, sizeof(*ret));
    ret->cur_time = 0;
    ret->session = 0;
    ret->last_real_time = _gettime();
    spinlock_init(&ret->lock);
    return ret;
}

static void
_clear_list(struct timer_list* list) {
    struct timer_node* p = list->head;
    while(p) {
        struct timer_node* next = p->next;
        hive_free(p);
        p = next;
    }
}

void
hive_timer_free(struct timer_state* state) {
    int i=0;
    for(i=0; i<NEAR; i++) {
        _clear_list(&state->near_wheel[i]);
    }
    int j=0;
    for(i=0; i<4; i++) {
        for(j=0; j<LEVEL; j++) {
            _clear_list(&state->level_wheel[i][j]);
        }
    }

    hive_free(state);
}

static struct timer_node *
_node_new(struct timer_state* state, uint32_t offset, int session, uint32_t handle) {
    struct timer_node* node = (struct timer_node*)hive_malloc(sizeof(*node));
    node->next = NULL;
    node->expire = state->cur_time + offset;
    node->data.session = session;
    node->data.handle = handle;
    return node;
}

static void
_node_free(struct timer_node* node) {
    hive_free(node);
}

static void
_list_append(struct timer_list* list, struct timer_node* node) {
    if(list->tail == NULL) {
        list->head = node;
        list->tail = node;
    }else {
        list->tail->next = node;
        list->tail = node;
    }
}


static void
_hive_timer_add(struct timer_state* state, struct timer_node* node) {
    uint32_t expire = node->expire;
    uint32_t cur_time = state->cur_time;

    if((expire | NEAR_MASK) == (cur_time | NEAR_MASK)) {
        uint32_t idx = expire & NEAR_MASK;
        _list_append(&state->near_wheel[idx], node);
    } else {
        uint32_t value = expire >> NEAR_SHIFT;
        uint32_t time = cur_time >> NEAR_SHIFT;
        int i=0;
        for(i=0; i<3; i++) {
            if((value | LEVEL_MASK) == (time | LEVEL_MASK)) {
                break;
            }
            value = value >> LEVEL_SHITF;
            time = time >> LEVEL_SHITF;
        }
        uint32_t level = value & LEVEL_MASK;
        _list_append(&state->level_wheel[i][level], node);
    }
}

static void
_timer_dispatch(struct timer_state* state, struct timer_node* node) {
    uint32_t cur_time = state->cur_time;
    int session = node->data.session;
    uint32_t handle = node->data.handle;
    assert(cur_time == node->expire);
    hive_send(SYS_HANDLE, handle, HIVE_TTIMER, session, NULL, 0);
}


static void
_hive_timer_exec(struct timer_state* state) {
    uint32_t cur_time = state->cur_time;
    int idx = cur_time & NEAR_MASK;

    struct timer_list* list = &state->near_wheel[idx];
    struct timer_node* node = list->head;

    while(node) {
        list->tail = NULL;
        list->head = NULL;

        spinlock_unlock(&state->lock);
        while(node) {
            _timer_dispatch(state, node);
            struct timer_node* next = node->next;
            _node_free(node);
            node = next;
        }
        spinlock_lock(&state->lock);
        node = list->head;
    }
}

static void
_timer_move(struct timer_state* state, int level, int idx) {
    struct timer_list* list = &state->level_wheel[level][idx];
    struct timer_node* head = list->head;
    list->head = NULL;
    list->tail = NULL;
    while(head) {
        struct timer_node* next = head->next;
        _hive_timer_add(state, head);
        head = next;
    }
}


static void
_hive_timer_shift(struct timer_state* state) {
    uint32_t time = ++state->cur_time;
    if(time == 0) {
        _timer_move(state, 3, 0);
    } else {
        int i=0;
        uint32_t ct = time >> NEAR_SHIFT;
        uint32_t mask = NEAR_SHIFT;
        while((time & (mask-1)) == 0) {
            int idx = ct & LEVEL_MASK;
            if(idx != 0) {
                _timer_move(state, i, idx);
                break;
            }
            i++;
            ct = ct >> LEVEL_SHITF;
            mask = mask << LEVEL_SHITF;
        }
    }
}




static void
_timer_update(struct timer_state* state) {
    spinlock_lock(&state->lock);
    _hive_timer_exec(state);
    _hive_timer_shift(state);
    spinlock_unlock(&state->lock);
}

void 
hive_timer_update(struct timer_state* state) {
    uint64_t cur_real_time = _gettime();
    uint64_t last_real_time = state->last_real_time;
    state->last_real_time = cur_real_time;
    if(cur_real_time < last_real_time) {
        actor_log_send(SYS_HANDLE, HIVE_LOG_ERR, "invalid timer diff");
    }else {
        uint64_t diff = cur_real_time - last_real_time;
        uint64_t i=0;
        for(i=0; i<diff; i++) {
            _timer_update(state);
        }
    }
}


int
hive_timer_insert(struct timer_state* state, uint32_t offset, uint32_t handle) {
    spinlock_lock(&state->lock);
    int session = state->session++;
    struct timer_node* node = _node_new(state, offset, session, handle);
    _hive_timer_add(state, node);
    spinlock_unlock(&state->lock);
    return session;
}


