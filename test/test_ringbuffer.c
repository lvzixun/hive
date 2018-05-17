#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#include "actor_gate/ringbuffer.h"


static void
_trigger_close(int id) {
    printf("id close!!!\n");
}

static void
_trigger_package(int id, uint8_t* data, size_t sz) {
    size_t i=0;
    printf("package id:%d sz:%zu data:", id, sz);
    for(i=0; i<sz; i++) {
        printf(" %d", data[i]);
    }
    printf("\n");
}


int main(int argc, char const *argv[]) {
    struct ringbuffer_context* ringbuffer = ringbuffer_create();

    ringbuffer_cb(ringbuffer, _trigger_close, _trigger_package);

    uint8_t tmp1[] = {0x00, 0x05, 11, 12, 13, 14, 15};
    ringbuffer_add(ringbuffer, 1, tmp1, sizeof(tmp1));

    uint8_t tmp2[] = {0x00, 0x04, 21, 22, 23, 24, 0x00};
    ringbuffer_add(ringbuffer, 1, tmp2, sizeof(tmp2));

    uint8_t tmp3[] = {0x02, 31, 32, 0x00, 0x03, 41, 42, 43};
    ringbuffer_add(ringbuffer, 1, tmp3, sizeof(tmp3));

    uint8_t tmp4[] = {0x00, 0x02, 51, 52, 0x00, 0x03, 61, 62, 63};
    ringbuffer_add(ringbuffer, 1, tmp4, sizeof(tmp4));

    uint8_t tmp5[] = {0x00, 0x09, 71, 72, 73, 74};
    ringbuffer_add(ringbuffer, 1, tmp5, sizeof(tmp5));

    uint8_t tmp6[] = {75, 76, 77, 78, 79};
    ringbuffer_add(ringbuffer, 1, tmp6, sizeof(tmp6));

    uint8_t tmp7[] = {0x00, 0x20, 81, 82, 83, 84, 85, 86};
    ringbuffer_add(ringbuffer, 1, tmp7, sizeof(tmp7));

    uint8_t tmp8[] = {0x00, 0x06, 91, 92, 93, 94, 95};
    ringbuffer_add(ringbuffer, 2, tmp8, sizeof(tmp8));

    uint8_t tmp9[] = {96};
    ringbuffer_add(ringbuffer, 2, tmp9, sizeof(tmp9));

    ringbuffer_free(ringbuffer);
    return 0;
}