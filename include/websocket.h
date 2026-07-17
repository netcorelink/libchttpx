/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "crosspltm.h"
#include "request.h"
#include "serv.h"

#include <stddef.h>
#include <stdint.h>

#define CHTTPX_WSOCKET_OPCODE_CONTINUATION 0x0
#define CHTTPX_WSOCKET_OPCODE_TEXT 0x1
#define CHTTPX_WSOCKET_OPCODE_BINARY 0x2
#define CHTTPX_WSOCKET_OPCODE_CLOSE 0x8
#define CHTTPX_WSOCKET_OPCODE_PING 0x9
#define CHTTPX_WSOCKET_OPCODE_PONG 0xA

    struct chttpx_wsocket
    {
        chttpx_socket_t socket;
        int connected;
        /** Full request path, e.g. /api/v1/ws/chat/lobby-42 */
        char path[MAX_PATH];
        chttpx_param_t params[MAX_PARAMS];
        size_t params_count;
        void* userdata;
    };

    typedef struct chttpx_wsocket chttpx_wsocket_t;

    typedef void (*chttpx_wsocket_on_open_t)(chttpx_wsocket_t* ws, void* userdata);
    typedef void (*chttpx_wsocket_on_message_t)(chttpx_wsocket_t* ws, const unsigned char* data, size_t len,
                                                int opcode, void* userdata);
    typedef void (*chttpx_wsocket_on_close_t)(chttpx_wsocket_t* ws, void* userdata);

    typedef struct
    {
        chttpx_wsocket_on_open_t on_open;
        chttpx_wsocket_on_message_t on_message;
        chttpx_wsocket_on_close_t on_close;
        void* userdata;
    } chttpx_wsocket_callbacks_t;

    /**
     * Register a WebSocket route with event callbacks (non-blocking, shared poll thread).
     */
    void cHTTPX_WSocketRegisterRoute(chttpx_router_t* r, const char* path, const chttpx_wsocket_callbacks_t* callbacks);

    /** Send a text frame to one client. */
    int cHTTPX_WSocketSend(chttpx_wsocket_t* ws, const char* text);

    /** Send a binary frame to one client. */
    int cHTTPX_WSocketSendBinary(chttpx_wsocket_t* ws, const unsigned char* data, size_t len);

    /** Route param captured at connect time (e.g. room_id from /ws/chat/{room_id}). */
    const char* cHTTPX_WSocketParam(chttpx_wsocket_t* ws, const char* name);

    /** Broadcast text to all clients on the exact same path (same room URL). */
    int cHTTPX_WSocketBroadcast(const char* path, const char* text);

    /** Broadcast text to everyone in the sender's room (same path as ws). */
    int cHTTPX_WSocketBroadcastPeers(chttpx_wsocket_t* ws, const char* text);

    /** Broadcast text to all clients whose route param matches (e.g. room_id = "42"). */
    int cHTTPX_WSocketBroadcastRoom(const char* param_name, const char* param_value, const char* text);

    /** Stop the WebSocket engine and close all connections. */
    void cHTTPX_WSocketShutdown(void);

    /** Internal: handle upgrade request. Returns 1 if WebSocket took ownership of the socket. */
    int cHTTPX_WSocketTryHandle(chttpx_request_t* req);

#ifdef __cplusplus
}
#endif

#endif
