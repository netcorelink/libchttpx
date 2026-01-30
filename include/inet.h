/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef INET_H
#define INET_H

#ifdef __cplusplus
extern "C" {
#endif

#include "crosspltm.h"

/**
 * Get client IP address from the underlying socket connection.
 *
 * This function retrieves the real network-level IP address of the client
 * using the TCP socket (`getpeername`). It supports both IPv4 and IPv6.
 *
 * The returned value is a pointer to a static buffer, so it will be
 * overwritten on subsequent calls and is NOT thread-safe.
 *
 * This IP cannot be spoofed by HTTP headers, but if the server is behind
 * a reverse proxy (Nginx, CDN, load balancer), the returned address will
 * be the proxy’s IP instead of the original client.
 *
 * @param client_fd Connected client socket file descriptor.
 * @return Pointer to a string with the client IP address,
 *         or "-" if the address cannot be determined.
 */
const char* cHTTPX_ClientInetIP(chttpx_socket_t client_fd);

#ifdef __cplusplus
}
#endif

#endif
