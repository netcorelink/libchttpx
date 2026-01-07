/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "libchttpx.h"
#include "libchttpx_utils.h"
#include "include/cJSON.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#ifdef __WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <unistd.h>
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <sys/time.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
    #define strcasecmp _stricmp
#endif

static chttpx_server_t *serv = NULL;

/**
 * Logger 'log_serv' and 'log_error'
 * example: 20:18:26 [SERV] Hello world
 */
static void log_serv(const char *fmt, ...) {
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_info);

    printf("%s [SERV] ", time_buf);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
}

static void log_error(const char *fmt, ...) {
    time_t t = time(NULL);
    struct tm tm_info;
    localtime_r(&t, &tm_info);

    char time_buf[16];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &tm_info);

    printf("%s [ERROR] ", time_buf);

    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    printf("\n");
}

static int match_route(const char *template, const char *path, chttpx_param_t *params, int *param_count) {
    int count = 0;
    const char *t = template;
    const char *p = path;

    while (*t && *p) {
        if (*t == '{') {
            const char *t_end = strchr(t, '}');
            if (!t_end) return 0;

            if (count >= MAX_ROUTE_PARAMS) return 0;

            size_t name_len = t_end - t - 1;
            strncpy(params[count].name, t+1, name_len);
            params[count].name[name_len] = 0;

            const char *slash = strchr(p, '/');

            size_t val_len = slash ? (size_t)(slash - p) : strlen(p);

            if (val_len >= MAX_PARAM_VALUE) val_len = MAX_PARAM_VALUE-1;
            strncpy(params[count].value, p, val_len);
            params[count].value[val_len] = 0;

            count++;
            t = t_end + 1;
            p += val_len;
        }
        else {
            if (*t != *p) return 0;
            t++; p++;
        }
    }

    if (*t || *p) return 0;
    *param_count = count;

    return 1;
}

/**
 * Find a registered route by HTTP method and path.
 * @param method HTTP method string, e.g., "GET", "POST".
 * @param path URL path to match, e.g., "/users".
 * @return Pointer to the matching route_t if found, NULL otherwise.
 */
static route_t* find_route(chttpx_request_t *req) {
    if (!serv) {
        log_error("the server is not initialized");
        return NULL;
    }

    for (size_t i = 0; i < serv->routes_count; i++) {
        if (strcmp(serv->routes[i].method, req->method) != 0) continue;

        int count = 0;
        if (match_route(serv->routes[i].path, req->path, req->params, &count)) {
            req->param_count = count;
            return &serv->routes[i];
        }
    }

    return NULL;
}

static void parse_req_query(chttpx_request_t *req, char *query) {
    char *token = strtok(query, "&");

    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';

            req->query = realloc(req->query, sizeof(chttpx_query_t) * (req->query_count + 1));

            req->query[req->query_count].name = cHTTPX_strdup(token);
            req->query[req->query_count].value = cHTTPX_strdup(eq+1);
            req->query_count++;
        }

        token = strtok(NULL, "&");
    }
}

static void parse_req_body(chttpx_request_t *req, char *buffer) {
    const char *body_start = strstr(buffer, "\r\n\r\n");
    if (!body_start) {
        req->body = NULL;
        return;
    }
    
    body_start += 4;
    req->body = cHTTPX_strdup(body_start);
}

static void add_header(chttpx_request_t *req, const char *name, const char *value) {
    if (req->headers_count >= MAX_HEADERS) return;

    chttpx_header_t *h = &req->headers[req->headers_count++];

    strncpy(h->name, name, MAX_HEADER_NAME - 1);
    h->name[MAX_HEADER_NAME - 1] = '\0';

    strncpy(h->value, value, MAX_HEADER_VALUE - 1);
    h->value[MAX_HEADER_VALUE - 1] = '\0';
}

static void parse_req_headers(chttpx_request_t *req, char *buffer) {
    char *line = buffer;

    /* Meta data - continue one line */
    char *next = strstr(line, "\r\n");
    if (!next) return;
    line = next + 2;

    while (*line) {
        next = strstr(line, "\r\n");
        if (!next) break;
        *next = 0;

        char *colon = strchr(line, ':');
        if (colon) {
            *colon = 0;

            const char *name = line;
            const char *value = colon + 1;
            while (*value == ' ') value++;

            add_header(req, name, value);
        }

        line = next + 2;
    }
}

