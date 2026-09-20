/*
 * Copyright (c) 2026 netcorelink
 *
 * Lightweight per-server metrics and Prometheus exporter.
 */

#include "cHTTPX_metrics.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_http.h"
#include "cHTTPX_response.h"
#include "cHTTPX_serv.h"
#include "cHTTPX_compression.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CHTTPX_PLATFORM_POSIX
#include <pthread.h>
#endif

typedef struct
{
    const char* method;
    const char* route;
    uint64_t requests_total;
    uint64_t status_class[5];
} chttpx_route_metrics_entry_t;

typedef struct
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    CRITICAL_SECTION mutex;
#else
    pthread_mutex_t mutex;
#endif
    chttpx_metrics_t metrics;
    chttpx_route_metrics_entry_t* routes;
    size_t routes_count;
    size_t routes_capacity;
} chttpx_metrics_state_t;

static const double duration_bounds_seconds[] = {
    0.001,
    0.005,
    0.010,
    0.025,
    0.050,
    0.100,
    0.500,
    1.000,
};

static void metrics_lock(chttpx_metrics_state_t* state)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    EnterCriticalSection(&state->mutex);
#else
    pthread_mutex_lock(&state->mutex);
#endif
}

static void metrics_unlock(chttpx_metrics_state_t* state)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    LeaveCriticalSection(&state->mutex);
#else
    pthread_mutex_unlock(&state->mutex);
#endif
}

static chttpx_metrics_state_t* metrics_state(chttpx_serv_t* server)
{
    return server ? (chttpx_metrics_state_t*)server->metrics_state : NULL;
}

static size_t status_class_index(int status)
{
    if (status >= 100 && status < 200)
        return 0;
    if (status >= 200 && status < 300)
        return 1;
    if (status >= 300 && status < 400)
        return 2;
    if (status >= 400 && status < 500)
        return 3;
    return 4;
}

static void increment_status_class(chttpx_metrics_t* metrics, int status)
{
    switch (status_class_index(status))
    {
    case 0:
        metrics->responses_1xx_total++;
        break;
    case 1:
        metrics->responses_2xx_total++;
        break;
    case 2:
        metrics->responses_3xx_total++;
        break;
    case 3:
        metrics->responses_4xx_total++;
        break;
    default:
        metrics->responses_5xx_total++;
        break;
    }
}

static chttpx_route_metrics_entry_t* route_metrics_entry(chttpx_metrics_state_t* state,
                                                         const char* method,
                                                         const char* route_template)
{
    if (!state || !method || !route_template)
        return NULL;

    for (size_t i = 0; i < state->routes_count; i++)
    {
        chttpx_route_metrics_entry_t* entry = &state->routes[i];
        if (strcmp(entry->method, method) == 0 && strcmp(entry->route, route_template) == 0)
            return entry;
    }

    if (state->routes_count == state->routes_capacity)
    {
        size_t capacity = state->routes_capacity ? state->routes_capacity * 2 : 16;
        if (capacity < state->routes_capacity || capacity > SIZE_MAX / sizeof(*state->routes))
            return NULL;

        chttpx_route_metrics_entry_t* routes =
            realloc(state->routes, capacity * sizeof(*state->routes));
        if (!routes)
            return NULL;

        state->routes = routes;
        state->routes_capacity = capacity;
    }

    chttpx_route_metrics_entry_t* entry = &state->routes[state->routes_count++];
    memset(entry, 0, sizeof(*entry));
    entry->method = method;
    entry->route = route_template;
    return entry;
}

int _chttpx_metrics_server_init(chttpx_serv_t* server, int enabled)
{
    if (!server)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    if (!enabled)
    {
        server->metrics_state = NULL;
        return CHTTPX_OK;
    }

    chttpx_metrics_state_t* state = calloc(1, sizeof(*state));
    if (!state)
        return CHTTPX_ERR_MEMORY;

#ifdef CHTTPX_PLATFORM_WINDOWS
    InitializeCriticalSection(&state->mutex);
#else
    if (pthread_mutex_init(&state->mutex, NULL) != 0)
    {
        free(state);
        return CHTTPX_ERR_STATE;
    }
#endif

    server->metrics_state = state;
    return CHTTPX_OK;
}

