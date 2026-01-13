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

#include "include/middlewares.h"

#include "include/serv.h"
#include "include/http.h"
#include "include/headers.h"
#include "include/request.h"
#include "include/response.h"
#include "include/crosspltm.h"

#include <stdio.h>
#include <string.h>

static rate_limiter_entry_t rate_limits[MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE];
static char rate_limit_ips[MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE][64];

#if defined(_WIN32) || defined(_WIN64)
static CRITICAL_SECTION rate_limit_mu;

#define INIT_MUTEX() InitializeCriticalSection(&rate_limit_mu)
#define LOCK_MUTEX() EnterCriticalSection(&rate_limit_mu)
#define UNLOCK_MUTEX() LeaveCriticalSection(&rate_limit_mu)
#else
static pthread_mutex_t rate_limit_mu = PTHREAD_MUTEX_INITIALIZER;

#define INIT_MUTEX()
#define LOCK_MUTEX() pthread_mutex_lock(&rate_limit_mu)
#define UNLOCK_MUTEX() pthread_mutex_unlock(&rate_limit_mu)
#endif

static uint8_t rl_max_requests = 5;
static uint16_t rl_window_sec = 1;

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
void cHTTPX_MiddlewareUse(chttpx_middleware_t mw) {
    if (!serv) {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    if (serv->middleware.middleware_count >= MAX_MIDDLEWARES) {
        fprintf(stderr, "Error: the number of middleware (MAX_MIDDLEWARES) has been exceeded\n");
        return;
    }

    serv->middleware.middlewares[serv->middleware.middleware_count++] = mw;
}

static uint32_t rate_limiter_hash(const char *ip) {
    uint32_t hash = 5381;
    int char_v;

    while ((char_v = *ip++)) hash = ((hash << 5) + hash) + char_v;
    return hash % MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE;
}

static chttpx_middleware_result_t rate_limiter_middleware(chttpx_request_t *req, chttpx_response_t *res) {
    const char *client_ip = cHTTPX_ClientIP(req);
    if (!client_ip) return next;

    LOCK_MUTEX();

    uint32_t indx = rate_limiter_hash(client_ip);
    rate_limiter_entry_t *entry = &rate_limits[indx];

    if (strcmp(rate_limit_ips[indx], client_ip) != 0) {
        strncpy(rate_limit_ips[indx], client_ip, sizeof(rate_limit_ips[indx]) - 1);
        rate_limit_ips[indx][sizeof(rate_limit_ips[indx]) - 1] = 0;

        entry->window_start = time(NULL);
        entry->requests = 0;
    }

    time_t now = time(NULL);

    if (now - entry->window_start >= rl_window_sec) {
        entry->window_start = now;
        entry->requests = 0;
    }

    entry->requests++;

    if (entry->requests > rl_max_requests) {
        *res = cHTTPX_JsonResponse(cHTTPX_StatusTooManyRequests, "{\"error\": \"too many requests\"}");

        UNLOCK_MUTEX();
        return out;
    }

    UNLOCK_MUTEX();
    return next;
}

/**
 * @brief Configure the rate limiter and register the middleware.
 * 
 * Example:
 * cHTTPX_MiddlewareRateLimiter(10, 1); // 10 requests per second
 * 
 * @param max_requests maximum number of requests
 * @param window_sec time window in seconds
 */
void cHTTPX_MiddlewareRateLimiter(uint32_t max_requests, uint32_t window_sec) {
    rl_max_requests = max_requests;
    rl_window_sec = window_sec;

    /* middleware */
    INIT_MUTEX();
    cHTTPX_MiddlewareUse(rate_limiter_middleware);
}