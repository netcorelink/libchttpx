# CORS

CORS настраивается отдельно для каждого server.

## Настройка

```c
const char* origins[] = {
    "https://example.com",
    "https://admin.example.com"
};

cHTTPX_Cors(
    server,
    origins,
    CHTTPX_ARRAY_LEN(origins),
    "GET, POST, PATCH, OPTIONS",
    "Content-Type, Authorization, X-Request-ID"
);
```

Параметры:

- `origins` — разрешённые Origin;
- `methods` — разрешённые HTTP methods;
- `headers` — разрешённые request headers.

При `methods == NULL` используется:

```text
GET, POST, PUT, DELETE, OPTIONS
```

При `headers == NULL`:

```text
Content-Type
```

CORS config копируется в server state.

## Preflight

```http
OPTIONS /api/v2/users HTTP/2
Origin: https://example.com
Access-Control-Request-Method: POST
```

Настоящий CORS preflight может обрабатываться автоматически.

Обычный `OPTIONS` без CORS preflight semantics может попасть в зарегистрированный `cHTTPX_Options()` handler.

## Несколько серверов

Каждый AppServer может иметь свою независимую CORS policy.
