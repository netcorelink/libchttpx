# App runtime and server-to-server calls

`cHTTPX_App` is the top-level runtime. Local HTTP servers and remote server registrations belong to an App.

## Initialize

```c
chttpx_app_t app;

if (cHTTPX_AppInit(&app) != cHTTPX_OK)
    return 1;
```

## Add local servers

Each server has its own config, routes, middleware, CORS state, logger, limits, and listener.

```c
chttpx_config_t public_config = cHTTPX_DefaultConfig();
public_config.port = 8080;

chttpx_config_t admin_config = cHTTPX_DefaultConfig();
admin_config.port = 9090;

chttpx_serv_t* public_server =
    cHTTPX_AppServer(&app, "public", &public_config);

chttpx_serv_t* admin_server =
    cHTTPX_AppServer(&app, "admin", &admin_config);

if (!public_server || !admin_server)
{
    cHTTPX_AppShutdown(&app);
    return 1;
}
```

Names must be unique across local and remote registrations.

## Start, wait, run

```c
if (cHTTPX_AppStart(&app) != cHTTPX_OK)
{
    cHTTPX_AppShutdown(&app);
    return 1;
}

cHTTPX_AppWait(&app);
cHTTPX_AppShutdown(&app);
```

`cHTTPX_AppStart()` starts every local listener in its own thread. `cHTTPX_AppRun()` is start + wait.

Configure routes and middleware before starting.

## Register a remote server

```c
int result = cHTTPX_AppRemote(
    &app,
    "payments",
    "http://payment-server:8090"
);
```

The built-in transport accepts both `http://` and `https://` URLs. HTTPS is available in `TLS=1` builds and verifies the peer certificate and hostname by default. Use `cHTTPX_AppRemoteEx()` for a custom CA or client certificate. See [Native TLS / HTTPS](../tls/README.md). In Docker Compose, the host can be another service name on the same network.

## Call local or remote servers

```c
int result = cHTTPX_Call(
    req,
    "payments",
    cHTTPX_MethodPost,
    "/payments/create",
    res
);
```

Resolution is by name:

- local `AppServer` → direct in-process dispatch through the target route/middleware pipeline, without a new TCP connection;
- `AppRemote` → outbound HTTP or HTTPS.

The normal call inherits body, body size, content type, request ID, language, and relevant request headers.

## CallEx

Use `cHTTPX_CallEx()` to replace outgoing body/content type.

```c
const char* body =
    "{\"user_id\":123,\"plan\":\"premium\"}";

chttpx_call_options_t options = {
    .body = body,
    .body_size = strlen(body),
    .content_type = cHTTPX_CTYPE_JSON,
};

int result = cHTTPX_CallEx(
    req,
    "payments",
    cHTTPX_MethodPost,
    "/payments/create",
    &options,
    res
);
```

`CallEx` does not expose timeout configuration.

## Remote results

Connect/send/receive use a fixed 30-second timeout.

| Result | Meaning |
| --- | --- |
| `cHTTPX_OK` | valid HTTP response received, including HTTP 4xx/5xx |
| `cHTTPX_ERR_UNAVAILABLE` | DNS/connect failure or connection lost before a valid response |
| `cHTTPX_ERR_TIMEOUT` | connect/send/receive exceeded 30 seconds |
| `cHTTPX_ERR_TLS` | TLS handshake, certificate verification, encrypted read/write failure |
| `cHTTPX_ERR_PROTOCOL` | peer returned invalid HTTP |

A 400/403/500 response means the server responded successfully at the transport layer:

```c
if (result == cHTTPX_OK)
{
    if (res->status >= 400)
    {
        /* remote application returned an HTTP error */
    }
}
```

## Typical proxy handler

```c
static void create_payment(
    chttpx_request_t* req,
    chttpx_response_t* res)
{
    int result = cHTTPX_Call(
        req,
        "payments",
        cHTTPX_MethodPost,
        "/payments/create",
        res
    );

    if (result == cHTTPX_ERR_UNAVAILABLE)
    {
        *res = cHTTPX_ResError(
            cHTTPX_StatusServiceUnavailable,
            "payment server unavailable"
        );
        return;
    }

    if (result == cHTTPX_ERR_TIMEOUT)
    {
        *res = cHTTPX_ResError(
            cHTTPX_StatusGatewayTimeout,
            "payment server timeout"
        );
        return;
    }

    if (result != cHTTPX_OK)
    {
        *res = cHTTPX_ResError(
            cHTTPX_StatusBadGateway,
            "payment server error"
        );
        return;
    }

    /* res already contains the target HTTP response. */
}
```

Do not use a separate health check as a guarantee before `Call`. A server can fail after the check; the call result is authoritative.
