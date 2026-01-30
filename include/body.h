/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef BODY_H
#define BODY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "request.h"

#define MAX_BODY_IN_MEMORY 1048576 // 1 MB 

/* Parse body in request */
void _parse_req_body(chttpx_request_t* req, chttpx_socket_t client_fd, char* buffer, size_t buffer_len);

#ifdef __cplusplus
extern }
#endif

#endif