# Middleware

Middleware можно назначать на весь server, на router/group или на отдельный route.

```c
static chttpx_middleware_result_t middleware(
    chttpx_request_t* req,
    chttpx_response_t* res)
{
    return next;
}
```

`next` продолжает pipeline, `out` завершает before-chain и отправляет response из `res`.

## Global middleware

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

Middleware копируется в routes, зарегистрированные через этот router.

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

## Auth example

```c
static chttpx_middleware_result_t authenticate(
    chttpx_request_t* req,
    chttpx_response_t* res)
{
    const char* token = cHTTPX_BearerToken(req);

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

Для передачи auth/session state дальше используйте named contexts.

## Порядок выполнения

Упрощённо:

1. server before middleware;
2. router/route before middleware;
3. upload policy;
4. handler;
5. router/route after middleware;
6. server after middleware;
7. отправка response.

After middleware выполняется до финальной отправки ответа.

## Built-in middleware

- `cHTTPX_MiddlewareLogging(server)`
- `cHTTPX_MiddlewareRateLimiter(server, max_requests, window_sec)`
- `cHTTPX_MiddlewareRecovery(server)` — compatibility no-op

Fatal signals библиотека не пытается «лечить» внутри процесса; используйте supervisor/container restart policy.
