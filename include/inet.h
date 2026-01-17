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

#include <netinet/in.h>

const char* cHTTPX_ClientInetIP(int client_fd);

#ifdef __cplusplus
}
#endif

#endif