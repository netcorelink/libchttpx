# libchttpx

`libchttpx` — компактная кроссплатформенная HTTP/1.1-библиотека для C. Она сохраняет простой C-style API, но берёт на себя request-scoped память, routing, middleware, JSON binding, безопасную сериализацию JSON, typed params/query, uploads, request ID, выбор языка, CORS, logging, лимиты и graceful shutdown.

Главный принцип: handler должен содержать бизнес-логику, а не повторяющийся HTTP boilerplate.

## Возможности

- Linux и Windows
- единый config сервера и коды ошибок вместо `exit()`
- routes с `{param}`, method helpers и вложенные groups
- global/router/route middleware, фазы before и after
- request-scoped allocator, defer cleanup и named contexts
- JSON parse + normalization + validation + error response
- безопасные JSON response helpers и builder
- typed params/query и URL decoding
- multipart с несколькими файлами и обычными form fields
- автоматическое удаление temporary uploads и route upload policy
- `Content-Length` и chunked request bodies
- request ID, `Accept-Language`, CORS, cookies, rate limiting и logger callback
- корректные reason phrases и отправка ответа через `send_all`
- остановка listener с ожиданием активных requests

Важно: исходящие file responses специально не переделывались. `cHTTPX_ResFile()` по-прежнему полностью читает файл в RAM. Streaming response, `sendfile()` и zero-copy будут отдельной задачей.

## Установка

Linux:

```sh
make lin-lib
sudo make lib-install PREFIX=/usr/local DESTDIR=
```

Для Linux нужны development headers/library cJSON.

Windows с MinGW/GCC:

```powershell
make win-lib
```

На Windows используется встроенный `lib/cjson`.

## Быстрый старт

Все HTTP-серверы создаются через один `cHTTPX_App`. Каждый `cHTTPX_AppServer()` — обычный независимый `chttpx_serv_t` со своими routes, middleware, CORS, logger и портом.

```c
#include <libchttpx/libchttpx.h>

static void health(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "healthy");
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != CHTTPX_OK)
        return 1;

    chttpx_serv_t* public_api =
        cHTTPX_AppServer(&app, "public", 8080);

    chttpx_serv_t* internal_api =
        cHTTPX_AppServer(&app, "internal", 9090);

    if (!public_api || !internal_api)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t public_router =
        cHTTPX_RoutePathPrefix(public_api, "");

    cHTTPX_Get(&public_router, "/health", health);

    if (cHTTPX_AppStart(&app) != CHTTPX_OK)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    cHTTPX_AppWait(&app);
    cHTTPX_AppShutdown(&app);
    return 0;
}
```

`cHTTPX_AppStart(&app)` запускает каждый добавленный server в отдельном listener thread.

Если один server должен вызвать route другого server из того же `App`, можно использовать `cHTTPX_Call()`. В этом случае новый TCP connection не создаётся — запрос передаётся напрямую в route pipeline целевого server.

```c
cHTTPX_Call(
    req,
    "internal",
    cHTTPX_MethodPost,
    "/process",
    res
);
```

Текущий body, content type, request ID, language и request headers наследуются автоматически. Для другого body есть `cHTTPX_CallWithBody()`.

## Конфигурация сервера

```c
chttpx_config_t config = cHTTPX_DefaultConfig();
config.port = 8080;
config.max_clients = 256;
config.read_timeout_sec = 30;
config.write_timeout_sec = 30;
config.idle_timeout_sec = 60;
config.max_header_size = 16 * 1024;
config.max_body_size = 10 * 1024 * 1024;
config.max_upload_size = 500ULL * 1024 * 1024;
config.request_id_enabled = true;
```

`Content-Length` проверяется до скачивания body. Превышение body/upload limit возвращает `413 Payload Too Large`, превышение header limit — `431 Request Header Fields Too Large`. `cHTTPX_AppInit()` возвращает `chttpx_error_t`, а `cHTTPX_AppServerWithConfig()` возвращает указатель на созданный server или `NULL`; библиотека не завершает приложение через `exit()`.

## Routes и groups

