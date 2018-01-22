#ifndef _ACTOR_BOOTSTRAP_H_
#define _ACTOR_BOOTSTRAP_H_

#include <stddef.h>
#include <stdint.h>


void actor_bootstrap_setpath(const char* path);
void actor_bootstrap_dispatch(uint32_t source, uint32_t self, int type, int session, void* data, size_t sz, void* ud);


#endif