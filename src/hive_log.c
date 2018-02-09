#include <stdarg.h>
#include <stdio.h>


#define format2buffer(f) va_list args; \
    va_start(args, f); \
    char buffer[256] = {0}; \
    vsnprintf(buffer, sizeof(buffer)-1, f, args); 

void
hive_elog(const char* tag, const char* f, ...) {
    format2buffer(f);
    tag = (tag == NULL)?(""):(tag);
    fprintf(stderr, "![%s]: %s\n", tag, buffer);
}

void
hive_printf(const char* f, ...) {
    format2buffer(f);
    fprintf(stdout, "%s\n", buffer);
}


void
hive_panic(const char* f, ...) {
    format2buffer(f);
    fprintf(stderr, "[hive panic] %s\n", buffer);
}
