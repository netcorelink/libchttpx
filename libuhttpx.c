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

#include "libuhttpx.h"
#include "libuhttpx_utils.h"
#include "lib/cJSON.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static route_t *routes = NULL;
static int routes_count = 0;
static int routes_capacity = 0;
static int server_port = 80;
static int server_fd = -1;

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

/**
 * Find a registered route by HTTP method and path.
 * @param method HTTP method string, e.g., "GET", "POST".
 * @param path URL path to match, e.g., "/users".
 * @return Pointer to the matching route_t if found, NULL otherwise.
 */
static route_t* find_route(const char *method, const char *path) {
    for (int i = 0; i < routes_count; i++) {
        if (strcmp(routes[i].method, method) == 0 && strcmp(routes[i].path, path) == 0) {
            return &routes[i];
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
static void send_response(int client_fd, httpx_response_t res) {
    char buffer[BUFFER_SIZE];

    log_serv("HTTP %d %s %d", res.status, res.content_type, strlen(res.body));

    int len = snprintf(buffer, BUFFER_SIZE, "HTTP/1.1 %d\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s", res.status, res.content_type, strlen(res.body), res.body);
    send(client_fd, buffer, len, 0);
}

/**
 * Initialize the HTTP server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * @param max_routes Maximum number of routes that can be registered.
 * This function must be called before registering routes or starting the server.
 */
void uHTTPX_Init(int port, int max_routes) {
    server_port = port;

    routes_capacity = max_routes;
    routes = (route_t*)malloc(sizeof(route_t) * routes_capacity);
    if (!routes) {
        log_error("Failed to allocate routes");
        exit(1);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 4);

    log_serv("HTTP server started on port %d...", server_port);
}

/**
 * Register a route handler for a specific HTTP method and path.
 * @param method HTTP method string, e.g., "GET", "POST".
 * @param path URL path to match, e.g., "/users".
 * @param handler Function pointer to handle the request. The handler should return httpx_response_t.
 * This allows the server to call the appropriate function when a matching request is received.
 */
void uHTTPX_Route(const char *method, const char *path, httpx_handler_t handler) {
    if (routes_count >= routes_capacity) {
        log_error("Max routes reached");
        return;
    }

    routes[routes_count].method = method;
    routes[routes_count].path = path;
    routes[routes_count].handler = handler;
    routes_count++;
}

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
 * This function reads the request, parses it, calls the matching route handler,
 * and sends the response back to the client.
 */
void uHTTPX_Handle(int client_fd) {
    char buf[BUFFER_SIZE];
    int received = recv(client_fd, buf, BUFFER_SIZE-1, 0);
    if (received <= 0) {
        close(client_fd);
        return;
    }
    buf[received] = 0;

    char method[8], path[128];
    sscanf(buf, "%s %s", method, path);

    route_t *r = find_route(method, path);
    httpx_response_t res;

    if (r) {
        httpx_request_t req;
        req.method = uHTTPX_strdup(method);
        req.path = uHTTPX_strdup(path);
        req.query = NULL;

        // parser body request
        char *body_start = strstr(buf, "\r\n\r\n");
        if (body_start) {
            body_start += 4;
            req.body = body_start;
        }
        else {
            req.body = NULL;
        }

        // logs handle
        log_serv("%s %s", req.method, req.path);

        // fn handler
        res = r->handler(&req);

        free(req.method);
        free(req.path);
    } else {
        res.status = 404;
        res.content_type = "text/plain";
        res.body = "Not Found";
    }

    send_response(client_fd, res);
    close(client_fd);
}

/**
 * Start the server loop to listen for incoming connections.
 * This function blocks indefinitely, accepting new client connections
 * and dispatching them to uHTTPX_Handle.
 */
void uHTTPX_Listen(void) {
    while(1) {
        int client_fd = accept(server_fd, NULL, NULL);
        uHTTPX_Handle(client_fd);
    }
}

int uHTTPX_Parse(httpx_request_t *req, uHTTPX_FieldValidation *fields, size_t field_count, char **error_msg) {
    cJSON *json = cJSON_Parse(req->body);
    if (!json) {
        *error_msg = strdup("Invalid JSON");
        return 0;
    }

    for (size_t i = 0; i < field_count; i++) {
        uHTTPX_FieldValidation *f = &fields[i];
        cJSON *item = cJSON_GetObjectItem(json, f->name);

        if (!item) {
            if (f->required) {
                *error_msg = malloc(128);
                snprintf(*error_msg, 128, "Field '%s' is required", f->name);
                cJSON_Delete(json);
                return 0;
            }

            continue;
        }

        switch (f->type)
        {
        case FIELD_STRING:
            if (!cJSON_IsString(item)) {
                *error_msg = malloc(128);
                snprintf(*error_msg, 128, "Field '%s' must be a string", f->name);
                cJSON_Delete(json);
                return 0;
            }
            if (f->min_length && strlen(item->valuestring) < f->min_length) {
                *error_msg = malloc(128);
                snprintf(*error_msg, 128, "Field '%s' min length is %zu", f->name, f->min_length);
                cJSON_Delete(json);
                return 0;
            }
            if (f->max_length && strlen(item->valuestring) > f->max_length) {
                *error_msg = malloc(128);
                snprintf(*error_msg, 128, "Field '%s' max length is %zu", f->name, f->max_length);
                cJSON_Delete(json);
                return 0;
            }
            *(char **)f->target = item->valuestring;
            break;

        case FIELD_INT:
            if (!cJSON_IsNumber(item)) {
                *error_msg = malloc(128);
                snprintf(*error_msg, 128, "Field '%s' must be an integer", f->name);
                cJSON_Delete(json);
                return 0;
            }
            int val = item->valueint;
            *(int *)f->target = val;
            break;

        case FIELD_BOOL:
            if (!cJSON_IsBool(item)) {
                *error_msg = malloc(128);
                snprintf(*error_msg, 128, "Field '%s' must be a boolean", f->name);
                cJSON_Delete(json);
                return 0;
            }
            *(int *)f->target = cJSON_IsTrue(item);
            break;
        }
    }

    cJSON_Delete(json);
    return 1;
}