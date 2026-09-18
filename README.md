# libchttpx

`libchttpx` is a small, cross-platform HTTP/1.1 server library for C. It keeps a direct C-style API while providing request-scoped memory, routing groups and middleware, JSON binding, safe JSON responses, typed request values, uploads, request IDs, i18n language selection, CORS, logging, limits, and graceful shutdown.

The library owns the repetitive HTTP work. Application handlers should focus on business logic.

## Features

- Linux and Windows sockets and threads
- Configurable connection, body, upload, and header limits
- Exact and `{parameter}` routes with method helpers and groups
- Global, group, and route middleware with before/after phases
- Request-scoped allocation, deferred cleanup, and named contexts
- JSON parsing, validation, normalization, custom validation, and binding
- Escaping-safe JSON responses and object/array builder
- Typed path parameters and query values
- URL-decoded query and form values
- Multipart forms, multiple files, upload policies, and automatic temporary-file cleanup
- Fixed-length and chunked request bodies
- Request IDs and weighted `Accept-Language` selection
- CORS, cookies, rate limiting, and callback-based logging
- Partial-write-safe response sending
- Graceful listener shutdown and active-request draining

Outbound file responses intentionally retain their current behavior: `cHTTPX_ResFile()` reads the complete file into RAM. Streaming, `sendfile()`, and zero-copy responses are not part of this release.

## Installation

### Linux

```sh
make lin-lib
sudo make lib-install PREFIX=/usr/local DESTDIR=
```

The Linux build expects cJSON development headers and library to be installed.

### Windows

Use MinGW/GCC:

```powershell
make win-lib
```

The Windows build uses the bundled `lib/cjson` source and links Winsock.

## Quick start

All HTTP servers are created through one `cHTTPX_App`. Every `cHTTPX_AppServer()` is a normal independent `chttpx_serv_t` with its own routes, middleware, CORS, logger, and port.

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

`cHTTPX_AppStart(&app)` starts every registered server in its own listener thread.

When one server needs to call a route on another server in the same `App`, use `cHTTPX_Call()`. No extra TCP connection is opened; the request is dispatched directly through the target server's route pipeline.

```c
cHTTPX_Call(
    req,
    "internal",
    cHTTPX_MethodPost,
    "/process",
    res
);
```

The current body, content type, request ID, language, and request headers are inherited automatically. Use `cHTTPX_CallWithBody()` when a different body is needed.

## Server configuration

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

`Content-Length` is validated before a request body is downloaded. Oversized regular bodies and uploads receive `413 Payload Too Large`; oversized headers receive `431 Request Header Fields Too Large`.

`cHTTPX_AppInit()` returns `chttpx_error_t`. `cHTTPX_AppServerWithConfig()` returns the created server pointer or `NULL`; the library does not call `exit()` for socket, bind, listen, or allocation failures.

## Routes and groups

```c
chttpx_router_t api = cHTTPX_RoutePathPrefix(server, "/api/v2");
chttpx_router_t auth = cHTTPX_RouteGroup(&api, "/auth");

cHTTPX_Post(&auth, "/login", login_handler);
cHTTPX_Post(&auth, "/create", create_handler);
cHTTPX_Get(&api, "/users/{user_id}", user_handler);
```

Helpers are available for GET, POST, PUT, PATCH, DELETE, and OPTIONS. `cHTTPX_RegisterRoute()` remains as a compatibility API. Router prefixes are stored inside the router value, so they do not leak; `cHTTPX_RouterFree()` clears a router when desired.

## Middleware

A middleware returns `next` to continue or `out` to short-circuit with its response.

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

Middleware copied from a router applies only to routes registered through that router. This removes path-based exception lists from authentication middleware.

Route-specific and after middleware:

```c
chttpx_route_t* route = cHTTPX_Post(&private_api, "/admin/import", import_data);
cHTTPX_RouteUse(route, require_admin);
cHTTPX_RouteUseAfter(route, record_metrics);

cHTTPX_MiddlewareUse(server, global_before);
cHTTPX_MiddlewareUseAfter(server, global_after);
cHTTPX_RouterUseAfter(&private_api, trace_private_route);
```

