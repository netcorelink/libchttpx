# Rate limiting

libchttpx includes a per-server fixed-window rate limiter middleware.

## Enable

```c
cHTTPX_MiddlewareRateLimiter(
    server,
    100,
    1
);
```

Arguments:

- `max_requests` — allowed request count;
- `window_sec` — window size in seconds.

The example configures 100 requests per one-second window according to the built-in limiter's key/state logic.

## Per-server isolation

```c
cHTTPX_MiddlewareRateLimiter(
    public_server,
    100,
    1
);

cHTTPX_MiddlewareRateLimiter(
    admin_server,
    20,
    1
);
```

Limiter state belongs to its server.

## Concurrency

The limiter's shared table is protected by a mutex. Its table size is bounded by `MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE`.

## Scope

The built-in limiter is registered as global server middleware.

For more advanced requirements, implement application middleware instead:

- different limits per route;
- authenticated user/account keys;
- Redis-backed limits shared by multiple processes;
- sliding windows/token buckets;
- persisted/distributed counters.

When the built-in limiter rejects a request, middleware short-circuits the handler with the library-defined rate-limit response.
