/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef CORS_H
#define CORS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdint.h>

    struct chttpx_serv;

    /** CORS policy stored on the HTTP server. */
    typedef struct
    {
        uint8_t enabled;
        /* Allowed urls */
        const char** origins;
        /* Origins count*/
        size_t origins_count;
        /* Allowed http methods */
        const char* methods;
        /* Allowed http headers */
        const char* headers;
    } chttpx_cors_t;

    /**
     * Enable and configure CORS (Cross-Origin Resource Sharing).
     *
     * Copies the supplied configuration. Allowed origins are sorted for binary search at request time.
     *
     * @param server Initialized HTTP server to configure.
     * @param origins Array of exact allowed Origin header values.
     * @param origins_count Number of elements in origins.
     * @param methods Comma-separated allowed methods, or NULL for "GET, POST, PUT, DELETE, OPTIONS".
     * @param headers Comma-separated allowed request headers, or NULL for "Content-Type".
     */
    void cHTTPX_Cors(struct chttpx_serv* server, const char** origins, size_t origins_count, const char* methods, const char* headers);

#ifdef __cplusplus
}
#endif

#endif
