<p align="center">
  <img alt="golangci-lint logo" src="https://avatars.githubusercontent.com/u/252895549?s=400&u=6c747c431c2844620af7772fcd716ef423a6ab1d&v=4" height="150" />
  <h3 align="center">netcorelink/libchttpx</h3>
  <p align="center">A powerful, cross-platform HTTP server library in C/C++ for building full-featured web servers</p>
</p>

---

`netcorelink/libchttpx` a powerful, cross-platform HTTP server library in C/C++ for building full-featured web servers. 

## Install `libchttpx`

Copy the `cHTTPX/` library folder to your project to work with cHTTPX.

## A quick example

### Initial HTTP server

```c
int main()
{
  chttpx_server_t serv;

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

cHTTPX_Cors(allowed_origins, cHTTPX_ARRAY_LEN(allowed_origins), NULL, NULL);
```

### Middlewares

Example: Middleware for checking the authenticated user.

```c
int auth_middleware(chttpx_request_t *req, chttpx_response_t *res) {
  const char *token = cHTTPX_Header(req, "Auth-Token");

  if (!token) {
    *res = cHTTPX_JsonResponse(cHTTPX_StatusUnauthorized, "{\"error\": \"unauthorized\"}");
    return 0;
  }

  return 1;
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

> Return type from `chttpx_response_t` functions.

```c
chttpx_response_t home_index(chttpx_request_t *req) {
  return (chttpx_response_t){cHTTPX_StatusOK, cHTTPX_CTYPE_HTML, "<h1>This is home page!</h1>"};
}
```

#### `Http Response`
#### `Http Request`
#### `Parsing JSON fields`
#### `Validations fields`
#### `Get Headers`

```c
const char *origin = cHTTPX_Header(req, "Origin");
```

cHTTPX_Header - Get a request header by name.

`Parameters`:
- req – Pointer to the HTTP request.
- name – Header name (case-insensitive).

#### `Get Params`

> The path must contain the /{uuid} construct.

```c
const char *uuid = cHTTPX_Param(req, "uuid");
```

cHTTPX_Param - Get a route parameter value by its name.

`Parameters`:
- req – Pointer to the HTTP request.
- name – Name of the route parameter (e.g., "uuid").

#### `Get Query params`

```c
const char *sizeParam = cHTTPX_Query(req, "size");
```

cHTTPX_Query - Get a query parameter value by name.

`Parameters`:
- req – Pointer to the HTTP request.
- name – Name of the query parameter.