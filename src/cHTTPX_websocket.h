/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license.
 */

#ifndef CHTTPX_WEBSOCKET_H
#define CHTTPX_WEBSOCKET_H

#include "cHTTPX_serv.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CHTTPX_WSOCKET_OPCODE_CONTINUATION 0x0
#define CHTTPX_WSOCKET_OPCODE_TEXT 0x1
#define CHTTPX_WSOCKET_OPCODE_BINARY 0x2
#define CHTTPX_WSOCKET_OPCODE_CLOSE 0x8
#define CHTTPX_WSOCKET_OPCODE_PING 0x9
#define CHTTPX_WSOCKET_OPCODE_PONG 0xA

typedef struct
{
    int fin;
    int opcode;
    int masked;
    uint64_t payload_len;
    unsigned char mask[4];
    unsigned char* payload;
} wsocket_frame_t;

typedef struct chttpx_wsocket
{
    chttpx_socket_t socket;
    int connected;
    int opcode;
    chttpx_request_t* request;
    void* user_data;
    void* _internal;
} chttpx_wsocket_t;

typedef void (*chttpx_wsocket_handler_t)(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);
typedef void (*chttpx_wsocket_close_handler_t)(chttpx_wsocket_t* wsocket, uint16_t code, const unsigned char* reason, size_t len);
typedef void (*chttpx_wsocket_route_t)(chttpx_wsocket_t* wsocket);

void cHTTPX_WSocketRegisterRoute(chttpx_router_t* router, const char* path, chttpx_wsocket_route_t handler);
void cHTTPX_WSocketOnMessage(chttpx_wsocket_t* wsocket, chttpx_wsocket_handler_t handler);
void cHTTPX_WSocketOnClose(chttpx_wsocket_t* wsocket, chttpx_wsocket_close_handler_t handler);
void cHTTPX_WSocketSetData(chttpx_wsocket_t* wsocket, void* user_data);
void* cHTTPX_WSocketData(chttpx_wsocket_t* wsocket);

int cHTTPX_WSocketSend(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);
int cHTTPX_WSocketSendBinary(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);
int cHTTPX_WSocketPing(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);
int cHTTPX_WSocketClose(chttpx_wsocket_t* wsocket, uint16_t code, const char* reason);

/* Legacy HTTP/1.1/synchronous APIs retained for source compatibility. */
int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key);
int cHTTPX_WSocketRecv(chttpx_wsocket_t* wsocket, unsigned char* buffer, size_t len);

/* Internal HTTP/2 bridge. */
typedef int (*chttpx_wsocket_transport_send_fn)(void* context, const unsigned char* data, size_t len, bool end_stream);

chttpx_wsocket_route_t _chttpx_websocket_find_route(chttpx_serv_t* server, const char* path);
chttpx_wsocket_t* _chttpx_websocket_create(chttpx_serv_t* server, chttpx_socket_t socket, chttpx_request_t* request,
                                           chttpx_wsocket_transport_send_fn transport_send, void* transport_context);
void _chttpx_websocket_mark_connected(chttpx_wsocket_t* wsocket);
int _chttpx_websocket_feed(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);
void _chttpx_websocket_destroy(chttpx_wsocket_t* wsocket);
void _chttpx_websocket_server_cleanup(chttpx_serv_t* server);

#ifdef __cplusplus
}
#endif

#endif
