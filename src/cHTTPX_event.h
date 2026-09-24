#ifndef CHTTPX_EVENT_H
#define CHTTPX_EVENT_H

#include "cHTTPX_crosspltm.h"

#include <stddef.h>
#include <stdint.h>

#define CHTTPX_EVENT_READ 0x01u
#define CHTTPX_EVENT_WRITE 0x02u
#define CHTTPX_EVENT_ERROR 0x04u
#define CHTTPX_EVENT_WAKE 0x08u

/**
 * Opaque platform event multiplexer state.
 */
typedef struct chttpx_event_loop chttpx_event_loop_t;

/**
 * One ready socket and its registered interest flags.
 */
typedef struct
{
    chttpx_socket_t fd;
    uint32_t events;
    void* data;
} chttpx_event_t;

/** Allocate a platform-specific event loop. @return Event loop or NULL on failure. */
chttpx_event_loop_t* _chttpx_event_create(void);

/** Free an event loop and registered state. @param loop Event loop to destroy. */
void _chttpx_event_destroy(chttpx_event_loop_t* loop);

/**
 * Register a socket for read/write monitoring.
 *
 * @return Zero on success or -1 on failure.
 */
int _chttpx_event_add(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data);

/**
 * Update interest flags or user data for a socket.
 *
 * @return Zero on success or -1 on failure.
 */
int _chttpx_event_mod(chttpx_event_loop_t* loop, chttpx_socket_t fd, uint32_t events, void* data);

/** Remove a socket from the event loop. */
void _chttpx_event_del(chttpx_event_loop_t* loop, chttpx_socket_t fd);

/**
 * Wait for ready sockets or a wake signal.
 *
 * @return Number of ready events, zero on timeout, or -1 on error.
 */
int _chttpx_event_wait(chttpx_event_loop_t* loop, chttpx_event_t* events, size_t max_events, int timeout_ms);

/** Interrupt a blocking event wait. */
void _chttpx_event_wake(chttpx_event_loop_t* loop);

/** Set a socket to non-blocking mode. @return Zero on success or -1 on failure. */
int _chttpx_socket_set_nonblocking(chttpx_socket_t fd);

#endif
