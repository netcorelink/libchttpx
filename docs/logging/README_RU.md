# Logging

Logging настраивается отдельно для server.

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

Регистрация:

```c
cHTTPX_SetLogger(
    server,
    logger,
    NULL,
    CHTTPX_LOG_INFO
);
```

Levels:

- `CHTTPX_LOG_DEBUG`
- `CHTTPX_LOG_INFO`
- `CHTTPX_LOG_WARN`
- `CHTTPX_LOG_ERROR`
- `CHTTPX_LOG_OFF`

`user_data` можно использовать для передачи собственного logger/sink.

## Request logging middleware

```c
cHTTPX_MiddlewareLogging(server);
```

## Разные logger для разных server

```c
cHTTPX_SetLogger(
    public_server,
    public_logger,
    public_sink,
    CHTTPX_LOG_INFO
);

cHTTPX_SetLogger(
    admin_server,
    audit_logger,
    audit_sink,
    CHTTPX_LOG_DEBUG
);
```

Logger callback получает request ID, поэтому его удобно использовать для correlation.

Библиотека не навязывает `./logs`. Rotation, compression, retention и storage реализуются application logging backend.
