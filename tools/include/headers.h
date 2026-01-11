/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef HEADERS_H
#define HEADERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "include/request.h"

/**
 * Get a request header by name.
 * @param req Pointer to the HTTP request.
 * @param name Header name (case-insensitive).
 * @return Pointer to header value if found, otherwise NULL.
 */
const char* cHTTPX_Header(chttpx_request_t *req, const char *name);

void _parse_req_headers(chttpx_request_t *req, char *buffer, size_t buffer_len);

#ifdef __cplusplus
extern }
#endif

#endif