After middleware runs in reverse registration order after the handler or a short-circuiting route middleware and before the response is sent.

`cHTTPX_MiddlewareRecovery(server)` is retained as a compatibility no-op. Catching `SIGSEGV` with `setjmp`/`longjmp` and continuing a potentially corrupted process was unsafe. Use process supervision and restart on fatal faults.

## Request data

Common metadata is parsed once:

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

Headers, cookies, parameters, and query values are borrowed pointers. Do not free them.

```c
const char* origin = cHTTPX_HeaderGet(req, "Origin");
const chttpx_cookie_t* session = cHTTPX_CookieGet(req, "session");
const char* raw_id = cHTTPX_Param(req, "user_id");
const char* search = cHTTPX_Query(req, "search");
```

Queries decode `%20`, `%2F`, `%40`, and `+`. Invalid percent encoding makes the request invalid.

## Typed parameters and queries

```c
uint64_t user_id;
bool enabled;
double score;

if (!cHTTPX_ParamU64(req, "user_id", &user_id))
    return;

cHTTPX_QueryU64Default(req, "offset", &offset, 0);
cHTTPX_QueryBool(req, "enabled", &enabled);
cHTTPX_QueryDouble(req, "score", &score);
```

Typed helpers reject missing/empty input, trailing characters, overflow, and negative unsigned values.

## JSON bind and validation

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

`cHTTPX_BindJSON()` combines parsing, normalization, validation, request-owned allocations, and a safe JSON `400 Bad Request` response. You do **not** free `payload.email` or `payload.username`; their memory belongs to the request and is released automatically.

The legacy `cHTTPX_Parse()` and `cHTTPX_Validate()` functions remain available and now produce request-owned strings and arrays.

## JSON responses and builder

Use these helpers when inserting application strings:

```c
*res = cHTTPX_ResError(cHTTPX_StatusForbidden, reason);
*res = cHTTPX_ResMessage(cHTTPX_StatusOK, message);
*res = cHTTPX_ResNoContent();
```

They escape JSON correctly. For structured output:

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

Objects, arrays, strings, numbers, booleans, nulls, and nested values are supported. `cHTTPX_JsonEscape()` is available for legacy formatted JSON. Prefer the builder or response helpers over interpolating untrusted values into `cHTTPX_ResJson()`.

## Request-scoped memory and contexts

```c
char* copy = cHTTPX_Strdup(req, source);
void* buffer = cHTTPX_Alloc(req, 4096);

FILE* file = fopen(path, "rb");
cHTTPX_Defer(req, file, (chttpx_cleanup_fn)fclose);

cHTTPX_ContextSet(req, "auth", auth, auth_free);
auth_context_t* current = cHTTPX_ContextGet(req, "auth");
```

Everything registered this way is released after the request. `cHTTPX_Detach()` and `cHTTPX_ContextDetach()` transfer ownership to the application. The legacy `req->context` and `req->context_free` fields remain supported.

## Ownership

| Value | Ownership |
|---|---|
| Header, query, path parameter, cookie, form value | borrowed; do not free |
| JSON bind strings and arrays | request-owned; do not free |
| `cHTTPX_Alloc` / `cHTTPX_Strdup` | request-owned; do not free |
| Named context with cleanup callback | request-owned |
| `cHTTPX_ResJson`, `ResHtml`, `ResBinary`, JSON builder response | response-owned; library frees after sending |
| Static response body with `CHTTPX_BODY_BORROWED` | application/static storage; library does not free |
| Temporary upload | request-owned file; automatically removed |
| Explicit detach/keep | ownership or file lifetime moves to the application |

## File uploads and forms

```c
const chttpx_file_t* avatar = cHTTPX_FormFile(req, "avatar");
const char* caption = cHTTPX_FormValue(req, "caption");

if (!avatar)
{
    *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, "avatar is required");
    return;
}

/* Read or copy avatar->path here. It is removed after the request. */
```

