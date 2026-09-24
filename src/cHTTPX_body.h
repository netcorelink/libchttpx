/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef BODY_H
#define BODY_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cHTTPX_request.h"

#define MAX_BODY_IN_MEMORY 1048576 /* 1 MB */

/**
 * Read and decode the HTTP request body (Content-Length or chunked).
 *
 * Populates req->body or req->_multipart_stream according to content type and
 * server size limits. Sets req->_parse_status on framing or I/O errors.
 *
 * @param req Current HTTP request.
 * @param client_fd Connected client socket.
 * @param buffer Initial receive buffer containing the request headers.
 * @param buffer_len Size of buffer in bytes.
 */
void _parse_req_body(chttpx_request_t* req, chttpx_socket_t client_fd, char* buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif
