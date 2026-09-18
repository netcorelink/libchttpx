# Responses

Handler записывает результат в `chttpx_response_t`.

## Основные helpers

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

JSON/HTML:

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

Для недоверенных строк лучше использовать JSON builder или escaping helpers.

## Binary

```c
*res = cHTTPX_ResBinary(
    cHTTPX_StatusOK,
    cHTTPX_CTYPE_PNG,
    data,
    data_size
);
```

## File

```c
*res = cHTTPX_ResFile(
    cHTTPX_StatusOK,
    cHTTPX_CTYPE_PDF,
    "./report.pdf"
);
```

Сейчас `ResFile` полностью читает файл в RAM перед отправкой.

## Headers

```c
cHTTPX_HeaderAdd(
    res,
    "Cache-Control",
    "no-store"
);
```

`HeaderAdd` добавляет новое значение и не заменяет существующее, поэтому подходит для нескольких `Set-Cookie`.

## Status/content type

Используйте `cHTTPX_Status*` и `cHTTPX_CTYPE_*` из public API.

`cHTTPX_StatusReason(status)` возвращает стандартный reason phrase.

## Ownership

- `CHTTPX_BODY_OWNED` — cleanup освобождает body;
- `CHTTPX_BODY_BORROWED` — библиотека body не освобождает.

Response helpers, которые выделяют память, создают owned response. В обычном handler не освобождайте их body вручную.

`cHTTPX_ResponseCleanup()` нужен для standalone lifecycle вне обычной отправки server.

## Partial send

Внутри server используется `cHTTPX_SendAll()`, который корректно обрабатывает partial socket writes.
