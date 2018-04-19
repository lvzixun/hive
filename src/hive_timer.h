#ifndef _HIVE_TIMER_H_
#define _HIVE_TIMER_H_

#include <stddef.h>
#include <stdint.h>

struct timer_state;

struct timer_state* hive_timer_create();
void hive_timer_free(struct timer_state* state);
void hive_timer_update(struct timer_state* state);
int hive_timer_insert(struct timer_state* state, uint32_t offset, uint32_t handle);

#endif