void _chttpx_metrics_server_cleanup(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;

#ifdef CHTTPX_PLATFORM_WINDOWS
    DeleteCriticalSection(&state->mutex);
#else
    pthread_mutex_destroy(&state->mutex);
#endif
    free(state->routes);
    free(state);
    server->metrics_state = NULL;
}

void _chttpx_metrics_connection_accepted(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;
    metrics_lock(state);
    state->metrics.connections_accepted_total++;
    metrics_unlock(state);
}

void _chttpx_metrics_connection_rejected(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;
    metrics_lock(state);
    state->metrics.connections_rejected_total++;
    metrics_unlock(state);
}

void _chttpx_metrics_connection_opened(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;
    metrics_lock(state);
    state->metrics.connections_active++;
    metrics_unlock(state);
}

void _chttpx_metrics_connection_closed(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;
    metrics_lock(state);
    if (state->metrics.connections_active > 0)
        state->metrics.connections_active--;
    metrics_unlock(state);
}

void _chttpx_metrics_request_begin(chttpx_serv_t* server, size_t request_bytes)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;

    metrics_lock(state);
    state->metrics.requests_total++;
    state->metrics.requests_in_flight++;
    state->metrics.request_bytes_total += (uint64_t)request_bytes;
    metrics_unlock(state);
}

void _chttpx_metrics_request_end(chttpx_serv_t* server,
                                 const char* method,
                                 const char* route_template,
                                 int status,
                                 size_t response_bytes,
                                 double duration_seconds)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;

    if (duration_seconds < 0.0)
        duration_seconds = 0.0;

    metrics_lock(state);

    if (state->metrics.requests_in_flight > 0)
        state->metrics.requests_in_flight--;

    increment_status_class(&state->metrics, status);
    state->metrics.response_bytes_total += (uint64_t)response_bytes;
    state->metrics.request_duration_count++;
    state->metrics.request_duration_seconds_sum += duration_seconds;

    for (size_t i = 0; i < CHTTPX_ARRAY_LEN(duration_bounds_seconds); i++)
    {
        if (duration_seconds <= duration_bounds_seconds[i])
            state->metrics.request_duration_buckets[i]++;
    }
    state->metrics.request_duration_buckets[CHTTPX_METRICS_DURATION_BUCKETS - 1]++;

    chttpx_route_metrics_entry_t* route_entry =
        route_metrics_entry(state, method, route_template);
    if (route_entry)
    {
        route_entry->requests_total++;
        route_entry->status_class[status_class_index(status)]++;
    }

    metrics_unlock(state);
}

void _chttpx_metrics_parser_failure(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;
    metrics_lock(state);
    state->metrics.parser_failures_total++;
    metrics_unlock(state);
}

void _chttpx_metrics_timeout_failure(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;
    metrics_lock(state);
    state->metrics.timeout_failures_total++;
    metrics_unlock(state);
}

void _chttpx_metrics_rate_limit_failure(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;
    metrics_lock(state);
    state->metrics.rate_limit_failures_total++;
    metrics_unlock(state);
}

int cHTTPX_ServerMetrics(chttpx_serv_t* server, chttpx_metrics_t* metrics)
{
    if (!server || !metrics)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return CHTTPX_ERR_UNAVAILABLE;

    metrics_lock(state);
    *metrics = state->metrics;
    metrics_unlock(state);
    return CHTTPX_OK;
}

typedef struct
{
    char* data;
    size_t length;
    size_t capacity;
} prometheus_buffer_t;

static int prometheus_reserve(prometheus_buffer_t* buffer, size_t extra)
{
    if (!buffer || extra > SIZE_MAX - buffer->length - 1)
        return 0;

    size_t required = buffer->length + extra + 1;
    if (required <= buffer->capacity)
        return 1;

    size_t capacity = buffer->capacity ? buffer->capacity : 2048;
    while (capacity < required)
    {
        if (capacity > SIZE_MAX / 2)
        {
            capacity = required;
            break;
        }
        capacity *= 2;
    }

    char* data = realloc(buffer->data, capacity);
    if (!data)
        return 0;

    buffer->data = data;
    buffer->capacity = capacity;
    return 1;
}

