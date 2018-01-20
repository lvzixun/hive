#include <stdbool.h>
#include <assert.h>
#include "hive_memory.h"
#include "spinlock.h"
#include "hive_mq.h"


struct hive_message_queue {
    struct spinlock lock;

    size_t size;
    size_t head;
    size_t tail;
    size_t cap;
    struct hive_message* buffer;
};

#define MESSAGE_QUEUE_DEFAULT_SIZE 1024
#define queue_block(q)  spinlock_lock(&q->lock)
#define queue_unlock(q) spinlock_unlock(&q->lock)

#define queue_point(q, i) ((i)%((q)->size))

struct hive_message_queue*
hive_mq_new() {
    struct hive_message_queue* q = (struct hive_message_queue*)hive_malloc(
        sizeof(struct hive_message_queue));
    spinlock_init(&q->lock);
    q->size = MESSAGE_QUEUE_DEFAULT_SIZE;
    q->head = 0;
    q->tail = 0;
    q->cap = 0;
    q->buffer = (struct hive_message*)hive_malloc(sizeof(struct hive_message)*MESSAGE_QUEUE_DEFAULT_SIZE);
    return q;
}


void
hive_mq_free(struct hive_message_queue* q) {
    assert(q);
    hive_free(q->buffer);
    hive_free(q);
}


static void
expand_queue(struct hive_message_queue* q) {
    size_t sz = q->size*2;
    struct hive_message* new_buffer = (struct hive_message*)hive_malloc(sz);
    size_t i = 0;
    for(i=0; i<q->cap; i++) {
        size_t idx = queue_point(q, q->head+i);
        new_buffer[i] = q->buffer[idx];
    }
    hive_free(q->buffer);
    q->buffer = new_buffer;
    q->size = sz;
    q->head = 0;
    q->tail = q->cap;
}


void
hive_mq_push(struct hive_message_queue* q, struct hive_message* msg) {
    assert(msg);
    queue_block(q);
    size_t tail = q->tail;
    q->buffer[tail] = *msg;
    q->tail = queue_point(q, tail+1);
    q->cap++;

    if(q->tail == q->head) {
        expand_queue(q);
    }
    queue_unlock(q);
}


size_t
hive_mq_pop(struct hive_message_queue* q, struct hive_message* out_msg) {
    assert(out_msg);
    queue_block(q);
    if(q->cap == 0) {
        return 0;
    }

    size_t head = q->head;
    *out_msg = q->buffer[head];
    size_t cap = q->cap;
    q->cap--;
    q->head = queue_point(q, head+1);
    queue_unlock(q);
    return cap;
}


