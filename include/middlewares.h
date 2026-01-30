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

#include "request.h"
#include "response.h"

#include <stdio.h>
#include <pthread.h>

#define MAX_MIDDLEWARES 128

/* Enum for result all middlewares */
typedef enum {
    out = 0,
    next = 1,
} chttpx_middleware_result_t;

typedef chttpx_middleware_result_t (*chttpx_middleware_t)(
    chttpx_request_t *req,
    chttpx_response_t *res
);

/* Struct for base middlewares */
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
 * If a middleware returns 0(out), the middleware chain is aborted and the
 * response provided by the middleware is sent to the client.
 *
 * If a middleware returns 1(next), processing continues to the next middleware
 * or to the route handler.
 *
 * @param mw Middleware function pointer.
 */
void cHTTPX_MiddlewareUse(chttpx_middleware_t mw);

#define MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE 4096

/* Struct for middleware [RATE LIMITER]*/
typedef struct {
    /* Start limit window time*/
    time_t window_start;
    /* How many requests have already been received in this window */
    uint32_t requests;
} rate_limiter_entry_t;

/**
 * Configure the rate limiter and register the middleware.
 * 
 * Example:
 * cHTTPX_MiddlewareRateLimiter(10, 1); // 10 requests per second
 * 
 * @param max_requests maximum number of requests
 * @param window_sec time window in seconds
 */
void cHTTPX_MiddlewareRateLimiter(uint32_t max_requests, uint32_t window_sec);

/**
 * Initialize global recovery signal handlers.
 *
 * This function installs signal handlers for critical runtime errors
 * such as segmentation faults, abort signals, and floating-point exceptions.
 *
 * When a registered signal is raised during request processing,
 * the handler will transfer control back to the recovery middleware
 * using setjmp/longjmp instead of terminating the process.
 */
void _recovery_init(void);

/**
 * Recovery middleware.
 *
 * This middleware protects the request processing pipeline from fatal
 * runtime errors such as segmentation faults.
 *
 * Internally, it uses setjmp/longjmp together with POSIX signal handlers
 * to recover control flow if a critical signal occurs while handling
 * the request.
 *
 * If a signal is caught:
 *  - The error is logged to stderr
 *  - A 500 Internal Server Error JSON response is returned
 *  - Further middleware and handlers are skipped
 *
 * @param req Pointer to the HTTP request structure.
 * @param res Pointer to the HTTP response structure.
 */
void cHTTPX_MiddlewareRecovery();

/**
 * Writes the HTTP request and response log to a file.
 *
 * This function is called after a request has been processed and a response
 * has been generated. It collects information about the client, HTTP method,
 * request path, protocol, response status, response size, and processing time,
 * then writes it to a log file.
 *
 * @param req Pointer to the chttpx_request_t request structure.
 * @param res Pointer to the chttpx_response_t response structure.
 */
void postmiddleware_logging_write(chttpx_request_t* req, chttpx_response_t* res);

/**
 * Registers a middleware for logging HTTP requests.
 *
 * This middleware is executed after every request to log request and response
 * information into a log file. If logging has not been initialized via
 * cHTTPX_LoggingInit, this middleware does not perform any logging.
 */
void cHTTPX_MiddlewareLogging();

#ifdef __cplusplus
extern }
#endif

#endif