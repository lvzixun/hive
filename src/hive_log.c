#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>


// #define format2buffer(f) va_list args; \
//     va_start(args, f); \
//     char buffer[256] = {0}; \
//     vsnprintf(buffer, sizeof(buffer)-1, f, args); 

void
hive_elog(const char* tag, const char* f, ...) {
    tag = (tag == NULL)?(""):(tag);
    fprintf(stderr, "![%s]: ", tag);
    va_list args;
    va_start(args, f);
    vfprintf(stderr, f, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}

void
hive_printf(const char* f, ...) {
    va_list args;
    va_start(args, f);
    vfprintf(stdout, f, args);
    fprintf(stdout, "\n");
    fflush(stdout);
}


void
hive_panic(const char* f, ...) {
    va_list args;
    va_start(args, f);
    vfprintf(stderr, f, args);
    fprintf(stderr, "\n");
    fflush(stderr);
    exit(1);
}
