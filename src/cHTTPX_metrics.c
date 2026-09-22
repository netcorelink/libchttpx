/*
 * Copyright (c) 2026 netcorelink
 *
 * Lightweight per-server metrics and Prometheus exporter.
 */

#include "cHTTPX_metrics.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_http.h"
#include "cHTTPX_response.h"
#include "cHTTPX_runtime.h"
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
    CRITICAL_SECTION routes_mutex;
#else
    pthread_mutex_t routes_mutex;
#endif
    chttpx_metrics_t metrics;
    uint64_t request_duration_nanoseconds_total;
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

/**
 * Lock the route metrics hash table.
 *
 * Global counters intentionally use relaxed atomics and do not share this
 * lock, avoiding one server-wide mutex on every metrics update.
 *
 * @param state Metrics state containing the route table.
 */
static void routes_lock(chttpx_metrics_state_t* state)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    EnterCriticalSection(&state->routes_mutex);
#else
    pthread_mutex_lock(&state->routes_mutex);
#endif
}

/**
 * Unlock the route metrics hash table.
 *
 * @param state Metrics state containing the route table.
 */
static void routes_unlock(chttpx_metrics_state_t* state)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    LeaveCriticalSection(&state->routes_mutex);
#else
    pthread_mutex_unlock(&state->routes_mutex);
#endif
}

/**
 * Return the internal metrics state associated with a server.
 *
 * @param server HTTP server instance.
 * @return Internal metrics state or NULL when metrics are disabled.
 */
static chttpx_metrics_state_t* metrics_state(chttpx_serv_t* server)
{
    return server ? (chttpx_metrics_state_t*)server->metrics_state : NULL;
}

/**
 * Load an integral metrics field without taking the route-table lock.
 *
 * @param value Counter or gauge address.
 * @return Current value.
 */
static uint64_t metric_load(const uint64_t* value)
{
    return __atomic_load_n(value, __ATOMIC_RELAXED);
}

/**
 * Add to an integral metrics field without taking a global mutex.
 *
 * @param value Counter or gauge address.
 * @param amount Amount to add.
 */
static void metric_add(uint64_t* value, uint64_t amount)
{
    __atomic_fetch_add(value, amount, __ATOMIC_RELAXED);
}

/**
 * Decrement a gauge only when it is greater than zero.
 *
 * @param value Gauge address.
 */
