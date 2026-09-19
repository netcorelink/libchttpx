# Middleware

Middleware can run globally for one server, on a router/group, or on one route.

```c
static chttpx_middleware_result_t middleware(
    chttpx_request_t* req,
    chttpx_response_t* res)
{
    return next;
}
```

Return `next` to continue or `out` to short-circuit with the response already stored in `res`.

## Global server middleware

```c
cHTTPX_MiddlewareUse(server, authenticate);
cHTTPX_MiddlewareUseAfter(server, record_metrics);
```

## Router middleware

```c
chttpx_router_t private_api =
    cHTTPX_RouteGroup(&api, "");

cHTTPX_RouterUse(&private_api, authenticate);
cHTTPX_RouterUseAfter(&private_api, trace_request);
```

Router middleware is copied into routes registered through that router, so unrelated/public routes are not affected.

## Route middleware

```c
chttpx_route_t* route =
    cHTTPX_Post(
        &private_api,
        "/admin/import",
        import_handler
    );

cHTTPX_RouteUse(route, require_admin);
cHTTPX_RouteUseAfter(route, audit_import);
```

## Authentication example

```c
static chttpx_middleware_result_t authenticate(
    chttpx_request_t* req,
    chttpx_response_t* res)
{
    const char* token =
        cHTTPX_BearerToken(req);

    if (!token)
    {
        *res = cHTTPX_ResError(
            cHTTPX_StatusUnauthorized,
            "authentication required"
        );
        return out;
    }

    return next;
}
```

Use named contexts to pass parsed auth/session data to later middleware and handlers.

## Pipeline

Conceptually:

1. server before middleware;
2. route/router before middleware;
3. upload policy validation;
4. handler;
5. route/router after middleware;
6. server after middleware;
7. response send.

After middleware runs before final response transmission. Route after middleware runs in reverse registration order.

## Built-in middleware

- `cHTTPX_MiddlewareLogging(server)` — request/response logging;
- `cHTTPX_MiddlewareRateLimiter(server, max_requests, window_sec)` — fixed-window limiter;
- `cHTTPX_MiddlewareRecovery(server)` — compatibility no-op.

Fatal memory-corrupting signals are not recovered inside the library; use process/container supervision and restart.
