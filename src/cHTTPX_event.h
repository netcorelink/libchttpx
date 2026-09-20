#ifndef CHTTPX_EVENT_H
#define CHTTPX_EVENT_H

#include "cHTTPX_crosspltm.h"

#include <stddef.h>
#include <stdint.h>

#define CHTTPX_EVENT_READ 0x01u
#define CHTTPX_EVENT_WRITE 0x02u
#define CHTTPX_EVENT_ERROR 0x04u
#define CHTTPX_EVENT_WAKE 0x08u

typedef struct chttpx_event_loop chttpx_event_loop_t;

typedef struct
{
    chttpx_socket_t fd;
    uint32_t events;
    void* data;
} chttpx_event_t;

chttpx_event_loop_t* _chttpx_event_create(void);
void _chttpx_event_destroy(chttpx_event_loop_t* loop);
int _chttpx_event_add(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data);
int _chttpx_event_mod(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data);
void _chttpx_event_del(chttpx_event_loop_t* loop, chttpx_socket_t fd);
int _chttpx_event_wait(chttpx_event_loop_t* loop, chttpx_event_t* events, size_t max_events, int timeout_ms);
void _chttpx_event_wake(chttpx_event_loop_t* loop);
int _chttpx_socket_set_nonblocking(chttpx_socket_t fd);

#endif