static int prometheus_append(prometheus_buffer_t* buffer, const char* text)
{
    size_t length = strlen(text);
    if (!prometheus_reserve(buffer, length))
        return 0;
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 1;
}

static int prometheus_appendf(prometheus_buffer_t* buffer, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (needed < 0)
    {
        va_end(args);
        return 0;
    }

    if (!prometheus_reserve(buffer, (size_t)needed))
    {
        va_end(args);
        return 0;
    }

    vsnprintf(buffer->data + buffer->length,
              buffer->capacity - buffer->length,
              format,
              args);
    va_end(args);
    buffer->length += (size_t)needed;
    return 1;
}

static int prometheus_append_label_value(prometheus_buffer_t* buffer, const char* value)
{
    if (!value)
        value = "";

    for (const unsigned char* cursor = (const unsigned char*)value; *cursor; cursor++)
    {
        char escaped[3] = {0};
        if (*cursor == '\\')
        {
            escaped[0] = '\\';
            escaped[1] = '\\';
        }
        else if (*cursor == '"')
        {
            escaped[0] = '\\';
            escaped[1] = '"';
        }
        else if (*cursor == '\n')
        {
            escaped[0] = '\\';
            escaped[1] = 'n';
        }
        else
        {
            escaped[0] = (char)*cursor;
        }

        if (!prometheus_append(buffer, escaped))
            return 0;
    }
    return 1;
}

static int prometheus_global_metrics(prometheus_buffer_t* buffer,
                                     const chttpx_metrics_t* metrics)
{
    static const char* bucket_labels[] = {
        "0.001",
        "0.005",
        "0.01",
        "0.025",
        "0.05",
        "0.1",
        "0.5",
        "1",
        "+Inf",
    };

    if (!prometheus_append(buffer, "# HELP libchttpx_requests_total Total HTTP requests processed.\n"
                                   "# TYPE libchttpx_requests_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_requests_total %llu\n",
                            (unsigned long long)metrics->requests_total) ||
        !prometheus_append(buffer, "# HELP libchttpx_requests_in_flight Requests currently being processed.\n"
                                   "# TYPE libchttpx_requests_in_flight gauge\n") ||
        !prometheus_appendf(buffer, "libchttpx_requests_in_flight %llu\n",
                            (unsigned long long)metrics->requests_in_flight) ||
        !prometheus_append(buffer, "# HELP libchttpx_connections_active Active accepted network connections.\n"
                                   "# TYPE libchttpx_connections_active gauge\n") ||
        !prometheus_appendf(buffer, "libchttpx_connections_active %llu\n",
                            (unsigned long long)metrics->connections_active) ||
        !prometheus_append(buffer, "# TYPE libchttpx_connections_accepted_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_connections_accepted_total %llu\n",
                            (unsigned long long)metrics->connections_accepted_total) ||
        !prometheus_append(buffer, "# TYPE libchttpx_connections_rejected_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_connections_rejected_total %llu\n",
                            (unsigned long long)metrics->connections_rejected_total) ||
        !prometheus_append(buffer, "# HELP libchttpx_responses_total HTTP responses grouped by status class.\n"
                                   "# TYPE libchttpx_responses_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_responses_total{class=\"1xx\"} %llu\n",
                            (unsigned long long)metrics->responses_1xx_total) ||
        !prometheus_appendf(buffer, "libchttpx_responses_total{class=\"2xx\"} %llu\n",
                            (unsigned long long)metrics->responses_2xx_total) ||
        !prometheus_appendf(buffer, "libchttpx_responses_total{class=\"3xx\"} %llu\n",
                            (unsigned long long)metrics->responses_3xx_total) ||
        !prometheus_appendf(buffer, "libchttpx_responses_total{class=\"4xx\"} %llu\n",
                            (unsigned long long)metrics->responses_4xx_total) ||
        !prometheus_appendf(buffer, "libchttpx_responses_total{class=\"5xx\"} %llu\n",
                            (unsigned long long)metrics->responses_5xx_total) ||
        !prometheus_append(buffer, "# TYPE libchttpx_request_bytes_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_request_bytes_total %llu\n",
                            (unsigned long long)metrics->request_bytes_total) ||
        !prometheus_append(buffer, "# TYPE libchttpx_response_bytes_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_response_bytes_total %llu\n",
                            (unsigned long long)metrics->response_bytes_total) ||
        !prometheus_append(buffer, "# TYPE libchttpx_parser_failures_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_parser_failures_total %llu\n",
                            (unsigned long long)metrics->parser_failures_total) ||
        !prometheus_append(buffer, "# TYPE libchttpx_timeout_failures_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_timeout_failures_total %llu\n",
                            (unsigned long long)metrics->timeout_failures_total) ||
        !prometheus_append(buffer, "# TYPE libchttpx_rate_limit_failures_total counter\n") ||
        !prometheus_appendf(buffer, "libchttpx_rate_limit_failures_total %llu\n",
                            (unsigned long long)metrics->rate_limit_failures_total) ||
        !prometheus_append(buffer, "# HELP libchttpx_request_duration_seconds HTTP request duration.\n"
                                   "# TYPE libchttpx_request_duration_seconds histogram\n"))
        return 0;

    for (size_t i = 0; i < CHTTPX_METRICS_DURATION_BUCKETS; i++)
    {
        if (!prometheus_appendf(buffer,
                                "libchttpx_request_duration_seconds_bucket{le=\"%s\"} %llu\n",
                                bucket_labels[i],
                                (unsigned long long)metrics->request_duration_buckets[i]))
            return 0;
    }

    return prometheus_appendf(buffer, "libchttpx_request_duration_seconds_sum %.9f\n",
                              metrics->request_duration_seconds_sum) &&
           prometheus_appendf(buffer, "libchttpx_request_duration_seconds_count %llu\n",
                              (unsigned long long)metrics->request_duration_count);
}