static chttpx_request_t* parse_req_buffer(char *buffer, int received) {
    chttpx_request_t *req = malloc(sizeof(chttpx_request_t));
    if (!req) return NULL;

    buffer[received] = '\0';

    char method[8], path[128];
    sscanf(buffer, "%s %s", method, path);

    memset(req, 0, sizeof(*req));
    req->method = cHTTPX_strdup(method);
    req->path   = cHTTPX_strdup(path);

    /* Parse headers */
    parse_req_headers(req, buffer);

    /* Parse query request */
    char *query = strchr(req->path, '?');
    if (query) {
        *query = '\0';
        parse_req_query(req, query + 1);
    }

    /* Parse body request */
    parse_req_body(req, buffer);

    return req;
}

static char* allowed_origin_cors(const char *req_origin) {
    if (!serv) {
        log_error("the server is not initialized");
        return NULL;
    }

    if (!serv->cors.enabled || !req_origin) {
        return NULL;
    }

    for (size_t i = 0; i < serv->cors.origins_count; i++) {
        if (strcmp(serv->cors.origins[i], req_origin) == 0) {
            return serv->cors.origins[i];
        }
    }

    return NULL;
}

/**
 * Send an HTTP response to a connected client socket.
 * @param client_fd File descriptor of the connected client socket.
 * @param res httpx_response_t structure containing status, content type, and body.
 * 
 * This function formats the HTTP response headers and body according to HTTP/1.1.
 */
static void send_response(chttpx_request_t *req, chttpx_response_t res, int client_fd) {
    char buffer[BUFFER_SIZE];

    /* Cors */
    const char *allowed_origin = req ? allowed_origin_cors(cHTTPX_Header(req, "Origin")) : NULL;

    int n = snprintf(buffer, sizeof(buffer), "HTTP/1.1 %d OK\r\n" 
                                             "Content-Type: %s\r\n" 
                                             "Content-Length: %zu\r\n", 
                                             res.status, res.content_type, strlen(res.body));

    if (allowed_origin) {
        n += snprintf(buffer + n, sizeof(buffer) - n, "Access-Control-Allow-Origin: %s\r\n" 
                                                      "Access-Control-Allow-Methods: %s\r\n" 
                                                      "Access-Control-Allow-Headers: %s\r\n", 
                                                      allowed_origin, serv->cors.methods, serv->cors.headers);
    }

    strcat(buffer, "\r\n");

    log_serv("HTTP/1.1 %d %s %d", res.status, res.content_type, strlen(res.body));

    send(client_fd, buffer, strlen(buffer), 0);
    send(client_fd, res.body, strlen(res.body), 0);
}

/**
 * Initialize the HTTP server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * @param max_routes Maximum number of routes that can be registered.
 * This function must be called before registering routes or starting the server.
 */
int cHTTPX_Init(chttpx_server_t *serv_p, int port) {
    serv = serv_p;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        perror("WSAStartup");
        return -1;
    }
#endif

    serv->port = port;
    serv->server_fd = socket(AF_INET, SOCK_STREAM, 0);

#ifdef _WIN32
    if (serv->server_fd == INVALID_SOCKET)
#else
    if (serv->server_fd < 0)
#endif
    {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(
        serv->server_fd, 
        SOL_SOCKET, 
        SO_REUSEADDR, 
#ifdef _WIN32
        (const char*)&opt,
#else
        &opt, 
#endif
        sizeof(opt)
    );

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serv->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(serv->server_fd, 128) < 0) {
        perror("listen");
        exit(1);
    }

    /* Timeouts */
    serv->read_timeout_sec = 300;
    serv->write_timeout_sec = 300;
    serv->idle_timeout_sec = 90;

    /* Default values for routes */
    serv->routes = NULL;
    serv->routes_count = 0;
    serv->routes_capacity = 0;

    log_serv("HTTP server started on port %d...", port);

    return 0;
}

/**
 * Register a route handler for a specific HTTP method and path.
 * @param method HTTP method string, e.g., "GET", "POST".
 * @param path URL path to match, e.g., "/users".
 * @param handler Function pointer to handle the request. The handler should return httpx_response_t.
 * This allows the server to call the appropriate function when a matching request is received.
 */
