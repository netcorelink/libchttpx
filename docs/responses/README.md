# Responses

Handlers write into `chttpx_response_t`.

## Common helpers

```c
*res = cHTTPX_ResMessage(
    cHTTPX_StatusOK,
    "healthy"
);

*res = cHTTPX_ResError(
    cHTTPX_StatusForbidden,
    "forbidden"
);

*res = cHTTPX_ResNoContent();
```

Formatted JSON/HTML:

```c
*res = cHTTPX_ResJson(
    cHTTPX_StatusOK,
    "{\"id\":%llu}",
    (unsigned long long)id
);

*res = cHTTPX_ResHtml(
    cHTTPX_StatusOK,
    "<h1>%s</h1>",
    title
);
```

Prefer the JSON builder or escaping helpers for untrusted strings.

## Binary response

```c
*res = cHTTPX_ResBinary(
    cHTTPX_StatusOK,
    cHTTPX_CTYPE_PNG,
    data,
    data_size
);
```

## File response

```c
*res = cHTTPX_ResFile(
    cHTTPX_StatusOK,
    cHTTPX_CTYPE_PDF,
    "./report.pdf"
);
```

Current limitation: `ResFile` reads the complete file into RAM before sending.

## Headers

```c
cHTTPX_HeaderAdd(
    res,
    "Cache-Control",
    "no-store"
);
```

`HeaderAdd` appends and does not replace existing same-name headers, allowing multiple `Set-Cookie` values.

## Status/content constants

Use `cHTTPX_Status*` and `cHTTPX_CTYPE_*` constants from `http.h`. `cHTTPX_StatusReason(status)` returns the standard reason phrase.

## Ownership

- `CHTTPX_BODY_OWNED` → response cleanup frees the body;
- `CHTTPX_BODY_BORROWED` → library does not free the body.

Response helpers that allocate content create owned responses. Normal handler code should not manually free helper-produced bodies; the server cleans them after send.

`cHTTPX_ResponseCleanup()` exists for explicit standalone response lifecycles.

## Invalid output and partial writes

If a handler/middleware does not produce a valid response status, the server returns an internal server error rather than emitting an invalid status line.

`cHTTPX_SendAll()` handles partial socket writes; applications normally do not call it directly.
