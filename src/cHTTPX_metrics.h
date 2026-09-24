/**
 * Copyright (c) 2026 netcorelink
 *
 * Lightweight per-server metrics and Prometheus exporter.
 */

#ifndef CHTTPX_METRICS_H
#define CHTTPX_METRICS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <stdint.h>

#define CHTTPX_METRICS_DURATION_BUCKETS 9

    struct chttpx_serv;
    struct chttpx_router;

    /**
     * Thread-safe metrics snapshot.
     *
     * request_duration_buckets contains cumulative counts for:
     * <=1ms, <=5ms, <=10ms, <=25ms, <=50ms, <=100ms, <=500ms,
     * <=1000ms and +Inf.
     */
    typedef struct
    {
        uint64_t requests_total;
        uint64_t requests_in_flight;

        uint64_t connections_active;
        uint64_t connections_accepted_total;
        uint64_t connections_rejected_total;

        uint64_t responses_1xx_total;
        uint64_t responses_2xx_total;
        uint64_t responses_3xx_total;
        uint64_t responses_4xx_total;
        uint64_t responses_5xx_total;

        uint64_t request_bytes_total;
        uint64_t response_bytes_total;

        uint64_t parser_failures_total;
        uint64_t timeout_failures_total;
        uint64_t rate_limit_failures_total;

        uint64_t request_duration_count;
        double request_duration_seconds_sum;
        uint64_t request_duration_buckets[CHTTPX_METRICS_DURATION_BUCKETS];
    } chttpx_metrics_t;

    /**
     * Snapshot of the bounded application worker pool.
     *
     * The pool is internal to the server runtime and uses a fixed 32 workers.
     * Queue depth and active_workers are gauges; the remaining fields are
     * monotonic counters for the lifetime of the server runtime.
     */
    typedef struct
    {
        uint64_t worker_queue_depth;
        uint64_t active_workers;
        uint64_t rejected_jobs_total;
        uint64_t completed_jobs_total;
        uint64_t queue_wait_nanoseconds_total;
    } chttpx_runtime_metrics_t;


    /**
     * Copy the current server metrics into a caller-owned snapshot.
     *
     * Global counters are read atomically. Individual fields may advance while
     * the snapshot is copied, which is expected for monitoring data.
     *
     * @param server Server whose metrics should be read.
     * @param metrics Output structure populated on success.
     * @return cHTTPX_OK on success, cHTTPX_ERR_INVALID_ARGUMENT for invalid
     * input, or cHTTPX_ERR_UNAVAILABLE when metrics are disabled.
     */
    int cHTTPX_ServerMetrics(struct chttpx_serv* server, chttpx_metrics_t* metrics);

    /**
     * Copy bounded worker-pool runtime metrics.
     *
     * @param server Server whose worker runtime should be inspected.
     * @param metrics Output runtime metrics snapshot.
     * @return cHTTPX_OK on success or cHTTPX_ERR_UNAVAILABLE when runtime
     * metrics are not available.
     */
    int cHTTPX_ServerRuntimeMetrics(struct chttpx_serv* server, chttpx_runtime_metrics_t* metrics);

    /**
     * Register a Prometheus text exposition endpoint.
     *
     * Metrics must be enabled in chttpx_config_t before the server is created.
     *
     * @param router Router that owns the metrics endpoint.
     * @param path Route path used for the Prometheus endpoint.
     * @return cHTTPX_OK on success, cHTTPX_ERR_INVALID_ARGUMENT for invalid
     * input, cHTTPX_ERR_UNAVAILABLE when metrics are disabled, or
     * cHTTPX_ERR_MEMORY when route registration fails.
     */
    int cHTTPX_MetricsRoute(struct chttpx_router* router, const char* path);

    /* Internal lifecycle/update hooks. */
    int _chttpx_metrics_server_init(struct chttpx_serv* server, int enabled);
    void _chttpx_metrics_server_cleanup(struct chttpx_serv* server);
    void _chttpx_metrics_connection_accepted(struct chttpx_serv* server);
    void _chttpx_metrics_connection_rejected(struct chttpx_serv* server);
    void _chttpx_metrics_connection_opened(struct chttpx_serv* server);
    void _chttpx_metrics_connection_closed(struct chttpx_serv* server);
    void _chttpx_metrics_request_begin(struct chttpx_serv* server, size_t request_bytes);
    void _chttpx_metrics_request_end(struct chttpx_serv* server, const char* method, const char* route_template, int status, size_t response_bytes, double duration_seconds);
    void _chttpx_metrics_parser_failure(struct chttpx_serv* server);
    void _chttpx_metrics_timeout_failure(struct chttpx_serv* server);
    void _chttpx_metrics_rate_limit_failure(struct chttpx_serv* server);

#ifdef __cplusplus
}
#endif

#endif
