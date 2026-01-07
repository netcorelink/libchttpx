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
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

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
static route_t* find_route(chttpx_server_t *serve, chttpx_request_t *req) {
    for (size_t i = 0; i < serve->routes_count; i++) {
        if (strcmp(serve->routes[i].method, req->method) != 0) continue;

        int count = 0;
        if (match_route(serve->routes[i].path, req->path, req->params, &count)) {
            req->param_count = count;
            return &serve->routes[i];
        }
    }

    return NULL;
}

static void parse_req_query(char *query, chttpx_request_t *req) {
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

static void parse_req_body(char *buffer, chttpx_request_t *req) {
    const char *body_start = strstr(buffer, "\r\n\r\n");
    if (!body_start) {
        req->body = NULL;
        return;
    }
    
    body_start += 4;
    req->body = cHTTPX_strdup(body_start);
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

    /* Parse query request */
    char *query = strchr(req->path, '?');
    if (query) {
        *query = '\0';
        parse_req_query(query + 1, req);
    }

    /* Parse body request */
    parse_req_body(buffer, req);

    return req;
}

/**
 * Send an HTTP response to a connected client socket.
 * @param client_fd File descriptor of the connected client socket.
 * @param res httpx_response_t structure containing status, content type, and body.
 * 
 * This function formats the HTTP response headers and body according to HTTP/1.1.
 */
static void send_response(int client_fd, chttpx_response_t res) {
    char buffer[BUFFER_SIZE];

    log_serv("HTTP/1.1 %d %s %d", res.status, res.content_type, strlen(res.body));

    int len = snprintf(buffer, BUFFER_SIZE, "HTTP/1.1 %d\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s", res.status, res.content_type, strlen(res.body), res.body);
    send(client_fd, buffer, len, 0);
}

/**
 * Initialize the HTTP server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * @param max_routes Maximum number of routes that can be registered.
 * This function must be called before registering routes or starting the server.
 */
int cHTTPX_Init(chttpx_server_t *serve, int port) {
    serve->port = port;
    serve->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (serve->server_fd < 0) {
        perror("socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(serve->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serve->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(serve->server_fd, 128) < 0) {
        perror("listen");
        exit(1);
    }

    /* Timeouts */
    serve->read_timeout_sec = 300;
    serve->write_timeout_sec = 300;
    serve->idle_timeout_sec = 90;

    /* Default values for routes */
    serve->routes = NULL;
    serve->routes_count = 0;
    serve->routes_capacity = 0;

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
void cHTTPX_Route(chttpx_server_t *serve, const char *method, const char *path, chttpx_handler_t handler) {
    if (serve->routes_count == serve->routes_capacity) {
        size_t new_capacity = (serve->routes_capacity == 0) ? 4 : serve->routes_capacity * 2;
        route_t *new_routes = realloc(serve->routes, sizeof(route_t) * new_capacity);

        if (!new_routes) {
            perror("realloc routes");
            exit(1);
        }

        serve->routes = new_routes;
        serve->routes_capacity = new_capacity;
    }

    serve->routes[serve->routes_count].method = method;
    serve->routes[serve->routes_count].path = path;
    serve->routes[serve->routes_count].handler = handler;
    serve->routes_count++;
}

static void set_client_timeout(chttpx_server_t *serv, int client_fd) {
    /* Read timeout params */
    struct timeval tv;
    tv.tv_sec = serv->read_timeout_sec;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* Write timeout params */
    tv.tv_sec = serv->write_timeout_sec;
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
 * This function reads the request, parses it, calls the matching route handler,
 * and sends the response back to the client.
 */
void cHTTPX_Handle(chttpx_server_t *serv, int client_fd) {
    /* Timeouts */
    set_client_timeout(serv, client_fd);

    char buf[BUFFER_SIZE];
    int received = recv(client_fd, buf, BUFFER_SIZE-1, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            chttpx_response_t res = cHTTPX_JsonResponse(cHTTPX_StatusRequestTimeout, "{\"error\": \"Read Timeout\"}");
            send_response(client_fd, res);
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

    route_t *r = find_route(serv, req);
    chttpx_response_t res;

    if (r) {
        log_serv("%s %s", req->method, req->path);
        res = r->handler(req);
    } else {
        res = cHTTPX_JsonResponse(cHTTPX_StatusNotFound, "{\"error\": \"NotFound\"}");
    }

    send_response(client_fd, res);

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
void cHTTPX_Listen(chttpx_server_t *serve) {
    while(1) {
        int client_fd = accept(serve->server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        cHTTPX_Handle(serve, client_fd);
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