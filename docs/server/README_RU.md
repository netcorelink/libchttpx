# Конфигурация server и lifecycle

Каждый локальный server создаётся через `cHTTPX_AppServer()` и получает `chttpx_config_t`.

## Базовый config

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8080;

chttpx_serv_t* server =
    cHTTPX_AppServer(&app, "main", &config);
```

Основные defaults:

| Поле | Default |
| --- | ---: |
| `port` | 8080 |
| `network_mode` | `cHTTPX_NETWORK_DUAL` |
| `max_clients` | 255 |
| `read_timeout_sec` | 30 |
| `write_timeout_sec` | 30 |
| `idle_timeout_sec` | 60 |
| `max_body_size` | 10 MiB |
| `max_upload_size` | 500 MiB |
| `max_header_size` | 16383 bytes |
| `request_id_enabled` | true |
| `metrics_enabled` | false |
| `default_language` | `"en"` |
| `log_level` | `cHTTPX_LOG_INFO` |

## Сетевой режим

По умолчанию server работает в dual-stack режиме. Один IPv6 listener, привязанный к `::`, принимает как обычные IPv6 connections, так и IPv4 connections через IPv4-mapped IPv6 addresses.

```c
chttpx_config_t config = cHTTPX_DefaultConfig();

/* Default: IPv4 + IPv6. */
config.network_mode = cHTTPX_NETWORK_DUAL;

/* Только IPv4. */
// config.network_mode = cHTTPX_NETWORK_IPV4;

/* Только IPv6. */
// config.network_mode = cHTTPX_NETWORK_IPV6;
```

При `cHTTPX_NETWORK_DUAL` один server доступен и через `http://127.0.0.1:8080`, и через `http://[::1]:8080`. IPv4-mapped адреса нормализуются перед записью в `req->client_ip`, поэтому IPv4 client будет иметь адрес `127.0.0.1`, а не `::ffff:127.0.0.1`.

## Собственные лимиты

```c
config.max_clients = 512;
config.max_header_size = 32 * 1024;
config.max_body_size = 4 * 1024 * 1024;
config.max_upload_size = 100ULL * 1024 * 1024;
config.read_timeout_sec = 20;
config.write_timeout_sec = 20;
```

Превышение body/upload limit обычно приводит к `413 Payload Too Large`, header limit — к `431 Request Header Fields Too Large`.

## Встроенные metrics

По умолчанию metrics выключены. Включаются до создания server:

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.metrics_enabled = true;

chttpx_serv_t *server =
    cHTTPX_AppServer(&app, "main", &config);
```

Snapshot API и Prometheus endpoint описаны в [Metrics и Prometheus](../metrics/README_RU.md).

## Ошибки

Основные `chttpx_error_t`:

| Код | Значение |
| --- | --- |
| `cHTTPX_OK` | успех |
| `cHTTPX_ERR_MEMORY` | allocation failure |
| `cHTTPX_ERR_SOCKET` | socket error |
| `cHTTPX_ERR_BIND` | bind failed |
| `cHTTPX_ERR_LISTEN` | listen failed |
| `cHTTPX_ERR_INVALID_ARGUMENT` | неверные аргументы |
| `cHTTPX_ERR_LIMIT` | превышен limit |
| `cHTTPX_ERR_IO` | I/O error |
| `cHTTPX_ERR_NOT_FOUND` | target/resource не найден |
| `cHTTPX_ERR_PROTOCOL` | protocol error |
| `cHTTPX_ERR_STATE` | неверное состояние lifecycle |
| `cHTTPX_ERR_UNAVAILABLE` | remote server недоступен |
| `cHTTPX_ERR_TIMEOUT` | remote Call timeout |

`cHTTPX_AppServer()` возвращает pointer или `NULL`. Библиотека не вызывает `exit()` при обычной ошибке инициализации.

## Lifecycle

```c
chttpx_app_t app;

if (cHTTPX_AppInit(&app) != cHTTPX_OK)
    return 1;

chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8080;

chttpx_serv_t* server =
    cHTTPX_AppServer(&app, "main", &config);

/* routes / middleware / CORS / logger */

int result = cHTTPX_AppRun(&app);

cHTTPX_AppShutdown(&app);
```

## Graceful shutdown

`cHTTPX_AppShutdown()` прекращает accept, закрывает listeners, ждёт активные requests, освобождает routes, CORS, middleware state, server objects и remote registrations.

## Event-driven runtime и worker pool

Sockets работают в non-blocking режиме и остаются на server event loop, поэтому worker thread больше не занят ожиданием сетевого I/O. На Linux используется `epoll`, на macOS/BSD — `kqueue`, на Windows — non-blocking backend на `WSAPoll`.

Connection попадает во внутренний фиксированный пул из **32 worker threads** только после того, как HTTP request полностью принят. Worker выполняет middleware, routing и application handler и не вызывает `recv()` или `send()`. Готовый сериализованный response возвращается в event loop и отправляется только когда socket готов к записи.

Количество workers намеренно не добавлено в `chttpx_config_t` и не может изменяться кодом приложения. `max_clients` ограничивает число accepted/in-flight connections, а не количество OS threads. Большие multipart/chunked uploads во время приёма остаются disk-backed.

## Thread safety

Routes/middleware настраивайте до `AppStart/AppRun`. Во время работы они должны рассматриваться как read-only.

Один request и его allocator должны использоваться одним worker thread. Shared state приложения синхронизируется самим приложением.

## HTTP model

Текущая реализация работает по HTTP/2. Для соединений без TLS используется h2c prior knowledge, а TLS-серверы согласовывают `h2` через ALPN. Несколько HTTP/2 stream могут использовать одно соединение, при этом публичный App/router/handler API не меняется.