```c
chttpx_router_t api = cHTTPX_RoutePathPrefix(server, "/api/v2");
chttpx_router_t auth = cHTTPX_RouteGroup(&api, "/auth");

cHTTPX_Post(&auth, "/login", login_handler);
cHTTPX_Post(&auth, "/create", create_handler);
cHTTPX_Get(&api, "/users/{user_id}", user_handler);
```

Есть helpers для GET, POST, PUT, PATCH, DELETE и OPTIONS. Совместимый `cHTTPX_RegisterRoute()` оставлен. Prefix хранится внутри router value и не создаёт скрытой утечки.

## Middleware

Middleware возвращает `next` для продолжения или `out`, чтобы сразу отправить сформированный response.

```c
static chttpx_middleware_result_t authenticate(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* token = cHTTPX_BearerToken(req);
    if (!token)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, "authentication required");
        return out;
    }
    return next;
}

chttpx_router_t private_api = cHTTPX_RouteGroup(&api, "");
cHTTPX_RouterUse(&private_api, authenticate);
cHTTPX_Get(&private_api, "/users/me", get_me);
```

Router middleware копируется только в routes, зарегистрированные через этот router. Authentication больше не обязан проверять `req->path` и содержать список публичных исключений.

```c
chttpx_route_t* route = cHTTPX_Post(&private_api, "/admin/import", import_data);
cHTTPX_RouteUse(route, require_admin);
cHTTPX_RouteUseAfter(route, record_metrics);

cHTTPX_MiddlewareUse(server, global_before);
cHTTPX_MiddlewareUseAfter(server, global_after);
cHTTPX_RouterUseAfter(&private_api, trace_private_route);
```

After middleware выполняются в обратном порядке после handler или route short-circuit, но до отправки response.

`cHTTPX_MiddlewareRecovery(server)` оставлен как совместимый no-op. Продолжать процесс после `SIGSEGV` через `setjmp`/`longjmp` небезопасно; фатальные ошибки должны обрабатываться supervisor/restart-моделью.

## Request и metadata

Библиотека один раз разбирает:

```c
req->request_id;
req->client_ip;
req->method;
req->path;
req->protocol;
req->user_agent;
req->language;
req->content_type;
req->content_length;
```

Headers, cookies, params, query и form values возвращаются как borrowed pointers — освобождать их нельзя.

```c
const char* origin = cHTTPX_HeaderGet(req, "Origin");
const chttpx_cookie_t* session = cHTTPX_CookieGet(req, "session");
const char* raw_id = cHTTPX_Param(req, "user_id");
const char* search = cHTTPX_Query(req, "search");
```

Query parser декодирует `%20`, `%2F`, `%40` и `+`, не использует глобальный `strtok()` и отклоняет некорректные escape sequences.

## Typed params и typed query

```c
uint64_t user_id;
uint64_t offset;
bool enabled;
double score;

if (!cHTTPX_ParamU64(req, "user_id", &user_id))
    return;

cHTTPX_QueryU64Default(req, "offset", &offset, 0);
cHTTPX_QueryBool(req, "enabled", &enabled);
cHTTPX_QueryDouble(req, "score", &score);
```

Helpers различают отсутствующее/пустое значение, invalid characters, overflow и отрицательное значение для unsigned.

## JSON body, bind и validation

```c
typedef struct
{
    char* email;
    char* username;
} create_user_t;

static bool validate_username(const void* value, char* error, size_t error_size)
{
    const char* username = value;
    if (strlen(username) >= 3)
        return true;
    snprintf(error, error_size, "username is too short");
    return false;
}

static void create_user(chttpx_request_t* req, chttpx_response_t* res)
{
    create_user_t payload = {0};
    chttpx_validation_t fields[] = {
        cHTTPX_StringField("email", &payload.email, true, 3, 254,
                           CHTTPX_TRIM | CHTTPX_LOWERCASE, NULL),
        cHTTPX_StringField("username", &payload.username, true, 3, 32,
                           CHTTPX_TRIM, validate_username),
    };

    if (!cHTTPX_BindJSON(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    *res = cHTTPX_ResMessage(cHTTPX_StatusCreated, "user created");
}
```

