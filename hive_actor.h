#ifndef _HIVE_ACTOR_H_
#define _HIVE_ACTOR_H_

#include <stddef.h>
#include <stdint.h>
#include "hive.h"

struct hive_actor_context;

void hive_actor_init();
void hive_actor_free();
void hive_actor_exit();

uint32_t hive_actor_create(char* name, hive_actor_cb cb);
int hive_actor_release(uint32_t handle);

int hive_actor_send(uint32_t source, uint32_t target, int type, int session, void* data, size_t size);
int hive_actor_dispatch();

#endif