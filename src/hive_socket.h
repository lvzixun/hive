#ifndef _HIVE_SOCKET_H_
#define _HIVE_SOCKET_H_

#include <unistd.h>
#include <stdint.h>
#include <netdb.h>

enum socket_event {
    SE_CONNECTED,
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

struct socket_addrinfo {
    char ip[NI_MAXHOST];
    int port;
};

int hive_socket_connect(const char* host, uint16_t port, uint32_t actor_handle, char const** out_error);
int hive_socket_listen(const char* host, uint16_t port, uint32_t actor_handle);
int hive_socket_send(int id, const void* data, size_t size);
int hive_socket_addrinfo(int id, struct socket_addrinfo* out_addrinfo, const char** out_error);
int hive_socket_attach(int id, uint32_t actor_handle);
int hive_socket_close(int id);

#endif