`cHTTPX_BindJSON()` объединяет parse, normalizers, validation, request-owned memory и безопасный JSON response с `400 Bad Request`.

`payload.email` и `payload.username` освобождать **не нужно**. Память принадлежит request и автоматически освобождается после его завершения.

Старые `cHTTPX_Parse()` и `cHTTPX_Validate()` сохранены; их строки и массивы теперь также request-owned.

## JSON responses и builder

```c
*res = cHTTPX_ResError(cHTTPX_StatusForbidden, reason);
*res = cHTTPX_ResMessage(cHTTPX_StatusOK, message);
*res = cHTTPX_ResNoContent();
```

Строки корректно JSON-escaped. Для составного ответа:

```c
chttpx_json_t* json = cHTTPX_JsonObject(req);
cHTTPX_JsonString(json, "message", text);
cHTTPX_JsonNumber(json, "id", id);
cHTTPX_JsonBool(json, "active", true);

chttpx_json_t* tags = cHTTPX_JsonArray(req);
cHTTPX_JsonArrayString(tags, "c");
cHTTPX_JsonArrayString(tags, "http");
cHTTPX_JsonChild(json, "tags", tags);

*res = cHTTPX_ResJsonObject(cHTTPX_StatusOK, json);
```

Поддерживаются object, array, string, number, bool, null и nested values. Для legacy-кода есть `cHTTPX_JsonEscape()`. Не вставляйте недоверенные строки напрямую в format string `cHTTPX_ResJson()`.

## Request-scoped память и contexts

```c
char* copy = cHTTPX_Strdup(req, source);
void* buffer = cHTTPX_Alloc(req, 4096);

FILE* file = fopen(path, "rb");
cHTTPX_Defer(req, file, (chttpx_cleanup_fn)fclose);

cHTTPX_ContextSet(req, "auth", auth, auth_free);
auth_context_t* current = cHTTPX_ContextGet(req, "auth");
```

Все ресурсы cleanup-ятся после request. `cHTTPX_Detach()` и `cHTTPX_ContextDetach()` передают ownership приложению. Старые `req->context`/`context_free` временно сохранены.

## Ownership

| Значение | Владение |
|---|---|
| Header/query/param/cookie/form value | borrowed, не освобождать |
| Строки и массивы JSON bind | request-owned |
| `cHTTPX_Alloc` / `cHTTPX_Strdup` | request-owned |
| Named context с cleanup callback | request-owned |
| `ResJson` / `ResHtml` / `ResBinary` / builder response | response-owned, библиотека освобождает после отправки |
| Static response с `CHTTPX_BODY_BORROWED` | библиотека не освобождает |
| Temporary upload | request-owned файл, удаляется автоматически |
| Detach/Keep | lifetime переходит приложению |

## File uploads, multipart и формы

```c
const chttpx_file_t* avatar = cHTTPX_FormFile(req, "avatar");
const char* caption = cHTTPX_FormValue(req, "caption");

if (!avatar)
{
    *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, "avatar is required");
    return;
}

/* avatar->path доступен до окончания request */
```

Multipart parser понимает boundary, `Content-Disposition`, name, filename, `Content-Type`, обычные поля и несколько файлов. `application/x-www-form-urlencoded` тоже поддерживается.

Temporary file автоматически удаляется. `cHTTPX_FileKeep(req)` сохраняет первый upload; `cHTTPX_FileDetach(req, file)` отключает автоматическое удаление конкретного файла.

```c
const char* image_types[] = {"image/jpeg", "image/png", "image/gif"};
chttpx_upload_policy_t policy = {
    .max_size = 10 * 1024 * 1024,
    .allowed_types = image_types,
    .allowed_types_count = CHTTPX_ARRAY_LEN(image_types),
};

chttpx_route_t* upload = cHTTPX_Patch(&private_api, "/users/me/avatar", upload_avatar);
cHTTPX_RouteUploadPolicy(upload, &policy);
```

