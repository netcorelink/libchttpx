/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef HEADERS_H
#define HEADERS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cHTTPX_request.h"
#include "cHTTPX_response.h"

    /**
     * Get a request header by name.
     * @param req Pointer to the HTTP request.
     * @param name Header name (case-insensitive).
     * @return Pointer to header value if found, otherwise NULL.
     */
    const char* cHTTPX_HeaderGet(chttpx_request_t* req, const char* name);

    /**
     * Add a new HTTP header.
     *
     * Appends a header to the response header list. Unlike HeaderSet, it does not
     * replace existing headers with the same name (required for Set-Cookie).
     *
     * @param res Pointer to HTTP response structure.
     * @param name Header name.
     * @param value Header value.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_HeaderAdd(chttpx_response_t* res, const char* name, const char* value);

    /**
     * Set or add a request header.
     * If a header exists (case-insensitive), its value is replaced; otherwise a new header is added.
     *
     * @param req Pointer to the HTTP request.
     * @param name Header name.
     * @param value Header value.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_HeaderSet(chttpx_request_t* req, const char* name, const char* value);

    /**
     * Get the client's IP from request headers (X-Forwarded-For or Remote-Addr).
     *
     * @param req Pointer to the HTTP request.
     * @return Client IP string, or NULL if not present.
     */
    const char* cHTTPX_ClientIP(chttpx_request_t* req);

    /**
     * Parse HTTP headers from a raw request buffer.
     *
     * @param req Pointer to the HTTP request being parsed.
     * @param buffer Raw bytes containing the request line and headers.
     * @param buffer_len Length of buffer in bytes.
     */
    void _parse_req_headers(chttpx_request_t* req, char* buffer, size_t buffer_len);

#ifdef __cplusplus
}
#endif

#endif
