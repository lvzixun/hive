#ifndef _HIVE_LOG_H_
#define _HIVE_LOG_H_


void hive_elog(const char* tag, const char* f, ...);
void hive_printf(const char* f, ...);
void hive_panic(const char* f, ...);

#endif