#ifndef _HIVE_SOCKET_H_
#define _HIVE_SOCKET_H_

#include <unistd.h>
#include <stdint.h>

enum socket_event {
    SE_BREAK,
    SE_ACCEPT,
    SE_RECIVE,
    SE_ERROR,
};


struct socket_data {
    enum socket_event se;
    union {
        size_t size;
        int id;
    } u;
    uint8_t data[0];
};


int hive_socket_connect(const char* host, uint16_t port, uint32_t actor_handle);
int hive_socket_listen(const char* host, uint16_t port, uint32_t actor_handle);
int hive_socket_send(int id, const void* data, size_t size);
int hive_socket_close(int id);

#endif