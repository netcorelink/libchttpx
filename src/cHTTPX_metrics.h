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
     * Copy a consistent metrics snapshot.
     *
     * Returns CHTTPX_OK on success or CHTTPX_ERR_UNAVAILABLE when metrics are
     * disabled for the server.
     */
    int cHTTPX_ServerMetrics(struct chttpx_serv* server, chttpx_metrics_t* metrics);

    /**
     * Register a Prometheus text exposition endpoint on the supplied router.
     *
     * Metrics must be enabled in chttpx_config_t before the server is created.
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
    void _chttpx_metrics_request_end(struct chttpx_serv* server,
                                     const char* method,
                                     const char* route_template,
                                     int status,
                                     size_t response_bytes,
                                     double duration_seconds);
    void _chttpx_metrics_parser_failure(struct chttpx_serv* server);
    void _chttpx_metrics_timeout_failure(struct chttpx_serv* server);
    void _chttpx_metrics_rate_limit_failure(struct chttpx_serv* server);

#ifdef __cplusplus
}
#endif

#endif