static int prometheus_route_metrics(prometheus_buffer_t* buffer,
                                    chttpx_metrics_state_t* state)
{
    if (!prometheus_append(buffer,
                           "# HELP libchttpx_route_requests_total HTTP requests grouped by registered route template, method and status class.\n"
                           "# TYPE libchttpx_route_requests_total counter\n"))
        return 0;

    for (size_t i = 0; i < state->routes_count; i++)
    {
        chttpx_route_metrics_entry_t* entry = &state->routes[i];
        for (size_t status = 0; status < 5; status++)
        {
            if (entry->status_class[status] == 0)
                continue;

            if (!prometheus_append(buffer, "libchttpx_route_requests_total{method=\"") ||
                !prometheus_append_label_value(buffer, entry->method) ||
                !prometheus_append(buffer, "\",route=\"") ||
                !prometheus_append_label_value(buffer, entry->route) ||
                !prometheus_appendf(buffer,
                                    "\",class=\"%zuxx\"} %llu\n",
                                    status + 1,
                                    (unsigned long long)entry->status_class[status]))
                return 0;
        }
    }

    return 1;
}

static void metrics_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusServiceUnavailable,
                               "metrics are disabled");
        return;
    }

    prometheus_buffer_t output = {0};

    metrics_lock(state);
    chttpx_metrics_t snapshot = state->metrics;
    int ok = prometheus_global_metrics(&output, &snapshot) &&
             prometheus_route_metrics(&output, state);
    metrics_unlock(state);

    if (!ok)
    {
        free(output.data);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError,
                               "failed to render metrics");
        return;
    }

    *res = cHTTPX_ResBinary(cHTTPX_StatusOK,
                            "text/plain; version=0.0.4; charset=utf-8",
                            (const unsigned char*)output.data,
                            output.length);
    free(output.data);
    cHTTPX_ResponseCompression(res, false);
}

int cHTTPX_MetricsRoute(chttpx_router_t* router, const char* path)
{
    if (!router || !router->serv || !path || !*path)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    if (!metrics_state(router->serv))
        return CHTTPX_ERR_UNAVAILABLE;

    chttpx_route_t* route = cHTTPX_Get(router, path, metrics_handler);
    if (!route)
        return CHTTPX_ERR_MEMORY;

    cHTTPX_RouteCompression(route, false);
    return CHTTPX_OK;
}
