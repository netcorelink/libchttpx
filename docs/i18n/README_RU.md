# Request ID и i18n

## Request ID

Если `request_id_enabled = true`:

- входящий валидный `X-Request-ID` сохраняется;
- иначе библиотека генерирует новый;
- значение доступно в `req->request_id`;
- оно добавляется в response;
- передаётся logger callback.

## Языки server

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

Также можно вызвать:

```c
cHTTPX_i18n_languages(
    server,
    languages,
    CHTTPX_ARRAY_LEN(languages),
    "en"
);
```

## Accept-Language

Например:

```http
Accept-Language: en;q=0.2, ru-RU;q=0.9
```

При наличии `ru` будет выбран:

```c
req->language /* "ru" */
```

Учитываются quality weights и regional suffixes. Если подходящего языка нет, используется fallback.

## Translation catalog

```c
cHTTPX_i18n("./locales");
```

Структура:

```text
locales/
  en.json
  ru.json
  es.json
```

Пример:

```json
{
  "error.unauthorized": "Authentication required",
  "user.created": "User created"
}
```

Получить строку:

```c
const char* message =
    cHTTPX_i18n_t(
        "error.unauthorized",
        req->language
    );
```

Returned string принадлежит i18n manager.

Language preference config — per-server, а loaded translation catalog сейчас process-global.
