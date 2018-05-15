#ifndef _RINGBUFFER_H_
#define _RINGBUFFER_H_

#include <stdint.h>

struct ringbuffer_context;

struct ringbuffer_context* ringbuffer_create();
void ringbuffer_free(struct ringbuffer_context* ringbuffer);

void ringbuffer_add(struct ringbuffer_context* ringbuffer, int id, uint8_t* data, int sz);

typedef void(* ringbuffer_triggre_close)(int id);
typedef void(* ringbuffer_triggre_package)(int id, uint8_t* data, size_t sz);
void ringbuffer_cb(struct ringbuffer_context* ringbuffer, ringbuffer_triggre_close close_cb, ringbuffer_triggre_package package_cb);

#endif