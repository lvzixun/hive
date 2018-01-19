#ifndef _HIVE_ACTOR_H_
#define _HIVE_ACTOR_H_

#include <stddef.h>
#include <stdint.h>

struct hive_actor_context;

typedef void (*hive_actor_cb)(
    struct hive_actor_context* ctx, uint32_t source, int type, void* data, size_t sz);

#endif