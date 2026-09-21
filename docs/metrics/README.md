# Metrics and Prometheus

libchttpx can collect lightweight per-server runtime metrics and expose them in Prometheus text format. Metrics are disabled by default, so servers that do not need monitoring do not allocate metrics state or take the metrics lock on the request path.

## Enable metrics

Metrics are enabled in the server configuration:

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8080;
config.metrics_enabled = true;

chttpx_serv_t *server = cHTTPX_AppServer(&app, "api", &config);
```

The configuration must be set before the server is created.

## Read a snapshot

`cHTTPX_ServerMetrics()` copies a consistent snapshot under the server metrics lock:

```c
chttpx_metrics_t metrics;

if (cHTTPX_ServerMetrics(server, &metrics) == cHTTPX_OK) {
    printf("requests: %llu\n",
           (unsigned long long)metrics.requests_total);
    printf("in flight: %llu\n",
           (unsigned long long)metrics.requests_in_flight);
}
```

This API is independent of Prometheus, so the same snapshot can be sent to another telemetry backend.

The snapshot includes:

- total requests and requests currently in flight;
- active, accepted and rejected network connections;
- response counts for 1xx through 5xx status classes;
- request-body and response-body byte counters;
- parser, timeout and rate-limit failure counters;
- request-duration count, sum and cumulative histogram buckets.

The duration buckets are:

```text
<= 1 ms
<= 5 ms
<= 10 ms
<= 25 ms
<= 50 ms
<= 100 ms
<= 500 ms
<= 1000 ms
+Inf
```

## Prometheus endpoint

Register the exporter with one call:

```c
chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");

cHTTPX_Get(&router, "/health", health_handler);

if (cHTTPX_MetricsRoute(&router, "/metrics") != cHTTPX_OK) {
    /* metrics are disabled or the route could not be registered */
}
```

Then scrape:

```bash
curl http://127.0.0.1:8080/metrics
```

Example output:

```text
# TYPE libchttpx_requests_total counter
libchttpx_requests_total 128

# TYPE libchttpx_requests_in_flight gauge
libchttpx_requests_in_flight 1

# TYPE libchttpx_responses_total counter
libchttpx_responses_total{class="2xx"} 120
libchttpx_responses_total{class="4xx"} 8

# TYPE libchttpx_request_duration_seconds histogram
libchttpx_request_duration_seconds_bucket{le="0.01"} 104
libchttpx_request_duration_seconds_bucket{le="+Inf"} 127
```

The metrics response itself is not gzip-compressed. This keeps the exporter simple and avoids spending CPU compressing small monitoring payloads.

## Route labels and cardinality

Route-level request counters use the **registered route template**, never the raw request URL.

For this route:

```c
cHTTPX_Get(&router, "/users/{id}", user_handler);
```

requests such as `/users/10`, `/users/55` and `/users/9999` are exported under one bounded label:

```text
libchttpx_route_requests_total{method="GET",route="/users/{id}",class="2xx"} 3
```

Request IDs, client IPs, query values and raw paths are never used as Prometheus labels.

## Failure counters

The built-in counters distinguish several common failure sources:

- `parser_failures_total` — malformed requests, parser errors and oversized request headers;
- `timeout_failures_total` — socket read timeouts;
- `rate_limit_failures_total` — requests rejected by the built-in rate limiter;
- `connections_rejected_total` — accepted sockets rejected before normal request processing, including overload and TLS-handshake rejection.

Application-specific failures should still be represented by normal HTTP status codes or custom application metrics.

## Overhead

When metrics are disabled, the server keeps `metrics_state == NULL`; the lifecycle hooks return immediately.

When enabled, request updates use one short per-server mutex section for request start and one for request completion. The exporter and snapshot API use the same lock to provide consistent values.

The concurrency test also contains a small snapshot micro-benchmark:

```bash
make test-metrics
```

It prints:

```text
metrics snapshot benchmark: ... ns/op
```

The number is intentionally measured at test time rather than documented as a fixed performance claim because it depends on CPU, compiler and operating system.

## Complete example

A runnable example is available in `example/metrics.c`:

```bash
make .build/example-metrics
./.build/example-metrics
```

Then open `/hello` and `/metrics` on port 8080.

## Current scope

Metrics are per HTTP server. The current snapshot/exporter covers HTTP request and network-connection lifecycle metrics. WebSocket-specific connection counters can be added when the experimental WebSocket API is tied to the App-managed server lifecycle.
