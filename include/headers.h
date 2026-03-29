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

#include "request.h"
#include "response.h"

/**
 * Get a request header by name.
 * @param req Pointer to the HTTP request.
 * @param name Header name (case-insensitive).
 * @return Pointer to header value if found, otherwise NULL.
 */
const char* cHTTPX_HeaderGet(chttpx_request_t *req, const char *name);

/**
 * Add a new HTTP header.
 *
 * This function appends a header to the request/response header list.
 * Unlike HeaderSet, it does NOT replace existing headers with the same name.
 * This is required for headers like "Set-Cookie" that may appear multiple times.
 *
 * @param res   Pointer to HTTP request/response structure.
 * @param name  Header name.
 * @param value Header value.
 */
int cHTTPX_HeaderAdd(chttpx_response_t* res, const char* name, const char* value);

/**
 * Set or add a request header.
 * If header exists (case-insensitive), its value will be replaced.
 * Otherwise a new header will be added.
 *
 * @param req Pointer to the HTTP request.
 * @param name Header name.
 * @param value Header value.
 * @return 0 on success, -1 on error.
 */
int cHTTPX_HeaderSet(chttpx_request_t *req, const char *name, const char *value);

/**
 * Get the client's IP from the HEADER request.
 * 
 * @param req a pointer to the query structure
 * @return const char* Client's IP
 */
const char *cHTTPX_ClientIP(chttpx_request_t *req);

/* Parse headers in request */
void _parse_req_headers(chttpx_request_t *req, char *buffer, size_t buffer_len);

#ifdef __cplusplus
extern }
#endif

#endif