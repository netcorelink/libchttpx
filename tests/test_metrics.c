#include "libchttpx.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WORKER_COUNT 6
#define REQUESTS_PER_WORKER 30
#define RESPONSE_CAPACITY 65536

typedef struct
{
    uint16_t port;
    int count;
} worker_ctx_t;

typedef struct
{
    chttpx_serv_t* server;
    volatile int stop;
} snapshot_ctx_t;

typedef struct
{
    char bytes[RESPONSE_CAPACITY];
    size_t size;
    size_t header_size;
} http_response_t;

static void user_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* id = cHTTPX_Param(req, "id");
    assert(id && *id);
    static const unsigned char body[] = "ok";
    *res = cHTTPX_ResBinary(cHTTPX_StatusOK, "text/plain", body, sizeof(body) - 1);
}

static void wait_until_listening(chttpx_serv_t* server)
{
    for (int i = 0; i < 5000 && !__atomic_load_n(&server->listening, __ATOMIC_ACQUIRE); i++)
        usleep(1000);

    assert(__atomic_load_n(&server->listening, __ATOMIC_ACQUIRE));
}

static const char* response_body(http_response_t* response)
{
    assert(response && response->header_size <= response->size);
    return response->bytes + response->header_size;
}

static http_response_t exchange(uint16_t port, const char* path)
{
    http_response_t response;
    memset(&response, 0, sizeof(response));

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    assert(connect(fd, (struct sockaddr*)&address, sizeof(address)) == 0);

    char request[1024];
    int length = snprintf(request,
                          sizeof(request),
                          "GET %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
                          path);
    assert(length > 0 && (size_t)length < sizeof(request));
    assert(cHTTPX_SendAll(fd, request, (size_t)length) == cHTTPX_OK);
    shutdown(fd, SHUT_WR);

    while (response.size + 1 < sizeof(response.bytes))
    {
        int received = recv(fd,
                            response.bytes + response.size,
                            sizeof(response.bytes) - response.size - 1,
                            0);
        if (received <= 0)
            break;
        response.size += (size_t)received;
    }
    response.bytes[response.size] = '\0';
    close(fd);

    char* delimiter = strstr(response.bytes, "\r\n\r\n");
    assert(delimiter);
    response.header_size = (size_t)(delimiter - response.bytes) + 4;
    return response;
}

static void* worker(void* data)
{
    worker_ctx_t* ctx = data;
    for (int i = 0; i < ctx->count; i++)
    {
        char path[64];
        snprintf(path, sizeof(path), "/users/%d", i);
        http_response_t response = exchange(ctx->port, path);
        assert(strncmp(response.bytes, "HTTP/1.1 200 OK", 15) == 0);
    }
    return NULL;
}

static void* snapshot_reader(void* data)
{
    snapshot_ctx_t* ctx = data;
    while (!__atomic_load_n(&ctx->stop, __ATOMIC_ACQUIRE))
    {
        chttpx_metrics_t metrics;
        assert(cHTTPX_ServerMetrics(ctx->server, &metrics) == cHTTPX_OK);
        assert(metrics.requests_in_flight <= metrics.requests_total);
    }
    return NULL;
}

