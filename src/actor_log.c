#include "hive.h"
#include "actor_log.h"
#include "hive_memory.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

struct {
    FILE* fp;
    uint32_t handle;
}_ENV;



const char* log_fmt[] = {
    [HIVE_LOG_DBG] = "[DBG] %s\n",
    [HIVE_LOG_INF] = "[INF] %s\n",
    [HIVE_LOG_ERR] = "\x1b[1;31m[ERR] %s\x1b[0m\n",
};


static void _actor_log_release();

static void 
_actor_log_dispatch(uint32_t source, uint32_t self, int type, int session, void* data, size_t sz, void* ud) {
    switch(type) {
        case HIVE_TRELEASE:{
                _actor_log_release();
            } break;

        case HIVE_TNORMAL: {
                enum hive_log_level level = (enum hive_log_level)session;
                assert(level>=HIVE_LOG_DBG && level<=HIVE_LOG_ERR);
                const char* level_fmt = log_fmt[level];
                if(sz > 0) {
                    fprintf(_ENV.fp, level_fmt, (const char*)data);
                }
            } break;
    }
}


static void
_actor_log_release() {
    if(_ENV.fp != stdout) {
        fclose(_ENV.fp);
    }
}


void 
actor_log_init(const char* file_path) {
    FILE* fp = NULL;
    if(file_path) {
        fp = fopen(file_path, "rb");
    }

    _ENV.fp = (fp)?(fp):(stdout);
    _ENV.handle = hive_register("hive_log", _actor_log_dispatch, NULL, NULL, 0);
    assert(_ENV.handle > 0);
}


void 
actor_log_send(uint32_t source, enum hive_log_level level, const char* msg) {
    uint32_t handle = _ENV.handle;
    hive_send(source, handle, HIVE_TNORMAL, (int)level, (void*)msg, strlen(msg)+1);
}

