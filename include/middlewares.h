/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef MIDDLEWARES_H
#define MIDDLEWARES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "include/request.h"
#include "include/response.h"

#include <stdio.h>

#define MAX_MIDDLEWARES 32

typedef int (*chttpx_middleware_t)(
    chttpx_request_t *req,
    chttpx_response_t *res
);

typedef struct {
    chttpx_middleware_t middlewares[MAX_MIDDLEWARES];
    size_t middleware_count;
} chttpx_middleware_stack_t;

/**
 * Register a global middleware function.
 *
 * Middleware functions are executed in the order they are registered,
 * before the route handler is called.
 *
 * If a middleware returns 0, the middleware chain is aborted and the
 * response provided by the middleware is sent to the client.
 *
 * If a middleware returns 1, processing continues to the next middleware
 * or to the route handler.
 *
 * @param mw Middleware function pointer.
 */
void cHTTPX_MiddlewareUse(chttpx_middleware_t mw);

#ifdef __cplusplus
extern }
#endif

#endif