При нарушении policy handler не запускается, возвращается `413` или `415`. Есть `cHTTPX_MimeMatch()`, `cHTTPX_MimeIsImage()`, `cHTTPX_MimeIsVideo()` и `cHTTPX_MimeIsAudio()`.

Fixed-length и chunked request bodies поддерживаются. Multipart body во время приёма сохраняется в дисковый временный stream и затем разбирается ограниченными буферами; файловые части становятся request-owned временными файлами. Поэтому большой `multipart/form-data` больше не требует RAM размером со весь запрос. JSON, text и URL-encoded body остаются memory-backed.

## Request ID и Accept-Language

При включённом `request_id_enabled` валидный `X-Request-ID` клиента сохраняется, иначе библиотека генерирует новый. Он доступен в `req->request_id`, автоматически добавляется в response и передаётся logger callback.

```c
const char* languages[] = {"en", "ru"};
config.languages = languages;
config.languages_count = CHTTPX_ARRAY_LEN(languages);
config.default_language = "en";
```

`Accept-Language` разбирается с quality weights; `ru-RU` сопоставляется с `ru`, выбирается только разрешённый язык, иначе используется fallback. Результат — `req->language`.

## CORS, cookies, logging, rate limiting

```c
const char* origins[] = {"https://example.com"};
cHTTPX_Cors(server, origins, CHTTPX_ARRAY_LEN(origins),
            "GET, POST, PATCH, OPTIONS",
            "Content-Type, Authorization, X-Request-ID");
```

CORS config копируется сервером. Для cookies используются `cHTTPX_CookieGet()` и `cHTTPX_CookieSet()`.

```c
static void logger(chttpx_log_level_t level, const char* request_id,
                   const char* message, void* user_data)
{
    (void)level;
    (void)user_data;
    fprintf(stderr, "request_id=%s %s\n", request_id, message);
}

cHTTPX_SetLogger(server, logger, NULL, CHTTPX_LOG_INFO);
cHTTPX_MiddlewareLogging(server);
cHTTPX_MiddlewareRateLimiter(server, 100, 1);
```

Путь `./logs` больше не захардкожен. Default logger пишет в stderr, приложение может подключить любой backend. Shared table rate limiter защищена mutex.

## Timeouts, shutdown и thread safety

Timeouts задаются в `chttpx_config_t`. Для SIGINT/SIGTERM вызывайте `cHTTPX_AppShutdown(&app)` из control/signal thread: функция прекращает accept, закрывает listener, ждёт активные requests, освобождает routes и CORS state.

`current_clients` изменяется атомарно; busy-loop при достижении лимита удалён. Routes настраиваются до `cHTTPX_AppStart()`/`cHTTPX_AppRun()` и затем только читаются. Один request и его allocator должны использоваться только его worker thread. Shared state приложения синхронизируется самим приложением.

## Обработка ошибок и миграция

Status line использует корректный reason phrase. `cHTTPX_SendAll()` обрабатывает partial sends. Сервер возвращает `CHTTPX_ERR_*`, а не вызывает `exit()`.

Миграция:

- standalone `cHTTPX_Init()`/`cHTTPX_Listen()`/`cHTTPX_Shutdown()` удалены; server создаётся и управляется только через `cHTTPX_App`;
- `cHTTPX_RegisterRoute()` работает, но method helpers удобнее;
- `req->context` работает, но named contexts не конфликтуют между middleware;
- `req->filename` заполняется для первого файла, но лучше `RequestFile`/`FormFile`;
- `Parse`/`Validate` работают, но `BindJSON` убирает boilerplate;
- вручную освобождать response body и JSON-bound строки больше не нужно;
- recovery middleware больше не перехватывает фатальные сигналы.

## Сборка и tests

```sh
make lin-lib
```

Windows:

```powershell
make test-win
```

Tests покрывают cleanup/defer, contexts, JSON bind и normalization, typed params/query, URL decoding, Bearer, JSON escaping/builder, multipart, удаление temporary files, MIME, status reasons и router middleware metadata.

## Лицензия

MIT, см. [LICENSE](LICENSE).
