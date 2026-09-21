# App runtime и вызовы между серверами

`cHTTPX_App` — верхний уровень runtime. В одном App можно держать несколько локальных HTTP-серверов и регистрировать удалённые сервисы.

## Инициализация

```c
chttpx_app_t app;

if (cHTTPX_AppInit(&app) != cHTTPX_OK)
    return 1;
```

При завершении приложения вызывайте `cHTTPX_AppShutdown()`.

## Несколько локальных серверов

Каждый server получает собственные routes, middleware, CORS, logger, limits и listener.

```c
chttpx_config_t public_config = cHTTPX_DefaultConfig();
public_config.port = 8080;

chttpx_config_t admin_config = cHTTPX_DefaultConfig();
admin_config.port = 9090;

chttpx_serv_t* public_server =
    cHTTPX_AppServer(&app, "public", &public_config);

chttpx_serv_t* admin_server =
    cHTTPX_AppServer(&app, "admin", &admin_config);
```

Имена должны быть уникальны внутри App и не должны совпадать с именами `AppRemote`.

## Запуск

```c
if (cHTTPX_AppStart(&app) != cHTTPX_OK)
{
    cHTTPX_AppShutdown(&app);
    return 1;
}

cHTTPX_AppWait(&app);
cHTTPX_AppShutdown(&app);
```

`cHTTPX_AppStart()` запускает listener каждого локального server в отдельном thread.

`cHTTPX_AppRun()` — сокращение для Start + Wait.

## Удалённый server

```c
cHTTPX_AppRemote(
    &app,
    "payments",
    "http://payment-server:8090"
);
```

Встроенный transport поддерживает `http://` и `https://`. HTTPS доступен в сборке `TLS=1`; проверка сертификата и hostname включена по умолчанию. Для custom CA или client certificate используйте `cHTTPX_AppRemoteEx()`. Подробнее: [Native TLS / HTTPS](../tls/README_RU.md).

В Docker Compose имя host может быть именем другого service в той же сети.

## cHTTPX_Call

```c
int result = cHTTPX_Call(
    req,
    "payments",
    cHTTPX_MethodPost,
    "/payments/create",
    res
);
```

Если `payments` — local AppServer, библиотека вызывает route напрямую внутри процесса без нового TCP connection.

Если `payments` зарегистрирован через `AppRemote`, выполняется HTTP- или HTTPS-запрос.

Обычный `Call` наследует body, body size, content type, request ID, language и нужные headers текущего request.

## cHTTPX_CallEx

Используйте `CallEx`, когда body исходящего запроса должен отличаться от текущего:

```c
const char* body =
    "{\"user_id\":123,\"plan\":\"premium\"}";

chttpx_call_options_t options = {
    .body = body,
    .body_size = strlen(body),
    .content_type = cHTTPX_CTYPE_JSON,
};

int result = cHTTPX_CallEx(
    req,
    "payments",
    cHTTPX_MethodPost,
    "/payments/create",
    &options,
    res
);
```

Timeout через `CallEx` менять нельзя.

## Ошибки remote Call

Для connect/send/receive используется фиксированный timeout 30 секунд.

| Результат | Значение |
| --- | --- |
| `cHTTPX_OK` | получен корректный HTTP response, даже если status 400/403/500 |
| `cHTTPX_ERR_UNAVAILABLE` | DNS/connect failed или connection оборвался до HTTP response |
| `cHTTPX_ERR_TIMEOUT` | connect/send/receive превысил 30 секунд |
| `cHTTPX_ERR_TLS` | ошибка TLS handshake, certificate verification или encrypted read/write |
| `cHTTPX_ERR_PROTOCOL` | remote сторона вернула некорректный HTTP response |

HTTP 500 — не transport error:

```c
if (result == cHTTPX_OK && res->status == 500)
{
    /* server жив, но его handler вернул ошибку */
}
```

## Пример proxy handler

```c
static void create_payment(
    chttpx_request_t* req,
    chttpx_response_t* res)
{
    int result = cHTTPX_Call(
        req,
        "payments",
        cHTTPX_MethodPost,
        "/payments/create",
        res
    );

    if (result == cHTTPX_ERR_UNAVAILABLE)
    {
        *res = cHTTPX_ResError(
            cHTTPX_StatusServiceUnavailable,
            "payment server unavailable"
        );
        return;
    }

    if (result == cHTTPX_ERR_TIMEOUT)
    {
        *res = cHTTPX_ResError(
            cHTTPX_StatusGatewayTimeout,
            "payment server timeout"
        );
        return;
    }

    if (result != cHTTPX_OK)
    {
        *res = cHTTPX_ResError(
            cHTTPX_StatusBadGateway,
            "payment server error"
        );
        return;
    }

    /* res уже содержит ответ target server */
}
```

Отдельный health-check перед `Call` не является гарантией: server может упасть между проверкой и реальным запросом. Ориентируйтесь на результат самого `Call`.