void cHTTPX_Route(const char *method, const char *path, chttpx_handler_t handler) {
    if (!serv) {
        log_error("the server is not initialized");
        return;
    }

    if (serv->routes_count == serv->routes_capacity) {
        size_t new_capacity = (serv->routes_capacity == 0) ? 4 : serv->routes_capacity * 2;
        route_t *new_routes = realloc(serv->routes, sizeof(route_t) * new_capacity);

        if (!new_routes) {
            perror("realloc routes");
            exit(1);
        }

        serv->routes = new_routes;
        serv->routes_capacity = new_capacity;
    }

    serv->routes[serv->routes_count].method = method;
    serv->routes[serv->routes_count].path = path;
    serv->routes[serv->routes_count].handler = handler;
    serv->routes_count++;
}

static void set_client_timeout(int client_fd) {
    if (!serv) {
        log_error("the server is not initialized");
        return;
    }

    /* Read timeout params */
    struct timeval tv;
    tv.tv_sec = serv->read_timeout_sec;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Write timeout params */
    tv.tv_sec = serv->write_timeout_sec;
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

static void is_method_options(chttpx_request_t *req, int client_fd) {
    if (strcasecmp(req->method, cHTTPX_MethodOptions) == 0) {
        chttpx_response_t res = {.status = 204, .content_type = "text/plain", .body = ""};
        send_response(req, res, client_fd);
        close(client_fd);
    }
}

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
 * This function reads the request, parses it, calls the matching route handler,
 * and sends the response back to the client.
 */
void cHTTPX_Handle(int client_fd) {
    if (!serv) {
        log_error("the server is not initialized");
        return;
    }

    /* Timeouts */
    set_client_timeout(client_fd);

    char buf[BUFFER_SIZE];
    int received = recv(client_fd, buf, BUFFER_SIZE-1, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            chttpx_request_t dummy_req = {0};
            chttpx_response_t res = cHTTPX_JsonResponse(cHTTPX_StatusRequestTimeout, "{\"error\": \"Read Timeout\"}");
            send_response(&dummy_req, res, client_fd);
        }

        close(client_fd);
        return;
    }

    /* REQUEST */
    chttpx_request_t *req = parse_req_buffer(buf, received);
    if (!req) {
        close(client_fd);
        return;
    }

    /* ALLOWED OPTIONS METHOD */
    is_method_options(req, client_fd);

    route_t *r = find_route(req);
    chttpx_response_t res;

    if (r) {
        log_serv("%s %s", req->method, req->path);
        res = r->handler(req);
    } else {
        res = cHTTPX_JsonResponse(cHTTPX_StatusNotFound, "{\"error\": \"NotFound\"}");
    }

    send_response(req, res, client_fd);

    free(req->method);
    free(req->path);
    free(req->body);

    for (size_t i = 0; i < req->query_count; i++) {
        free(req->query[i].name);
        free(req->query[i].value);
    }

    free(req->query);

    close(client_fd);
}

/**
 * Start the server loop to listen for incoming connections.
 * This function blocks indefinitely, accepting new client connections
 * and dispatching them to cHTTPX_Handle.
 */
