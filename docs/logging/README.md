# Logging

Logging is configured per server.

## Logger callback

```c
static void logger(
    chttpx_log_level_t level,
    const char* request_id,
    const char* message,
    void* user_data)
{
    (void)level;
    (void)user_data;

    fprintf(
        stderr,
        "request_id=%s %s\n",
        request_id ? request_id : "-",
        message ? message : ""
    );
}
```

Register:

```c
cHTTPX_SetLogger(
    server,
    logger,
    NULL,
    cHTTPX_LOG_INFO
);
```

Levels:

- `cHTTPX_LOG_DEBUG`
- `cHTTPX_LOG_INFO`
- `cHTTPX_LOG_WARN`
- `cHTTPX_LOG_ERROR`
- `cHTTPX_LOG_OFF`

`user_data` is passed back to the callback and can point to an application logging sink.

## Request logging middleware

```c
cHTTPX_MiddlewareLogging(server);
```

This adds request/response logging to that server's middleware pipeline.

## Different loggers per server

```c
cHTTPX_SetLogger(
    public_server,
    public_logger,
    public_sink,
    cHTTPX_LOG_INFO
);

cHTTPX_SetLogger(
    admin_server,
    audit_logger,
    audit_sink,
    cHTTPX_LOG_DEBUG
);
```

## Request correlation

The callback receives `request_id`. With request IDs enabled, this allows correlation between incoming HTTP logs and outgoing App calls.

See [Request IDs and i18n](../i18n/README.md).

## Storage and rotation

libchttpx does not require a hard-coded `./logs` directory. Persistence, rotation, compression, indexing, retention, and centralized collection are application/backend responsibilities.

For production, connect the callback to your structured logger, stdout/stderr collector, journald, or centralized log system.