static void metric_decrement_nonzero(uint64_t* value)
{
    uint64_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (current > 0)
    {
        if (__atomic_compare_exchange_n(value, &current, current - 1, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return;
    }
}

/**
 * Convert an HTTP status code to the 1xx..5xx metrics bucket index.
 *
 * @param status HTTP status code.
 * @return Bucket index in the range 0..4.
 */
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

/**
 * Increment the response status-class counter.
 *
 * @param state Metrics state.
 * @param status HTTP response status.
 */
static void increment_status_class(chttpx_metrics_state_t* state, int status)
{
    uint64_t* counter = NULL;
    switch (status_class_index(status))
    {
    case 0:
        counter = &state->metrics.responses_1xx_total;
        break;
    case 1:
        counter = &state->metrics.responses_2xx_total;
        break;
    case 2:
        counter = &state->metrics.responses_3xx_total;
        break;
    case 3:
        counter = &state->metrics.responses_4xx_total;
        break;
    default:
        counter = &state->metrics.responses_5xx_total;
        break;
    }
    metric_add(counter, 1);
}

/**
 * Hash a route identity with FNV-1a.
 *
 * @param method Registered HTTP method.
 * @param route Registered route template.
 * @return Stable 64-bit hash.
 */
static uint64_t route_metrics_hash(const char* method, const char* route)
{
    uint64_t hash = 14695981039346656037ULL;
    const char* values[] = {method, route};

    for (size_t part = 0; part < CHTTPX_ARRAY_LEN(values); ++part)
    {
        for (const unsigned char* p = (const unsigned char*)values[part]; *p; ++p)
        {
            hash ^= (uint64_t)*p;
            hash *= 1099511628211ULL;
        }
        hash ^= 0xffU;
        hash *= 1099511628211ULL;
    }

    return hash;
}

/**
 * Rebuild the open-addressed route metrics table.
 *
 * @param state Metrics state.
 * @param capacity New power-of-two capacity.
 * @return 1 on success, 0 on allocation failure.
 */
static int route_metrics_rehash(chttpx_metrics_state_t* state, size_t capacity)
{
    chttpx_route_metrics_entry_t* routes = calloc(capacity, sizeof(*routes));
    if (!routes)
        return 0;

    for (size_t i = 0; i < state->routes_capacity; ++i)
    {
        chttpx_route_metrics_entry_t entry = state->routes[i];
        if (!entry.method)
            continue;

        size_t index = (size_t)(route_metrics_hash(entry.method, entry.route) & (uint64_t)(capacity - 1));
        while (routes[index].method)
            index = (index + 1) & (capacity - 1);
        routes[index] = entry;
    }

    free(state->routes);
    state->routes = routes;
    state->routes_capacity = capacity;
    return 1;
}

/**
 * Find or create route metrics in average O(1) time.
 *
 * Caller must hold routes_mutex.
 *
 * @param state Metrics state.
 * @param method Registered HTTP method.
 * @param route_template Registered route template.
 * @return Route metrics entry or NULL on allocation failure.
 */
static chttpx_route_metrics_entry_t* route_metrics_entry(chttpx_metrics_state_t* state,
                                                         const char* method,
                                                         const char* route_template)
{
    if (!state || !method || !route_template)
        return NULL;

    if (state->routes_capacity == 0)
    {
        if (!route_metrics_rehash(state, 16))
            return NULL;
    }
    else if ((state->routes_count + 1) * 10 >= state->routes_capacity * 7)
    {
        if (state->routes_capacity > SIZE_MAX / 2)
            return NULL;
        if (!route_metrics_rehash(state, state->routes_capacity * 2))
            return NULL;
    }

    size_t index = (size_t)(route_metrics_hash(method, route_template) & (uint64_t)(state->routes_capacity - 1));
    while (state->routes[index].method)
    {
        chttpx_route_metrics_entry_t* entry = &state->routes[index];
        if (strcmp(entry->method, method) == 0 && strcmp(entry->route, route_template) == 0)
            return entry;
        index = (index + 1) & (state->routes_capacity - 1);
    }

    chttpx_route_metrics_entry_t* entry = &state->routes[index];
    entry->method = method;
    entry->route = route_template;
    state->routes_count++;
    return entry;
}

/**
 * Copy all atomic global metrics into the public snapshot representation.
 *
 * @param state Internal metrics state.
 * @param metrics Output snapshot.
 */
static void metrics_snapshot(chttpx_metrics_state_t* state, chttpx_metrics_t* metrics)
{
    memset(metrics, 0, sizeof(*metrics));

    metrics->requests_total = metric_load(&state->metrics.requests_total);
    metrics->requests_in_flight = metric_load(&state->metrics.requests_in_flight);
    metrics->connections_active = metric_load(&state->metrics.connections_active);
    metrics->connections_accepted_total = metric_load(&state->metrics.connections_accepted_total);
    metrics->connections_rejected_total = metric_load(&state->metrics.connections_rejected_total);
    metrics->responses_1xx_total = metric_load(&state->metrics.responses_1xx_total);
    metrics->responses_2xx_total = metric_load(&state->metrics.responses_2xx_total);
    metrics->responses_3xx_total = metric_load(&state->metrics.responses_3xx_total);
    metrics->responses_4xx_total = metric_load(&state->metrics.responses_4xx_total);
    metrics->responses_5xx_total = metric_load(&state->metrics.responses_5xx_total);
    metrics->request_bytes_total = metric_load(&state->metrics.request_bytes_total);
    metrics->response_bytes_total = metric_load(&state->metrics.response_bytes_total);
    metrics->parser_failures_total = metric_load(&state->metrics.parser_failures_total);
    metrics->timeout_failures_total = metric_load(&state->metrics.timeout_failures_total);
    metrics->rate_limit_failures_total = metric_load(&state->metrics.rate_limit_failures_total);
    metrics->request_duration_count = metric_load(&state->metrics.request_duration_count);
    metrics->request_duration_seconds_sum =
        (double)metric_load(&state->request_duration_nanoseconds_total) / 1000000000.0;

    for (size_t i = 0; i < CHTTPX_METRICS_DURATION_BUCKETS; ++i)
        metrics->request_duration_buckets[i] = metric_load(&state->metrics.request_duration_buckets[i]);
}

/**
 * Initialize per-server metrics state.
 *
 * @param server HTTP server instance.
 * @param enabled Non-zero when metrics collection is enabled.
 * @return cHTTPX_OK on success or an error code.
 */
int _chttpx_metrics_server_init(chttpx_serv_t* server, int enabled)
{
    if (!server)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (!enabled)
    {
        server->metrics_state = NULL;
        return cHTTPX_OK;
    }

    chttpx_metrics_state_t* state = calloc(1, sizeof(*state));
    if (!state)
        return cHTTPX_ERR_MEMORY;

#ifdef CHTTPX_PLATFORM_WINDOWS
    InitializeCriticalSection(&state->routes_mutex);
#else
    if (pthread_mutex_init(&state->routes_mutex, NULL) != 0)
    {
        free(state);
        return cHTTPX_ERR_STATE;
    }
#endif

    server->metrics_state = state;
    return cHTTPX_OK;
}

/**
 * Release per-server metrics state.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_server_cleanup(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;

#ifdef CHTTPX_PLATFORM_WINDOWS
    DeleteCriticalSection(&state->routes_mutex);
#else
    pthread_mutex_destroy(&state->routes_mutex);
#endif
    free(state->routes);
    free(state);
    server->metrics_state = NULL;
}

/**
 * Record an accepted network connection.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_connection_accepted(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (state)
        metric_add(&state->metrics.connections_accepted_total, 1);
}

/**
 * Record a rejected network connection.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_connection_rejected(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (state)
        metric_add(&state->metrics.connections_rejected_total, 1);
}

/**
 * Record a newly active network connection.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_connection_opened(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (state)
        metric_add(&state->metrics.connections_active, 1);
}

/**
 * Record a closed network connection.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_connection_closed(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (state)
        metric_decrement_nonzero(&state->metrics.connections_active);
}

/**
 * Record the start of an HTTP request.
 *
 * @param server HTTP server instance.
 * @param request_bytes Buffered request body size in bytes.
 */
void _chttpx_metrics_request_begin(chttpx_serv_t* server, size_t request_bytes)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return;

    metric_add(&state->metrics.requests_total, 1);
    metric_add(&state->metrics.requests_in_flight, 1);
    metric_add(&state->metrics.request_bytes_total, (uint64_t)request_bytes);
}

/**
 * Record completion of an HTTP request.
 *
 * Global counters use relaxed atomics. Only the route hash table takes a
 * mutex, so unrelated connection/request counters no longer contend on one
 * server-wide lock.
 *
 * @param server HTTP server instance.
 * @param method Registered request method.
 * @param route_template Registered route template.
 * @param status HTTP response status.
 * @param response_bytes Response body size in bytes.
 * @param duration_seconds Request duration in seconds.
 */
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

    metric_decrement_nonzero(&state->metrics.requests_in_flight);
    increment_status_class(state, status);
    metric_add(&state->metrics.response_bytes_total, (uint64_t)response_bytes);
    metric_add(&state->metrics.request_duration_count, 1);

    double duration_nanoseconds = duration_seconds * 1000000000.0;
    uint64_t duration_ns = duration_nanoseconds >= (double)UINT64_MAX
                               ? UINT64_MAX
                               : (uint64_t)duration_nanoseconds;
    metric_add(&state->request_duration_nanoseconds_total, duration_ns);

    for (size_t i = 0; i < CHTTPX_ARRAY_LEN(duration_bounds_seconds); i++)
    {
        if (duration_seconds <= duration_bounds_seconds[i])
            metric_add(&state->metrics.request_duration_buckets[i], 1);
    }
    metric_add(&state->metrics.request_duration_buckets[CHTTPX_METRICS_DURATION_BUCKETS - 1], 1);

    routes_lock(state);
    chttpx_route_metrics_entry_t* route_entry = route_metrics_entry(state, method, route_template);
    if (route_entry)
    {
        route_entry->requests_total++;
        route_entry->status_class[status_class_index(status)]++;
    }
    routes_unlock(state);
}

