#ifndef _ACTOR_LOG_H_
#define _ACTOR_LOG_H_


enum hive_log_level {
    HIVE_LOG_DBG,
    HIVE_LOG_INF,
    HIVE_LOG_ERR,
};

void actor_log_init(const char* file_path);
void actor_log_send(uint32_t source, enum hive_log_level level, const char* msg);

#endif