/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#include "serv.h"

#include <stdlib.h>

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