/**
 * Record an HTTP parser failure.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_parser_failure(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (state)
        metric_add(&state->metrics.parser_failures_total, 1);
}

/**
 * Record a request/connection timeout.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_timeout_failure(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (state)
        metric_add(&state->metrics.timeout_failures_total, 1);
}

/**
 * Record a rate-limit rejection.
 *
 * @param server HTTP server instance.
 */
void _chttpx_metrics_rate_limit_failure(chttpx_serv_t* server)
{
    chttpx_metrics_state_t* state = metrics_state(server);
    if (state)
        metric_add(&state->metrics.rate_limit_failures_total, 1);
}

/**
 * Copy a lock-free snapshot of global server metrics.
 *
 * Individual fields are atomically consistent. As with most monitoring
 * snapshots, fields may have advanced independently while the snapshot is
 * being copied.
 *
 * @param server HTTP server instance.
 * @param metrics Output snapshot.
 * @return cHTTPX_OK on success, cHTTPX_ERR_INVALID_ARGUMENT for bad input, or
 * cHTTPX_ERR_UNAVAILABLE when metrics are disabled.
 */
int cHTTPX_ServerMetrics(chttpx_serv_t* server, chttpx_metrics_t* metrics)
{
    if (!server || !metrics)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_metrics_state_t* state = metrics_state(server);
    if (!state)
        return cHTTPX_ERR_UNAVAILABLE;

    metrics_snapshot(state, metrics);
    return cHTTPX_OK;
}

