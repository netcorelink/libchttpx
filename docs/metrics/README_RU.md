# Метрики и Prometheus

libchttpx умеет собирать лёгкие runtime-метрики отдельно для каждого сервера и отдавать их в Prometheus text format. По умолчанию метрики выключены: если monitoring не нужен, сервер не выделяет metrics state и не берёт metrics mutex на пути обработки запроса.

## Включение метрик

Метрики включаются в конфигурации сервера:

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8080;
config.metrics_enabled = true;

chttpx_serv_t *server = cHTTPX_AppServer(&app, "api", &config);
```

Флаг нужно задать до создания сервера.

## Получение snapshot

`cHTTPX_ServerMetrics()` thread-safe копирует согласованный snapshot:

```c
chttpx_metrics_t metrics;

if (cHTTPX_ServerMetrics(server, &metrics) == CHTTPX_OK) {
    printf("requests: %llu\n",
           (unsigned long long)metrics.requests_total);
    printf("in flight: %llu\n",
           (unsigned long long)metrics.requests_in_flight);
}
```

API не зависит от Prometheus, поэтому snapshot можно отправить в любую другую telemetry-систему.

Snapshot содержит:

- общее число запросов и requests in flight;
- active, accepted и rejected network connections;
- количество ответов по классам 1xx–5xx;
- байты request body и response body;
- ошибки parser, timeout и rate limiter;
- count, sum и cumulative histogram времени обработки запросов.

Buckets duration:

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

Exporter регистрируется одной функцией:

```c
chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");

cHTTPX_Get(&router, "/health", health_handler);

if (cHTTPX_MetricsRoute(&router, "/metrics") != CHTTPX_OK) {
    /* metrics выключены или route не удалось зарегистрировать */
}
```

Проверка:

```bash
curl http://127.0.0.1:8080/metrics
```

Пример:

```text
# TYPE libchttpx_requests_total counter
libchttpx_requests_total 128

# TYPE libchttpx_requests_in_flight gauge
libchttpx_requests_in_flight 1

# TYPE libchttpx_responses_total counter
libchttpx_responses_total{class="2xx"} 120
libchttpx_responses_total{class="4xx"} 8
```

Ответ `/metrics` специально не gzip-сжимается, чтобы не тратить CPU на небольшой monitoring payload.

## Route labels и cardinality

Route-метрики используют **шаблон зарегистрированного route**, а не фактический URL.

Например:

```c
cHTTPX_Get(&router, "/users/{id}", user_handler);
```

Запросы `/users/10`, `/users/55` и `/users/9999` попадут в один label:

```text
libchttpx_route_requests_total{method="GET",route="/users/{id}",class="2xx"} 3
```

Request ID, IP клиента, query values и реальные URL не используются как Prometheus labels. Поэтому количество series не растёт от каждого нового user id.

## Счётчики ошибок

Встроенные counters:

- `parser_failures_total` — malformed request, parser errors и слишком большие headers;
- `timeout_failures_total` — timeout чтения socket;
- `rate_limit_failures_total` — запросы, отклонённые встроенным rate limiter;
- `connections_rejected_total` — соединения, отклонённые до обычной обработки, в том числе overload и ошибка TLS handshake.

Ошибки бизнес-логики приложения по-прежнему лучше отражать HTTP status codes или своими metrics.

## Overhead

При выключенных метриках `metrics_state == NULL`, поэтому internal hooks сразу возвращаются.

При включённых метриках на один запрос приходится короткая mutex-секция в начале и одна после формирования ответа. Snapshot и exporter используют тот же mutex, чтобы значения были согласованными.

В concurrency test встроен небольшой micro-benchmark snapshot API:

```bash
make test-metrics
```

Он выводит:

```text
metrics snapshot benchmark: ... ns/op
```

Число не фиксируется в документации, потому что зависит от CPU, compiler и ОС.

## Полный пример

Рабочий пример находится в `example/metrics.c`:

```bash
make .build/example-metrics
./.build/example-metrics
```

После запуска доступны `/hello` и `/metrics` на порту 8080.

## Текущие границы

Метрики собираются отдельно для каждого HTTP server. Сейчас snapshot/exporter покрывают HTTP requests и network connections. Отдельные WebSocket counters можно добавить после привязки экспериментального WebSocket API к lifecycle App-managed server.
