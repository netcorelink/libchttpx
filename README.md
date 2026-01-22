<p align="center">
  <img alt="golangci-lint logo" src="https://avatars.githubusercontent.com/u/252895549?s=400&u=6c747c431c2844620af7772fcd716ef423a6ab1d&v=4" height="150" />
  <h3 align="center">netcorelink/libchttpx</h3>
  <p align="center">A powerful, cross-platform HTTP server library in C/C++ for building full-featured web servers</p>
</p>

---

`netcorelink/libchttpx` a powerful, cross-platform HTTP server library in C/C++ for building full-featured web servers.

## Linux

```bash
curl -s https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.sh | sudo sh
```

## Windows

```powershell
iwr https://raw.githubusercontent.com/netcorelink/libchttpx/main/scripts/install.ps1 -UseBasicParsing | iex
```

---

<p align="center"><h1 align="center">Documentation</h1></p>

### Initial http server

```c
#include <libchttpx/libchttpx.h>

int main()
{
  chttpx_serv_t serv = {0};

  if (cHTTPX_Init(&serv, 80) != 0) {
    printf("Failed to start server\n");
    return 1;
  }

  // cores
  // middlewares
  // routes

  /* At the very end, to start listening to incoming requests from users. */
  cHTTPX_Listen();
}
```

### Server timeouts settings

```c
/* Timeouts */
serv.read_timeout_sec = 300;
serv.write_timeout_sec = 300;
serv.idle_timeout_sec = 90;
```

### CORS Settings

`origins` – Array of allowed origin strings (e.g. "https://example.com"). Each origin must match exactly the value of the "Origin" header.

`origins_count` – Number of elements in the origins array.

`methods` – Comma-separated list of allowed HTTP methods. If NULL, defaults to: "GET, POST, PUT, DELETE, OPTIONS".

`headers` – Comma-separated list of allowed request headers. If NULL, defaults to: "Content-Type".

```c
/* Cors */
const char *allowed_origins[] = {
  "https://exmaple.ru",
  "http://localhost:8080",
};

cHTTPX_Cors(allowed_origins, cHTTPX_ARRAY_LEN(allowed_origins), NULL, "Accept-Language, Auth");
```

### Middlewares

Example: Middleware for checking the authenticated user.

```c
chttpx_middleware_result_t auth_middleware(chttpx_request_t *req, chttpx_response_t *res) {
  const char *token = cHTTPX_Header(req, "Auth-Token");

  if (!token) {
    *res = cHTTPX_ResJson(cHTTPX_StatusUnauthorized, "{\"error\": \"unauthorized\"}");
    return out;
  }

  return next;
}
```

Middlewares are connected `before cHTTPX_Route`.

```c
cHTTPX_MiddlewareUse(auth_middleware);
```

### Routes

`method` – HTTP method string, e.g., "GET", "POST".

`path` – URL path to match, e.g., "/users".

`handler` – Function pointer to handle the request. The handler should return httpx_response_t. This allows the server to call the appropriate function when a matching request is received.

```c
cHTTPX_Route("GET", "/", home_index);
cHTTPX_Route("GET", "/users/{uuid}/{org}", get_user); // ?org=netcorelink
cHTTPX_Route("POST", "/users", create_user);
```

### Handlers

HTML page return.

```c
void home_index(chttpx_request_t *req, chttpx_response_t *res) {
  *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "<h1>This is home page!</h1>");
}
```

### Http Response

Return Json response

```c
return cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": {\"uuid\": \"%s\", \"page\": \"%s\"}}", uuid, page);
```

Return Html response
Return Media response

<!-- ### Http Request -->

### Parsing JSON fields

```c
typedef struct {
  char *uuid;
  char *password;
  int is_admin;
} user_t;

chttpx_response_t create_user(chttpx_request_t *req) {
  user_t user = {0};

  chttpx_validation_t fields[] = {
    chttpx_validation_str("uuid", &user.uuid, true, 0, 36, VALIDATOR_NONE),
    chttpx_validation_str("password", &user.password, true, 6, 16, VALIDATOR_NONE),
    chttpx_validation_bool("is_admin", &user.is_admin, false),
  };

  if (!cHTTPX_Parse(req, fields, cHTTPX_ARRAY_LEN(fields), "en")) {
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
    goto cleanup;
  }

  /* ... */
}
```

> When working with cHTTPX_Parse, you need to refer to `req->error_msg`.

### Validations fields

Validates an array of `cHTTPX_FieldValidation` structures.

This function ensures that `required` fields are present, `string lengths` are within `limits`,
and basic validation for integers and boolean fields is performed.

> When working with cHTTPX_Validate, you need to refer to `req->error_msg`.

```c
if (!cHTTPX_Validate(req, fields, cHTTPX_ARRAY_LEN(fields), "en")) {
  *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
  goto cleanup;
}
```

Example response by validation:

- Field password is required.
- Field password min length is 6.
- Field password max length is 16.

### Get Headers

```c
const char *origin = cHTTPX_Header(req, "Origin");
```

cHTTPX_Header - Get a request header by name.

`Parameters`:

- req – Pointer to the HTTP request.
- name – Header name (case-insensitive).

### Get Params

> The path must contain the /{uuid} construct.

```c
const char *uuid = cHTTPX_Param(req, "uuid");
```

cHTTPX_Param - Get a route parameter value by its name.

`Parameters`:

- req – Pointer to the HTTP request.
- name – Name of the route parameter (e.g., "uuid").

### Get Query params

```c
const char *sizeParam = cHTTPX_Query(req, "size");
```

cHTTPX_Query - Get a query parameter value by name.

`Parameters`:

- req – Pointer to the HTTP request.
- name – Name of the query parameter.
