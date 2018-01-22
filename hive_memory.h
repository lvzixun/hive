#ifndef _HIVE_MEMORY_H_
#define _HIVE_MEMORY_H_


#ifdef DEBUG_MEMORY
    #include <stddef.h>
    void*   hive_memory_malloc(size_t size, const char* file, int line);
    void*   hive_memory_calloc(size_t count, size_t size, const char* file, int line);
    void*   hive_memory_realloc(void* p, size_t size, const char* file, int line);
    void    hive_memory_free(void* p);
    void    hive_memroy_dump();

    #define hive_malloc(size)   hive_memory_malloc(size, __FILE__, __LINE__)
    #define hive_calloc(count, size)    hive_memory_calloc(count, size, __FILE__, __LINE__)
    #define hive_realloc(p, size)   hive_memory_realloc(p, size, __FILE__, __LINE__)
    #define hive_free(p)    hive_memory_free(p)
    #define hive_memdump()  hive_memroy_dump()

#else
    #include <stdlib.h>
    #define hive_malloc   malloc
    #define hive_calloc   calloc
    #define hive_realloc  realloc
    #define hive_free     free
    #define hive_memdump()
#endif


#endif