/**
 * Copy the current bounded worker-pool metrics.
 *
 * @param server HTTP server instance.
 * @param metrics Output runtime metrics snapshot.
 * @return cHTTPX_OK on success or an error code when runtime metrics are unavailable.
 */
int cHTTPX_ServerRuntimeMetrics(chttpx_serv_t* server, chttpx_runtime_metrics_t* metrics)
{
    if (!server || !metrics)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_worker_stats_t worker_stats;
    int result = _chttpx_runtime_worker_stats(server, &worker_stats);
    if (result != cHTTPX_OK)
        return result;

    metrics->worker_queue_depth = (uint64_t)worker_stats.queue_depth;
    metrics->active_workers = (uint64_t)worker_stats.active_workers;
    metrics->rejected_jobs_total = worker_stats.rejected_jobs;
    metrics->completed_jobs_total = worker_stats.completed_jobs;
    metrics->queue_wait_nanoseconds_total = worker_stats.total_queue_wait_ns;
    return cHTTPX_OK;
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

static int prometheus_runtime_metrics(prometheus_buffer_t* buffer,
                                      const chttpx_runtime_metrics_t* metrics)
{
    if (!metrics)
        return 1;

    return prometheus_append(buffer, "# HELP libchttpx_worker_queue_depth Requests waiting for an application worker.\n"
                                     "# TYPE libchttpx_worker_queue_depth gauge\n") &&
           prometheus_appendf(buffer, "libchttpx_worker_queue_depth %llu\n",
                              (unsigned long long)metrics->worker_queue_depth) &&
           prometheus_append(buffer, "# HELP libchttpx_workers_active Application workers currently executing request jobs.\n"
                                     "# TYPE libchttpx_workers_active gauge\n") &&
           prometheus_appendf(buffer, "libchttpx_workers_active %llu\n",
                              (unsigned long long)metrics->active_workers) &&
           prometheus_append(buffer, "# TYPE libchttpx_worker_jobs_rejected_total counter\n") &&
           prometheus_appendf(buffer, "libchttpx_worker_jobs_rejected_total %llu\n",
                              (unsigned long long)metrics->rejected_jobs_total) &&
           prometheus_append(buffer, "# TYPE libchttpx_worker_jobs_completed_total counter\n") &&
           prometheus_appendf(buffer, "libchttpx_worker_jobs_completed_total %llu\n",
                              (unsigned long long)metrics->completed_jobs_total) &&
           prometheus_append(buffer, "# TYPE libchttpx_worker_queue_wait_seconds_total counter\n") &&
           prometheus_appendf(buffer, "libchttpx_worker_queue_wait_seconds_total %.9f\n",
                              (double)metrics->queue_wait_nanoseconds_total / 1000000000.0);
}

static int prometheus_route_metrics(prometheus_buffer_t* buffer,
                                    chttpx_metrics_state_t* state)
{
    if (!prometheus_append(buffer,
                           "# HELP libchttpx_route_requests_total HTTP requests grouped by registered route template, method and status class.\n"
                           "# TYPE libchttpx_route_requests_total counter\n"))
        return 0;

    for (size_t i = 0; i < state->routes_capacity; i++)
    {
        chttpx_route_metrics_entry_t* entry = &state->routes[i];
        if (!entry->method)
            continue;

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
    chttpx_runtime_metrics_t runtime_snapshot = {0};
    bool have_runtime_metrics = cHTTPX_ServerRuntimeMetrics(server, &runtime_snapshot) == cHTTPX_OK;

    chttpx_metrics_t snapshot;
    metrics_snapshot(state, &snapshot);

    int ok = prometheus_global_metrics(&output, &snapshot) &&
             (!have_runtime_metrics || prometheus_runtime_metrics(&output, &runtime_snapshot));

    if (ok)
    {
        routes_lock(state);
        ok = prometheus_route_metrics(&output, state);
        routes_unlock(state);
    }

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
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (!metrics_state(router->serv))
        return cHTTPX_ERR_UNAVAILABLE;

    chttpx_route_t* route = cHTTPX_Get(router, path, metrics_handler);
    if (!route)
        return cHTTPX_ERR_MEMORY;

    cHTTPX_RouteCompression(route, false);
    return cHTTPX_OK;
}
