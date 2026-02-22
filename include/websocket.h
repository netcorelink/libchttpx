/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#include "serv.h"

#include <stdlib.h>

#define CHTTPX_WSOCKET_OPCODE_CONTINUATION 0x0
#define CHTTPX_WSOCKET_OPCODE_TEXT 0x1
#define CHTTPX_WSOCKET_OPCODE_BINARY 0x2
#define CHTTPX_WSOCKET_OPCODE_CLOSE 0x8
#define CHTTPX_WSOCKET_OPCODE_PING 0x9
#define CHTTPX_WSOCKET_OPCODE_PONG 0xA

typedef struct {
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

typedef struct {
    int socket;
    int connected;
} chttpx_wsocket_t;

typedef void (*chttpx_wsocket_handler_t)(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

typedef void (*chttpx_wsocket_route_t)(chttpx_wsocket_t* wsocket);

void cHTTPX_WSocketRegisterRoute(chttpx_router_t* r, const char* path, chttpx_wsocket_route_t handler);

int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key);

int cHTTPX_WSocketSend(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

int cHTTPX_WSocketRecv(chttpx_wsocket_t* wsocket, unsigned char* buffer, size_t len);