#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "hive_memory.h"
#include "actor_gate/servergate.h"

static void
_msg_cb(int id, uint8_t* data, size_t sz) {
    size_t i=0;
    printf("package id:%d sz:%zu data:", id, sz);
    for(i=0; i<sz; i++) {
        printf(" %d", data[i]);
    }
    printf("\n");
    hive_free(data);
}

int 
main(int argc, char const *argv[]) {
    struct servergate_context* context = servergate_create();
    servergate_cb(context, _msg_cb);

    uint8_t tmp1[] = {0x00, 0x05, 11, 12, 13, 14, 15};
    servergate_add(context, 1, tmp1, sizeof(tmp1));

    uint8_t tmp2[] = {0x00, 0x04, 21, 22, 23, 24, 0x00};
    servergate_add(context, 1, tmp2, sizeof(tmp2));

    uint8_t tmp3[] = {0x02, 31, 32, 0x00, 0x03, 41, 42, 43};
    servergate_add(context, 1, tmp3, sizeof(tmp3));

    uint8_t tmp4[] = {0x00, 0x02, 51, 52, 0x00, 0x03, 61, 62, 63};
    servergate_add(context, 1, tmp4, sizeof(tmp4));

    uint8_t tmp5[] = {0x00, 0x09, 71, 72, 73, 74};
    servergate_add(context, 1, tmp5, sizeof(tmp5));

    uint8_t tmp6[] = {75, 76, 77, 78, 79};
    servergate_add(context, 1, tmp6, sizeof(tmp6));

    uint8_t tmp7[] = {0x00, 0x20, 81, 82, 83, 84, 85, 86};
    servergate_add(context, 1, tmp7, sizeof(tmp7));

    uint8_t tmp8[] = {0x00, 0x06, 91, 92, 93, 94, 95};
    servergate_add(context, 2, tmp8, sizeof(tmp8));

    uint8_t tmp9[] = {96};
    servergate_add(context, 2, tmp9, sizeof(tmp9));

    servergate_free(context);
    hive_memroy_dump();
    return 0;
}