`multipart/form-data` parses boundaries, `Content-Disposition`, field names, original filenames, content types, and data. Multiple files are supported. `application/x-www-form-urlencoded` values are URL-decoded.

To retain the first upload, call `cHTTPX_FileKeep(req)`. To detach a specific upload, call `cHTTPX_FileDetach(req, file)`. The application then owns its lifecycle.

Route upload policy:

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

The handler is skipped with `413` or `415` when policy validation fails. MIME helpers include `cHTTPX_MimeMatch()`, `cHTTPX_MimeIsImage()`, `cHTTPX_MimeIsVideo()`, and `cHTTPX_MimeIsAudio()`.

Fixed-length and chunked request bodies are accepted. Multipart bodies are spooled to a disk-backed temporary stream while they are received, then parsed into bounded text fields and request-owned temporary files. Large multipart uploads therefore no longer require a buffer equal to the complete request size in RAM. JSON, text, and URL-encoded bodies remain memory-backed. `cHTTPX_OnBodyChunk()` replays an already parsed body or upload in bounded chunks.

## Request ID and language

With `request_id_enabled`, a valid incoming `X-Request-ID` is preserved; otherwise the library generates one. It is exposed as `req->request_id`, added to the response, and passed to the logger.

```c
const char* languages[] = {"en", "ru"};
config.languages = languages;
config.languages_count = CHTTPX_ARRAY_LEN(languages);
config.default_language = "en";
```

`Accept-Language` is parsed as a list with quality weights, regional suffixes are reduced (`ru-RU` to `ru`), only configured languages are selected, and the fallback is placed in `req->language`.

## CORS and cookies

```c
const char* origins[] = {"https://example.com"};
cHTTPX_Cors(server, origins, CHTTPX_ARRAY_LEN(origins),
            "GET, POST, PATCH, OPTIONS",
            "Content-Type, Authorization, X-Request-ID");
```

CORS configuration is copied by the server. Cookies use `cHTTPX_CookieGet()` and `cHTTPX_CookieSet()`.

## Logging and rate limiting

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

The library no longer hardcodes a `./logs` directory. The built-in logger writes to stderr; an application callback can integrate any logging system. The rate limiter protects its shared table with a mutex.

## Graceful shutdown and thread safety

Call `cHTTPX_AppShutdown(&app)` from a signal-control thread for SIGINT/SIGTERM handling. It stops accepting, closes the listener, waits for active request threads, frees routes and CORS state, and clears the server.

`current_clients` updates are atomic and the accept loop no longer spins at 100% CPU when capacity is reached. Routes are expected to be configured before listening and are read-only while serving. A request and its request-scoped allocator must only be used by its handling thread. Application-owned shared state still requires application synchronization.

## Error handling

HTTP responses use the correct reason phrase. Socket and I/O helpers return errors. `cHTTPX_SendAll()` handles partial sends and interruptions. Server initialization returns `CHTTPX_ERR_*` codes instead of terminating the process.

## Migration to the App API

- Standalone `cHTTPX_Init()`/`cHTTPX_Listen()`/`cHTTPX_Shutdown()` were removed; create and manage servers only through `cHTTPX_App`.
- `cHTTPX_RegisterRoute()` still works; prefer method helpers.
- `req->context` still works; prefer named contexts.
- `req->filename` remains populated for the first upload; prefer `cHTTPX_RequestFile()`/`cHTTPX_FormFile()`.
- `cHTTPX_Parse()`/`cHTTPX_Validate()` remain available; prefer `cHTTPX_BindJSON()`.
- Do not manually free response bodies or JSON-bound strings anymore.
- Recovery middleware no longer catches fatal process signals.

## Build and tests

```sh
make lin-lib
```

On Windows:

```powershell
make test-win
```

The core test suite covers request cleanup, contexts, JSON bind/normalization, typed params and queries, URL decoding, Bearer parsing, JSON escaping/builder, multipart parsing, temporary-file deletion, MIME helpers, status reasons, and routing middleware metadata.

## License

MIT. See [LICENSE](LICENSE).
