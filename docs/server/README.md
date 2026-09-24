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

## Network mode

Servers use dual-stack networking by default. A single IPv6 listener bound to `::` accepts both native IPv6 clients and IPv4 clients through IPv4-mapped IPv6 addresses.

```c
chttpx_config_t config = cHTTPX_DefaultConfig();

/* Default: accept IPv4 and IPv6. */
config.network_mode = cHTTPX_NETWORK_DUAL;

/* IPv4 only. */
// config.network_mode = cHTTPX_NETWORK_IPV4;

/* IPv6 only. */
// config.network_mode = cHTTPX_NETWORK_IPV6;
```

With `cHTTPX_NETWORK_DUAL`, both `http://127.0.0.1:8080` and `http://[::1]:8080` reach the same server. IPv4-mapped peer addresses are normalized before they are exposed through `req->client_ip`, so an IPv4 client is reported as `127.0.0.1` rather than `::ffff:127.0.0.1`.

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

## Built-in metrics

Metrics are disabled by default. Enable them before creating the server:

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.metrics_enabled = true;

chttpx_serv_t *server =
    cHTTPX_AppServer(&app, "main", &config);
```

See [Metrics and Prometheus](../metrics/README.md) for snapshots and the `/metrics` exporter.

## Creation failures

`cHTTPX_AppServer()` returns `NULL` when server creation fails. The library does not call `exit()` for normal initialization failures.

## Error codes

| Code | Meaning |
| --- | --- |
| `cHTTPX_OK` | success |
| `cHTTPX_ERR_MEMORY` | allocation failure |
| `cHTTPX_ERR_SOCKET` | socket setup failure |
| `cHTTPX_ERR_BIND` | bind failed |
| `cHTTPX_ERR_LISTEN` | listen failed |
| `cHTTPX_ERR_INVALID_ARGUMENT` | invalid input |
| `cHTTPX_ERR_LIMIT` | limit exceeded |
| `cHTTPX_ERR_IO` | I/O failure |
| `cHTTPX_ERR_NOT_FOUND` | resource/target not found |
| `cHTTPX_ERR_PROTOCOL` | invalid protocol data |
| `cHTTPX_ERR_STATE` | invalid lifecycle state |
| `cHTTPX_ERR_UNAVAILABLE` | remote call unavailable |
| `cHTTPX_ERR_TIMEOUT` | remote call timed out |

## Lifecycle

```c
chttpx_app_t app;

if (cHTTPX_AppInit(&app) != cHTTPX_OK)
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

## Event-driven runtime and worker pool

Sockets are non-blocking and stay on the server event loop instead of occupying worker threads while waiting for network I/O. Linux uses `epoll`, macOS/BSD uses `kqueue`, and Windows uses the non-blocking `WSAPoll` backend.

A connection is submitted to the internal fixed pool of **32 worker threads** only after the complete HTTP request has been received. Workers execute middleware, routing and the application handler; they do not call `recv()` or `send()`. The serialized response is returned to the event loop and written when the socket is ready.

The worker count is intentionally not part of `chttpx_config_t` and cannot be changed by application code. `max_clients` limits accepted/in-flight connections, not the number of OS threads. Large multipart/chunked upload bodies remain disk-backed while they are received.

## Thread-safety model

- client count is updated atomically;
- configure routes/middleware before `AppStart/AppRun` and treat them as read-only while serving;
- a request and its request allocator should remain on its worker thread;
- application-owned shared state still needs its own synchronization.

## HTTP model

Current server behavior is HTTP/2. Cleartext servers use h2c prior knowledge; TLS servers negotiate `h2` with ALPN. Multiple HTTP/2 streams may share one connection, while the public App/router/handler API remains unchanged.
