# Cookies

libchttpx parses request cookies and can append `Set-Cookie` headers to responses.

## Read

```c
const chttpx_cookie_t* session =
    cHTTPX_CookieGet(
        req,
        "session"
    );

if (session)
{
    const char* value =
        session->value;
}
```

The returned cookie is request-owned.

## Set

```c
chttpx_cookie_t cookie = {
    .name = "session",
    .value = "token-value",
    .path = "/",
    .http_only = true,
    .secure = true,
    .same_site = 2,
};

cHTTPX_CookieSet(
    res,
    &cookie
);
```

Supported attributes:

- Path
- Domain
- Expires
- SameSite
- Secure
- HttpOnly

`same_site` values:

| Value | Meaning |
| ---: | --- |
| 0 | omit SameSite |
| 1 | Lax |
| 2 | Strict |
| 3 | None |

Browsers generally require `Secure` when using `SameSite=None`.

## Expiration

Set `cookie.expires` to a Unix `time_t` value. The library formats the attribute as GMT.

## Multiple cookies

`cHTTPX_CookieSet()` appends `Set-Cookie` rather than replacing previous cookie headers, so a response can set several cookies.

## Session-cookie baseline

A common starting point for session cookies:

```c
.http_only = true,
.secure = true,
.same_site = 1
```

Adjust SameSite behavior to the application's cross-site login/payment flows.
