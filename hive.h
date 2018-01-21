#ifndef _HIVE_H_
#define _HIVE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYS_HANDLE 0

#define HIVE_TCREATE 0
#define HIVE_TRELEASE 1
#define HIVE_TTIMER 2
#define HIVE_TSOCKET 3


void hive_init();
int hive_start();
void hive_exit();


typedef void (*hive_actor_cb)(
    uint32_t source, uint32_t self, int type, int session, void* data, size_t sz);
bool hive_register(char* name, hive_actor_cb cb);
bool hive_unregister(uint32_t handle);

bool hive_send(uint32_t source, uint32_t target, int type, int session, void* data, size_t size);

#endif