static double diff_seconds(struct timespec start, struct timespec end)
{
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

int main(void)
{
    chttpx_app_t app;
    assert(cHTTPX_AppInit(&app) == cHTTPX_OK);

    chttpx_config_t disabled_config = cHTTPX_DefaultConfig();
    disabled_config.port = 0;
    disabled_config.network_mode = cHTTPX_NETWORK_IPV4;

    chttpx_serv_t* disabled =
        cHTTPX_AppServer(&app, "metrics-disabled", &disabled_config);
    assert(disabled);

    chttpx_metrics_t disabled_metrics;
    assert(cHTTPX_ServerMetrics(disabled, &disabled_metrics) == cHTTPX_ERR_UNAVAILABLE);

    chttpx_runtime_metrics_t disabled_runtime_metrics;
    assert(cHTTPX_ServerRuntimeMetrics(disabled, &disabled_runtime_metrics) == cHTTPX_OK);

    chttpx_router_t disabled_router = cHTTPX_RoutePathPrefix(disabled, "");
    assert(cHTTPX_MetricsRoute(&disabled_router, "/metrics") == cHTTPX_ERR_UNAVAILABLE);

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 0;
    config.network_mode = cHTTPX_NETWORK_IPV4;
    config.metrics_enabled = true;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "metrics", &config);
    assert(server);

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    assert(cHTTPX_Get(&router, "/users/{id}", user_handler));
    assert(cHTTPX_MetricsRoute(&router, "/metrics") == cHTTPX_OK);

    assert(cHTTPX_AppStart(&app) == cHTTPX_OK);
    wait_until_listening(server);

    snapshot_ctx_t snapshot_ctx = {.server = server, .stop = 0};
    pthread_t snapshot_thread;
    assert(pthread_create(&snapshot_thread, NULL, snapshot_reader, &snapshot_ctx) == 0);

    pthread_t workers[WORKER_COUNT];
    worker_ctx_t worker_ctx = {
        .port = server->port,
        .count = REQUESTS_PER_WORKER,
    };

    for (size_t i = 0; i < WORKER_COUNT; i++)
        assert(pthread_create(&workers[i], NULL, worker, &worker_ctx) == 0);

    for (size_t i = 0; i < WORKER_COUNT; i++)
        assert(pthread_join(workers[i], NULL) == 0);

    __atomic_store_n(&snapshot_ctx.stop, 1, __ATOMIC_RELEASE);
    assert(pthread_join(snapshot_thread, NULL) == 0);

    const uint64_t expected_requests =
        (uint64_t)WORKER_COUNT * (uint64_t)REQUESTS_PER_WORKER;

    chttpx_metrics_t metrics;
    assert(cHTTPX_ServerMetrics(server, &metrics) == cHTTPX_OK);
    assert(metrics.requests_total == expected_requests);
    assert(metrics.requests_in_flight == 0);
    assert(metrics.responses_2xx_total == expected_requests);
    assert(metrics.request_duration_count == expected_requests);
    assert(metrics.request_duration_buckets[CHTTPX_METRICS_DURATION_BUCKETS - 1] ==
           expected_requests);
    assert(metrics.connections_accepted_total >= expected_requests);
    assert(metrics.response_bytes_total == expected_requests * 2);

    chttpx_runtime_metrics_t runtime_metrics;
    assert(cHTTPX_ServerRuntimeMetrics(server, &runtime_metrics) == cHTTPX_OK);
    assert(runtime_metrics.completed_jobs_total >= expected_requests);
    assert(runtime_metrics.rejected_jobs_total == 0);

    http_response_t scrape = exchange(server->port, "/metrics");
    assert(strncmp(scrape.bytes, "HTTP/1.1 200 OK", 15) == 0);

    const char* body = response_body(&scrape);
    assert(strstr(body, "# TYPE libchttpx_requests_total counter"));
    assert(strstr(body, "libchttpx_route_requests_total{method=\"GET\",route=\"/users/{id}\",class=\"2xx\"}"));
    assert(strstr(body, "libchttpx_request_duration_seconds_bucket{le=\"+Inf\"}"));
    assert(strstr(body, "libchttpx_worker_queue_depth"));
    assert(strstr(body, "libchttpx_workers_active"));
    assert(strstr(body, "libchttpx_worker_jobs_completed_total"));
    assert(strstr(body, "/users/0") == NULL);
    assert(strstr(body, "/users/29") == NULL);

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < 100000; i++)
        assert(cHTTPX_ServerMetrics(server, &metrics) == cHTTPX_OK);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double ns_per_snapshot = diff_seconds(start, end) * 1e9 / 100000.0;
    printf("metrics snapshot benchmark: %.1f ns/op\n", ns_per_snapshot);

    cHTTPX_AppShutdown(&app);
    puts("metrics tests passed");
    return 0;
}
