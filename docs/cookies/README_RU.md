# Cookies

## Читать cookie

```c
const chttpx_cookie_t* session =
    cHTTPX_CookieGet(req, "session");

if (session)
{
    const char* value = session->value;
}
```

Cookie request-owned; освобождать не нужно.

## Установить cookie

```c
chttpx_cookie_t cookie = {
    .name = "session",
    .value = "token-value",
    .path = "/",
    .http_only = true,
    .secure = true,
    .same_site = 2,
};

cHTTPX_CookieSet(res, &cookie);
```

Поддерживаются:

- Path
- Domain
- Expires
- SameSite
- Secure
- HttpOnly

`same_site`:

| Значение | Mode |
| ---: | --- |
| 0 | не добавлять SameSite |
| 1 | Lax |
| 2 | Strict |
| 3 | None |

Для `SameSite=None` браузеры обычно требуют `Secure`.

`cHTTPX_CookieSet()` добавляет новый `Set-Cookie` header и не заменяет предыдущий, поэтому response может установить несколько cookies.