void cHTTPX_Listen() {
    if (!serv) {
        log_error("the server is not initialized");
        return;
    }

    while(1) {
        int client_fd = accept(serv->server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        cHTTPX_Handle(client_fd);
    }
}

/**
 * Parse a JSON body and validate fields according to the provided definitions.
 * @param body JSON string to parse.
 * @param fields Array of field validation definitions (cHTTPX_FieldValidation).
 * @param field_count Number of fields in the array.
 * @param error_msg Output pointer to a string describing the first validation error, if any.
 * @return 1 if parsing and validation succeed, 0 if there is an error.
 * This function automatically checks required fields, string length, boolean types, etc.
 */
int cHTTPX_Parse(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count) {
    cJSON *json = cJSON_Parse(req->body);
    if (!json) {
        snprintf(req->error_msg, sizeof(req->error_msg), "Invalid JSON");
        return 0;
    }

    for (size_t i = 0; i < field_count; i++) {
        chttpx_validation_t *f = &fields[i];
        cJSON *item = cJSON_GetObjectItem(json, f->name);

        if (!item) {
            continue;
        }

        switch (f->type)
        {
        case FIELD_STRING:
            if (cJSON_IsString(item)) {
                *(char **)f->target = cHTTPX_strdup(item->valuestring);
            }
            break;

        case FIELD_INT:
            if (cJSON_IsNumber(item)) {
                *(int *)f->target = item->valueint;
            }
            break;

        case FIELD_BOOL:
            if (cJSON_IsBool(item)) {
                *(int *)f->target = cJSON_IsTrue(item);
            }
            break;
        }
    }

    cJSON_Delete(json);
    return 1;
}

/*
 * Validates an array of cHTTPX_FieldValidation structures.
 * This function ensures that required fields are present, string lengths are within limits,
 * and basic validation for integers and boolean fields is performed.
 */
int cHTTPX_Validate(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count) {
    for (size_t i = 0; i < field_count; i++) {
        chttpx_validation_t *f = &fields[i];

        switch (f->type)
        {
        case FIELD_STRING:
            char *v = *(char **)f->target;

            if (!v) {
                if (f->required) {
                    snprintf(req->error_msg, sizeof(req->error_msg), "Field '%s' is required", f->name);
                    return 0;
                }
                break;
            }

            size_t len = strlen(v);

            if (f->min_length && len < f->min_length) {
                snprintf(req->error_msg, sizeof(req->error_msg), "Field '%s' min length is %zu", f->name, f->min_length);
                return 0;
            }

            if (f->max_length && len > f->max_length) {
                snprintf(req->error_msg, sizeof(req->error_msg), "Field '%s' max length is %zu", f->name, f->max_length);
                return 0;
            }

            break;

        case FIELD_INT:
        case FIELD_BOOL:
            if (f->required && !*(int *)f->target) {
                snprintf(req->error_msg, sizeof(req->error_msg), "Field '%s' is required", f->name);
                return 0;
            }
            break;
        }
    }

    return 1;
}

/**
 * Get a request header by name.
 * @param req Pointer to the HTTP request.
 * @param name Header name (case-insensitive).
 * @return Pointer to header value if found, otherwise NULL.
 */
const char* cHTTPX_Header(chttpx_request_t *req, const char *name) {
    if (!req || req->headers_count == 0 || !name) return NULL;

    for (size_t i = 0; i < req->headers_count; i++) {
        if (strcasecmp(req->headers[i].name, name) == 0) {
            return req->headers[i].value;
        }
    }

    return NULL;
}

/**
 * Get a route parameter value by its name.
 * @param req  Pointer to the current HTTP request structure.
 * @param name Name of the route parameter (e.g., "uuid").
 *
 * @return Pointer to the parameter value string if found, or NULL if the parameter does not exist.
 */
const char* cHTTPX_Param(chttpx_request_t *req, const char *name) {
    if (!req || !name || req->param_count == 0) return NULL;

    for (size_t i = 0; i < req->param_count; i++) {
        if (strcmp(req->params[i].name, name) == 0) {
            return req->params[i].value;
        }
    }

    return NULL;
}

/**
 * Get a query parameter value by name.
 *
 * Searches the parsed URL query parameters (e.g. ?name=value&age=10)
 * and returns the value associated with the given parameter name.
 * 
 * @param req   Pointer to the current HTTP request.
 * @param name  Name of the query parameter.
 * @return Pointer to the parameter value string if found, or NULL if not present.
 */
const char* cHTTPX_Query(chttpx_request_t *req, const char *name) {
    if (!req || !req->query || req->query_count == 0) return NULL;

    for (size_t i = 0; i < req->query_count; i++) {
        if (strcmp(req->query[i].name, name) == 0) {
            return req->query[i].value;
        }
    }

    return NULL;
}

void cHTTPX_Cors(const char **origins, size_t origins_count, const char *methods, const char *headers) {
    if (!serv) {
        log_error("the server is not initialized");
        return;
    }

    serv->cors.enabled = 1;
    serv->cors.origins = origins;
    serv->cors.origins_count = origins_count;
    serv->cors.methods = methods ? methods : "GET, POST, PUT, DELETE, OPTIONS";
    serv->cors.headers = headers ? headers : "Content-Type";
}

/**
 * Create a JSON HTTP response with formatted content.
 *
 * Formats a JSON response body using printf-style arguments,
 * allocates memory for the response body, and returns a
 * fully initialized chttpx_response_t structure.
 *
 * @param status HTTP status code (e.g. 200, 400, 404).
 * @param fmt    printf-style format string for the JSON body.
 * @param ...    Format arguments.
 */
chttpx_response_t cHTTPX_JsonResponse(int status, const char *fmt, ...) {
    char buffer[BUFFER_SIZE];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    return (chttpx_response_t){status, cHTTPX_CTYPE_JSON, cHTTPX_strdup(buffer)};
}