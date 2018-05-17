#ifndef _SERVERGATE_H_
#define _SERVERGATE_H_

#include <stdint.h>

struct servergate_context;

struct servergate_context* servergate_create();
void servergate_free(struct servergate_context* context);
void servergate_add(struct servergate_context* context, int id, uint8_t* data, size_t size);
void servergate_del(struct servergate_context* context, int id);

typedef void(* servergate_msg)(int id, uint8_t* data, size_t sz);
void servergate_cb(struct servergate_context* context, servergate_msg msg_cb);

#endif