#include "hive.h"
#include "hive_socket.h"
#include "hive_memory.h"
#include "hive_log.h"

#include "actor_gate/ringbuffer.h"

struct actor_gate {
    struct ringbuffer_context* ringbuffer;
    uint32_t acotr_handle;    
};


void actor_gate_init() {
    // todo it!!
}

