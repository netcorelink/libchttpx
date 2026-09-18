# Rate limiting

В библиотеке есть per-server fixed-window rate limiter middleware.

## Включение

```c
cHTTPX_MiddlewareRateLimiter(
    server,
    100,
    1
);
```

Параметры:

- `max_requests` — разрешённое количество requests;
- `window_sec` — размер окна в секундах.

## Разные limits

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

Limiter state хранится отдельно для каждого server.

Shared table защищена mutex и ограничена `MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE`.

Для более сложных схем лучше написать application middleware:

- разные лимиты по routes;
- limits по user/account;
- Redis distributed limits;
- sliding window/token bucket;
- общие counters между несколькими processes.
