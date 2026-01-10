/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef CORS_H
#define CORS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

typedef struct {
    uint8_t enabled;
    /* Allowed urls */
    const char **origins;
    /* Origins count*/
    size_t origins_count;
    /* Allowed http methods */
    const char *methods;
    /* Allowed http headers */
    const char *headers;
} chttpx_cors_t;

/**
 * Enable and configure CORS (Cross-Origin Resource Sharing).
 *
 * This function enables CORS support for the HTTP server and configures
 * which origins, HTTP methods, and request headers are allowed.
 *
 * The CORS configuration is applied globally and is typically used together
 * with the built-in CORS middleware.
 *
 * @param origins        Array of allowed origin strings (e.g. "https://example.com").
 *                       Each origin must match exactly the value of the "Origin" header.
 * @param origins_count Number of elements in the origins array.
 * @param methods       Comma-separated list of allowed HTTP methods.
 *                       If NULL, defaults to:
 *                       "GET, POST, PUT, DELETE, OPTIONS"
 * @param headers       Comma-separated list of allowed request headers.
 *                       If NULL, defaults to:
 *                       "Content-Type"
 */
void cHTTPX_Cors(const char **origins, size_t origins_count, const char *methods, const char *headers);

#ifdef __cplusplus
extern }
#endif

#endif