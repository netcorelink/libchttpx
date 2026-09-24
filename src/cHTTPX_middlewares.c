/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software.
 */

#include "cHTTPX_middlewares.h"
#include "cHTTPX_metrics.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_headers.h"
#include "cHTTPX_http.h"
#include "cHTTPX_request.h"
#include "cHTTPX_response.h"
#include "cHTTPX_serv.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** Server-owned rate limiter hash table and synchronization. */
typedef struct
{
    rate_limiter_entry_t entries[MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE];
    char ips[MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE][64];
    uint32_t max_requests;
    uint32_t window_sec;
#ifdef CHTTPX_PLATFORM_WINDOWS
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
} chttpx_rate_limiter_state_t;

/**
 * Middleware registered.
 *
 * @param stack Parameter `stack`.
 * @param middleware Parameter `middleware`.
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
static int middleware_registered(const chttpx_middleware_stack_t* stack, chttpx_middleware_t middleware)
{
    if (!stack || !middleware)
        return 0;

    for (size_t i = 0; i < stack->middleware_count; i++)
        if (stack->middlewares[i] == middleware)
            return 1;

    return 0;
}

void cHTTPX_MiddlewareUse(chttpx_serv_t* server, chttpx_middleware_t middleware)
{
    if (!server || !server->initialized || !middleware)
        return;

    if (server->middleware.middleware_count >= MAX_MIDDLEWARES)
        return;

    server->middleware.middlewares[server->middleware.middleware_count++] = middleware;
}

void cHTTPX_MiddlewareUseAfter(chttpx_serv_t* server, chttpx_middleware_t middleware)
{
    if (!server || !server->initialized || !middleware || server->middleware.after_middleware_count >= MAX_MIDDLEWARES)
        return;

    server->middleware.after_middlewares[server->middleware.after_middleware_count++] = middleware;
}

/**
 * Rate limiter hash.
 *
 * @param ip Parameter `ip`.
 */
static uint32_t rate_limiter_hash(const char* ip)
{
    uint64_t hash = 5381;
    int value;

    while ((value = (unsigned char)*ip++))
        hash = ((hash << 5) + hash) + (uint64_t)value;

    return (uint32_t)(hash % MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE);
}

/**
 * Rate limiter lock.
 *
 * @param state Parameter `state`.
 */
static void rate_limiter_lock(chttpx_rate_limiter_state_t* state)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    EnterCriticalSection(&state->mutex);
#else
    pthread_mutex_lock(&state->mutex);
#endif
}

/**
 * Rate limiter unlock.
 *
 * @param state Parameter `state`.
 */
static void rate_limiter_unlock(chttpx_rate_limiter_state_t* state)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    LeaveCriticalSection(&state->mutex);
#else
    pthread_mutex_unlock(&state->mutex);
#endif
}

/**
 * Rate limiter middleware.
 *
 * @param req Current HTTP request.
 * @param response HTTP response.
 * @return Middleware chain result (out or next).
 */
static chttpx_middleware_result_t rate_limiter_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    chttpx_rate_limiter_state_t* state = server ? (chttpx_rate_limiter_state_t*)server->rate_limiter_state : NULL;
    if (!state)
        return next;

    rate_limiter_lock(state);

    uint32_t index = rate_limiter_hash(req->client_ip);
    rate_limiter_entry_t* entry = &state->entries[index];

    if (strcmp(state->ips[index], req->client_ip) != 0)
    {
        snprintf(state->ips[index], sizeof(state->ips[index]), "%s", req->client_ip);
        entry->window_start = time(NULL);
        entry->requests = 0;
    }

    time_t now = time(NULL);
    if (now - entry->window_start >= (time_t)state->window_sec)
    {
        entry->window_start = now;
        entry->requests = 0;
    }

    entry->requests++;

    if (entry->requests > state->max_requests)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusTooManyRequests, "{\"error\": \"too many requests\"}");
        _chttpx_metrics_rate_limit_failure(server);
        rate_limiter_unlock(state);
        return out;
    }

    rate_limiter_unlock(state);
    return next;
}

void cHTTPX_MiddlewareRateLimiter(chttpx_serv_t* server, uint32_t max_requests, uint32_t window_sec)
{
    if (!server || !server->initialized || max_requests == 0 || window_sec == 0)
        return;

    chttpx_rate_limiter_state_t* state = (chttpx_rate_limiter_state_t*)server->rate_limiter_state;
    if (!state)
    {
        state = calloc(1, sizeof(*state));
        if (!state)
            return;

#ifdef CHTTPX_PLATFORM_WINDOWS
        InitializeCriticalSection(&state->mutex);
#else
        if (pthread_mutex_init(&state->mutex, NULL) != 0)
        {
            free(state);
            return;
        }
#endif
        server->rate_limiter_state = state;
    }

    state->max_requests = max_requests;
    state->window_sec = window_sec;

    if (!middleware_registered(&server->middleware, rate_limiter_middleware))
        cHTTPX_MiddlewareUse(server, rate_limiter_middleware);
}

/**
 * Recovery init.
 *
 */
void _recovery_init(void)
{
}

/**
 * Recovery middleware.
 *
 * @param req Current HTTP request.
 * @param response HTTP response.
 * @return Middleware chain result (out or next).
 */
static chttpx_middleware_result_t recovery_middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    (void)res;
    return next;
}

void cHTTPX_MiddlewareRecovery(chttpx_serv_t* server)
{
    cHTTPX_MiddlewareUse(server, recovery_middleware);
}

/**
 * Diff ms.
 *
 * @param a Parameter `a`.
 * @param b Parameter `b`.
 * @return Computed value.
 */
static double diff_ms(struct timespec a, struct timespec b)
{
    return (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

void postmiddleware_logging_write(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    if (!server || !server->logging_enabled)
        return;

    double ms = diff_ms(res->start_ts, res->end_ts);
    char message[2048];
    snprintf(message, sizeof(message), "%s \"%s %s %s\" %d %zu \"%s\" %.4fms", req->client_ip, req->method ? req->method : "", req->path ? req->path : "", req->protocol, res->status, res->body_size, req->user_agent, ms);

    if (server->logger && server->log_level <= cHTTPX_LOG_INFO)
        server->logger(cHTTPX_LOG_INFO, req->request_id, message, server->logger_data);
}

void cHTTPX_MiddlewareLogging(chttpx_serv_t* server)
{
    if (server && server->initialized)
        server->logging_enabled = true;
}

/**
 * Middleware server cleanup.
 *
 * @param server HTTP server instance.
 */
void _chttpx_middleware_server_cleanup(chttpx_serv_t* server)
{
    if (!server || !server->rate_limiter_state)
        return;

    chttpx_rate_limiter_state_t* state = (chttpx_rate_limiter_state_t*)server->rate_limiter_state;
#ifdef CHTTPX_PLATFORM_WINDOWS
    DeleteCriticalSection(&state->mutex);
#else
    pthread_mutex_destroy(&state->mutex);
#endif
    free(state);
    server->rate_limiter_state = NULL;
}
