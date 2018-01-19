#ifndef _HIVE_MQ_H_
#define _HIVE_MQ_H_

#include <stddef.h>
#include <stdint.h>

struct hive_message {
    uint32_t source;
    int type;
    size_t size;
    unsigned char* data;
};

struct hive_message_queue;

struct hive_message_queue* hive_mq_new();
void hive_mq_free(struct hive_message_queue* q);
void hive_mq_push(struct hive_message_queue* q, struct hive_message* msg);
int hive_mq_pop(struct hive_message_queue* q, struct hive_message* out_msg);

#endif