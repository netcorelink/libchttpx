/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "middlewares.h"

#include "crosspltm.h"
#include "headers.h"
#include "http.h"
#include "request.h"
#include "response.h"
#include "serv.h"

#include <time.h>
#include <stdio.h>
#include <string.h>

static char logging_enabled = 0;

/* Rate limiter */
static rate_limiter_entry_t rate_limits[MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE];
static char rate_limit_ips[MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE][64];

#if defined(_WIN32) || defined(_WIN64)
static CRITICAL_SECTION rate_limit_mu;

#define INIT_RLIMIT_MUTEX() InitializeCriticalSection(&rate_limit_mu)
#define LOCK_RLIMIT_MUTEX() EnterCriticalSection(&rate_limit_mu)
#define UNLOCK_RLIMIT_MUTEX() LeaveCriticalSection(&rate_limit_mu)
#else
static pthread_mutex_t rate_limit_mu = PTHREAD_MUTEX_INITIALIZER;

#define INIT_RLIMIT_MUTEX()
#define LOCK_RLIMIT_MUTEX() pthread_mutex_lock(&rate_limit_mu)
#define UNLOCK_RLIMIT_MUTEX() pthread_mutex_unlock(&rate_limit_mu)
#endif

static uint32_t rl_max_requests = 5;
static uint32_t rl_window_sec = 1;

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
void cHTTPX_MiddlewareUse(chttpx_middleware_t mw)
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    if (serv->middleware.middleware_count >= MAX_MIDDLEWARES)
    {
        fprintf(stderr, "Error: the number of middleware (MAX_MIDDLEWARES) has been exceeded\n");
        return;
    }

    serv->middleware.middlewares[serv->middleware.middleware_count++] = mw;
}

void cHTTPX_MiddlewareUseAfter(chttpx_middleware_t mw)
{
    if (!serv || !mw || serv->middleware.after_middleware_count >= MAX_MIDDLEWARES)
        return;
    serv->middleware.after_middlewares[serv->middleware.after_middleware_count++] = mw;
}

static uint32_t rate_limiter_hash(const char* ip)
{
    uint64_t hash = 5381;
    int char_v;

    while ((char_v = *ip++))
        hash = ((hash << 5) + hash) + char_v;
    return hash % MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE;
}

static chttpx_middleware_result_t rate_limiter_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    LOCK_RLIMIT_MUTEX();

    uint32_t indx = rate_limiter_hash(req->client_ip);
    rate_limiter_entry_t* entry = &rate_limits[indx];

    if (strcmp(rate_limit_ips[indx], req->client_ip) != 0)
    {
        strncpy(rate_limit_ips[indx], req->client_ip, sizeof(rate_limit_ips[indx]) - 1);
        rate_limit_ips[indx][sizeof(rate_limit_ips[indx]) - 1] = 0;

        entry->window_start = time(NULL);
        entry->requests = 0;
    }

    time_t now = time(NULL);

    if (now - entry->window_start >= rl_window_sec)
    {
        entry->window_start = now;
        entry->requests = 0;
    }

    entry->requests++;

    if (entry->requests > rl_max_requests)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusTooManyRequests, "{\"error\": \"too many requests\"}");

        UNLOCK_RLIMIT_MUTEX();
        return out;
    }

    UNLOCK_RLIMIT_MUTEX();
    return next;
}

/**
 * Configure the rate limiter and register the middleware.
 *
 * Example:
 * cHTTPX_MiddlewareRateLimiter(10, 1); // 10 requests per second
 *
 * @param max_requests maximum number of requests
 * @param window_sec time window in seconds
 */
void cHTTPX_MiddlewareRateLimiter(uint32_t max_requests, uint32_t window_sec)
{
    rl_max_requests = max_requests;
    rl_window_sec = window_sec;

    /* middleware */
    INIT_RLIMIT_MUTEX();
    cHTTPX_MiddlewareUse(rate_limiter_middleware);
}

/**
 * Compatibility hook. Fatal signal recovery is deliberately disabled.
 */
void _recovery_init(void)
{
}

static chttpx_middleware_result_t recovery_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    (void)res;
    return next;
}

/**
 * Compatibility no-op. Use a process supervisor for fatal faults.
 */
void cHTTPX_MiddlewareRecovery()
{
    cHTTPX_MiddlewareUse(recovery_middleware);
}

static double diff_ms(struct timespec a, struct timespec b)
{
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

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
void postmiddleware_logging_write(chttpx_request_t* req, chttpx_response_t* res)
{
    if (!logging_enabled)
        return;

    double ms = diff_ms(res->start_ts, res->end_ts);
    char message[2048];
    snprintf(message, sizeof(message), "%s \"%s %s %s\" %d %zu \"%s\" %.4fms", req->client_ip, req->method, req->path, req->protocol, res->status,
             res->body_size, req->user_agent, ms);
    if (serv && serv->logger && serv->log_level <= CHTTPX_LOG_INFO)
        serv->logger(CHTTPX_LOG_INFO, req->request_id, message, serv->logger_data);
}

/**
 * Registers a middleware for logging HTTP requests.
 *
 * This middleware is executed after every request to log request and response
 * information into a log file. If logging has not been initialized via
 * cHTTPX_LoggingInit, this middleware does not perform any logging.
 */
void cHTTPX_MiddlewareLogging()
{
    logging_enabled = 1;
}
