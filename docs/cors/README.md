# CORS

CORS configuration belongs to one server.

## Enable CORS

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

Arguments:

- `origins` — exact allowed Origin values;
- `origins_count` — number of origins;
- `methods` — comma-separated allowed methods;
- `headers` — comma-separated allowed request headers.

When `methods == NULL`, the default is:

```text
GET, POST, PUT, DELETE, OPTIONS
```

When `headers == NULL`, the default is:

```text
Content-Type
```

The server copies CORS configuration into its own state.

## Preflight

A CORS preflight looks like:

```http
OPTIONS /api/v2/users HTTP/2
Origin: https://example.com
Access-Control-Request-Method: POST
```

Configured preflight can be answered automatically.

A normal `OPTIONS` request without CORS preflight semantics can still reach a registered `cHTTPX_Options()` handler.

## Multiple servers

Each App server can have an independent CORS policy:

```c
cHTTPX_Cors(
    public_server,
    public_origins,
    public_origin_count,
    NULL,
    NULL
);

cHTTPX_Cors(
    admin_server,
    admin_origins,
    admin_origin_count,
    NULL,
    NULL
);
```
