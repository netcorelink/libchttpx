# Request IDs and i18n

This module covers request IDs, language negotiation, and the translation catalog.

## Request ID

With `request_id_enabled` enabled:

- a valid incoming `X-Request-ID` is preserved;
- otherwise the library generates one;
- it is available in `req->request_id`;
- it is added to the response;
- it is passed to the logger callback.

```c
chttpx_config_t config =
    cHTTPX_DefaultConfig();

config.request_id_enabled = true;
```

## Supported languages

```c
const char* languages[] = {
    "en",
    "ru",
    "es"
};

chttpx_config_t config =
    cHTTPX_DefaultConfig();

config.languages = languages;
config.languages_count =
    CHTTPX_ARRAY_LEN(languages);

config.default_language = "en";
```

The server owns copied language configuration.

It can also be updated before serving:

```c
cHTTPX_i18n_languages(
    server,
    languages,
    CHTTPX_ARRAY_LEN(languages),
    "en"
);
```

## Accept-Language negotiation

Example:

```http
Accept-Language: en;q=0.2, ru-RU;q=0.9
```

With `ru` configured, the selected language becomes:

```c
req->language /* "ru" */
```

Regional suffixes are reduced to the configured base language when applicable, quality weights are considered, and unsupported languages fall back to the configured default.

## Translation catalog

Load locale JSON files:

```c
cHTTPX_i18n("./locales");
```

Directory example:

```text
locales/
  en.json
  ru.json
  es.json
```

Example file:

```json
{
  "error.unauthorized": "Authentication required",
  "user.created": "User created"
}
```

Read:

```c
const char* message =
    cHTTPX_i18n_t(
        "error.unauthorized",
        req->language
    );
```

Returned translations are manager-owned and must not be freed.

```c
*res = cHTTPX_ResError(
    cHTTPX_StatusUnauthorized,
    cHTTPX_i18n_t(
        "error.unauthorized",
        req->language
    )
);
```

Language preference configuration is per server. The loaded translation catalog is process-global in the current implementation.
