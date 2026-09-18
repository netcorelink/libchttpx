# Server configuration and lifecycle

Every local server is created through `cHTTPX_AppServer()` with a `chttpx_config_t`.

## Defaults

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8080;

chttpx_serv_t* server =
    cHTTPX_AppServer(&app, "main", &config);
```

Current defaults:

| Field | Default |
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

## Custom limits

```c
chttpx_config_t config = cHTTPX_DefaultConfig();

config.port = 8080;
config.max_clients = 512;
config.max_header_size = 32 * 1024;
config.max_body_size = 4 * 1024 * 1024;
config.max_upload_size = 100ULL * 1024 * 1024;
config.read_timeout_sec = 20;
config.write_timeout_sec = 20;
```

Oversized normal bodies/uploads return `413`; oversized header blocks return `431`. Invalid framing returns `400`.

## Creation failures

`cHTTPX_AppServer()` returns `NULL` when server creation fails. The library does not call `exit()` for normal initialization failures.

## Error codes

| Code | Meaning |
| --- | --- |
| `CHTTPX_OK` | success |
| `CHTTPX_ERR_MEMORY` | allocation failure |
| `CHTTPX_ERR_SOCKET` | socket setup failure |
| `CHTTPX_ERR_BIND` | bind failed |
| `CHTTPX_ERR_LISTEN` | listen failed |
| `CHTTPX_ERR_INVALID_ARGUMENT` | invalid input |
| `CHTTPX_ERR_LIMIT` | limit exceeded |
| `CHTTPX_ERR_IO` | I/O failure |
| `CHTTPX_ERR_NOT_FOUND` | resource/target not found |
| `CHTTPX_ERR_PROTOCOL` | invalid protocol data |
| `CHTTPX_ERR_STATE` | invalid lifecycle state |
| `CHTTPX_ERR_UNAVAILABLE` | remote call unavailable |
| `CHTTPX_ERR_TIMEOUT` | remote call timed out |

## Lifecycle

```c
chttpx_app_t app;

if (cHTTPX_AppInit(&app) != CHTTPX_OK)
    return 1;

chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8080;

chttpx_serv_t* server =
    cHTTPX_AppServer(&app, "main", &config);

if (!server)
{
    cHTTPX_AppShutdown(&app);
    return 1;
}

/* configure routes/middleware */

int result = cHTTPX_AppRun(&app);

cHTTPX_AppShutdown(&app);
```

## Graceful shutdown

`cHTTPX_AppShutdown()` requests listener shutdown, closes listeners, drains active request handling, joins App listener threads, and frees server-owned configuration/state.

Trigger shutdown from an appropriate control thread for SIGINT/SIGTERM. Avoid complex work directly in an async-signal handler.

## Thread-safety model

- client count is updated atomically;
- configure routes/middleware before `AppStart/AppRun` and treat them as read-only while serving;
- a request and its request allocator should remain on its worker thread;
- application-owned shared state still needs its own synchronization.

## HTTP model

Current server behavior is HTTP/1.1 with one request per connection and explicit `Connection: close`. Fixed `Content-Length` and chunked request bodies are supported.
