/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#include "cHTTPX_serv.h"

#include <stdlib.h>

#define CHTTPX_WSOCKET_OPCODE_CONTINUATION 0x0
#define CHTTPX_WSOCKET_OPCODE_TEXT 0x1
#define CHTTPX_WSOCKET_OPCODE_BINARY 0x2
#define CHTTPX_WSOCKET_OPCODE_CLOSE 0x8
#define CHTTPX_WSOCKET_OPCODE_PING 0x9
#define CHTTPX_WSOCKET_OPCODE_PONG 0xA

/** Parsed WebSocket frame header and payload view. */
typedef struct
{
    /* FIN - final fragment
     * 1 eq. this is the last frame of the message
     * 0 eq. the message is divided into several parts
     */
    int fin;
    /* Check define CHTTPX_WSOCKET_OPCODE */
    int opcode;
    int masked;
    /* Length data in payload */
    uint64_t payload_len;
    /* Mask for XOR payload */
    unsigned char mask[4];
    /* Data in socket */
    unsigned char* payload;
} wsocket_frame_t;

/** Connected WebSocket client handle. */
typedef struct
{
    int socket;
    int connected;
} chttpx_wsocket_t;

/** Callback invoked when a WebSocket message payload is received. */
typedef void (*chttpx_wsocket_handler_t)(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

/** Route handler invoked after a successful WebSocket upgrade. */
typedef void (*chttpx_wsocket_route_t)(chttpx_wsocket_t* wsocket);

/**
 * Register a WebSocket route on a router.
 *
 * @param r Router that owns the route.
 * @param path URL path template.
 * @param handler Handler invoked after upgrade.
 */
void cHTTPX_WSocketRegisterRoute(chttpx_router_t* r, const char* path, chttpx_wsocket_route_t handler);

/**
 * Perform the HTTP Upgrade handshake for WebSocket (legacy API).
 *
 * @param client_socket Connected client socket.
 * @param sec_wsocket_key Sec-WebSocket-Key header value.
 * @return cHTTPX_OK on success or a negative error code.
 */
int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key);

/**
 * Send one WebSocket frame payload to the client.
 *
 * @param wsocket Connected WebSocket handle.
 * @param data Payload bytes.
 * @param len Payload length in bytes.
 * @return cHTTPX_OK on success or a negative error code.
 */
int cHTTPX_WSocketSend(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

/**
 * Receive WebSocket payload bytes into a caller buffer.
 *
 * @param wsocket Connected WebSocket handle.
 * @param buffer Output buffer.
 * @param len Maximum bytes to read.
 * @return Number of bytes read or a negative error code.
 */
int cHTTPX_WSocketRecv(chttpx_wsocket_t* wsocket, unsigned char* buffer, size_t len);
