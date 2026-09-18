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
| `max_clients` | 255 |
| `read_timeout_sec` | 30 |
| `write_timeout_sec` | 30 |
| `idle_timeout_sec` | 60 |
| `max_body_size` | 10 MiB |
| `max_upload_size` | 500 MiB |
| `max_header_size` | 16383 bytes |
| `request_id_enabled` | true |
| `default_language` | `"en"` |
| `log_level` | `CHTTPX_LOG_INFO` |

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

## Ошибки

Основные `chttpx_error_t`:

| Код | Значение |
| --- | --- |
| `CHTTPX_OK` | успех |
| `CHTTPX_ERR_MEMORY` | allocation failure |
| `CHTTPX_ERR_SOCKET` | socket error |
| `CHTTPX_ERR_BIND` | bind failed |
| `CHTTPX_ERR_LISTEN` | listen failed |
| `CHTTPX_ERR_INVALID_ARGUMENT` | неверные аргументы |
| `CHTTPX_ERR_LIMIT` | превышен limit |
| `CHTTPX_ERR_IO` | I/O error |
| `CHTTPX_ERR_NOT_FOUND` | target/resource не найден |
| `CHTTPX_ERR_PROTOCOL` | protocol error |
| `CHTTPX_ERR_STATE` | неверное состояние lifecycle |
| `CHTTPX_ERR_UNAVAILABLE` | remote server недоступен |
| `CHTTPX_ERR_TIMEOUT` | remote Call timeout |

`cHTTPX_AppServer()` возвращает pointer или `NULL`. Библиотека не вызывает `exit()` при обычной ошибке инициализации.

## Lifecycle

```c
chttpx_app_t app;

if (cHTTPX_AppInit(&app) != CHTTPX_OK)
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

## Thread safety

Routes/middleware настраивайте до `AppStart/AppRun`. Во время работы они должны рассматриваться как read-only.

Один request и его allocator должны использоваться одним worker thread. Shared state приложения синхронизируется самим приложением.

## HTTP model

Текущая реализация работает по HTTP/1.1 с одним request на connection и явным `Connection: close`. Поддерживаются `Content-Length` и chunked request bodies.
