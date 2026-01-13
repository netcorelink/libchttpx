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

#include "include/response.h"

#include "include/body.h"
#include "include/crosspltm.h"
#include "include/headers.h"
#include "include/http.h"
#include "include/queries.h"
#include "include/serv.h"

#include <errno.h>
#include <stdarg.h>

chttpx_response_t cHTTPX_JsonResponse(int status, const char* fmt, ...);

static int match_route(const char* template, const char* path, chttpx_param_t* params,
                       int* param_count)
{
    int count = 0;
    const char* t = template;
    const char* p = path;

    while (*t && *p)
    {
        if (*t == '{')
        {
            const char* t_end = strchr(t, '}');
            if (!t_end)
                return 0;

            if (count >= MAX_PARAMS)
                return 0;

            size_t name_len = t_end - t - 1;
            strncpy(params[count].name, t + 1, name_len);
            params[count].name[name_len] = 0;

            const char* slash = strchr(p, '/');

            size_t val_len = slash ? (size_t)(slash - p) : strlen(p);

            if (val_len >= MAX_PARAM_VALUE)
                val_len = MAX_PARAM_VALUE - 1;
            strncpy(params[count].value, p, val_len);
            params[count].value[val_len] = 0;

            count++;
            t = t_end + 1;
            p += val_len;
        }
        else
        {
            if (*t != *p)
                return 0;
            t++;
            p++;
        }
    }

    if (*t || *p)
        return 0;
    *param_count = count;

    return 1;
}

/**
 * Find a registered route by HTTP method and path.
 * @param req  Pointer to the current HTTP request structure.
 * @return Pointer to the matching chttpx_route_t if found, NULL otherwise.
 */
static chttpx_route_t* find_route(chttpx_request_t* req)
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return NULL;
    }

    for (size_t i = 0; i < serv->routes_count; i++)
    {
        if (strcmp(serv->routes[i].method, req->method) != 0)
            continue;

        int count = 0;
        if (match_route(serv->routes[i].path, req->path, req->params, &count))
        {
            req->param_count = count;
            return &serv->routes[i];
        }
    }

    return NULL;
}

static size_t read_req(int fd, char* buffer, size_t buffer_size)
{
    size_t total = 0;

    while (1)
    {
        size_t n = recv(fd, buffer + total, buffer_size - 1 - total, 0);
        if (n <= 0)
            return -1;

        total += n;

        if (total >= buffer_size - 1)
            return -1;
        buffer[total] = '\0';

        if (memmem(buffer, buffer_size, "\r\n\r\n", 4))
            break;
        if (total >= buffer_size - 1)
            return -1;
    }

    int content_length = 0;
    char* cl = memmem(buffer, buffer_size, "Content-Length:", 15);
    if (cl)
    {
        sscanf(cl, "Content-Length: %d", &content_length);
    }

    /* Body */
    char* body_start = memmem(buffer, buffer_size, "\r\n\r\n", 4);
    size_t headers_len = body_start ? (size_t)(body_start - buffer + 4) : total;
    size_t body_in_buf = total - headers_len;

    while (body_in_buf < (size_t)content_length)
    {
        size_t n = recv(fd, buffer + total, buffer_size - 1 - total, 0);
        if (n <= 0)
            return -1;

        total += n;
        body_in_buf += n;
        buffer[total] = '\0';

        if (total >= buffer_size - 1)
            return -1;
    }

    return total;
}

static void set_client_timeout(int client_fd)
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    /* Read timeout params */
    struct timeval tv;
    tv.tv_sec = serv->read_timeout_sec;
    tv.tv_usec = 0;
#ifdef _WIN32
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    /* Write timeout params */
    tv.tv_sec = serv->write_timeout_sec;
#ifdef _WIN32
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
#else
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static const char* allowed_origin_cors(const char* req_origin)
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return NULL;
    }

    if (!serv->cors.enabled || !req_origin)
    {
        return NULL;
    }

    for (size_t i = 0; i < serv->cors.origins_count; i++)
    {
        if (strcmp(serv->cors.origins[i], req_origin) == 0)
        {
            return serv->cors.origins[i];
        }
    }

    return NULL;
}

/**
 * Send an HTTP response to a connected client socket.
 * @param req Pointer to the HTTP request.
 * @param res httpx_response_t structure containing status, content type, and body.
 * @param client_fd File descriptor of the connected client socket.
 *
 * This function formats the HTTP response headers and body according to HTTP/1.1.
 */
static void send_response(chttpx_request_t* req, chttpx_response_t res, int client_fd)
{
    char buffer[BUFFER_SIZE];

    /* Cors */
    const char* allowed_origin = req ? allowed_origin_cors(cHTTPX_Header(req, "Origin")) : NULL;

    int n = snprintf(buffer, sizeof(buffer),
                     "HTTP/1.1 %d OK\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n",
                     res.status, res.content_type, strlen(res.body));

    if (allowed_origin)
    {
        n += snprintf(buffer + n, sizeof(buffer) - n,
                      "Access-Control-Allow-Origin: %s\r\n"
                      "Access-Control-Allow-Methods: %s\r\n"
                      "Access-Control-Allow-Headers: %s\r\n",
                      allowed_origin, serv->cors.methods, serv->cors.headers);
    }

    strcat(buffer, "\r\n");

    printf("HTTP/1.1 %d %s %zu\n", res.status, res.content_type, strlen(res.body));

    send(client_fd, buffer, strlen(buffer), 0);
    send(client_fd, res.body, strlen(res.body), 0);
}

static void is_method_options(chttpx_request_t* req, int client_fd)
{
    if (strcasecmp(req->method, cHTTPX_MethodOptions) == 0)
    {
        chttpx_response_t res = {.status = 204, .content_type = "text/plain", .body = ""};
        send_response(req, res, client_fd);
        close(client_fd);
    }
}

static chttpx_request_t* parse_req_buffer(char* buffer, size_t received)
{
    chttpx_request_t* req = calloc(1, sizeof(chttpx_request_t));
    if (!req)
    {
        perror("calloc failed");
        return NULL;
    }

    buffer[received] = '\0';

    char method[16], path[MAX_PATH];
    sscanf(buffer, "%15s %4096s", method, path);

    memset(req, 0, sizeof(*req));
    req->method = strdup(method);
    req->path = strdup(path);

    /* Parse headers */
    _parse_req_headers(req, buffer, received);

    /* Parse query request */
    char* query = strchr(req->path, '?');
    if (query)
    {
        *query = '\0';
        _parse_req_query(req, query + 1);
    }

    /* Parse body request */
    _parse_req_body(req, buffer, received);

    return req;
}

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
 * This function reads the request, parses it, calls the matching route handler,
 * and sends the response back to the client.
 */
void* chttpx_handle(void* arg)
{
    int client_sock = *(int*)arg;
    free(arg);

    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return NULL;
    }

    /* Timeouts */
    set_client_timeout(client_sock);

    char buf[BUFFER_SIZE];
    size_t received = read_req(client_sock, buf, BUFFER_SIZE);
    if (received <= 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
        {
            chttpx_request_t dummy_req = {0};
            chttpx_response_t res =
                cHTTPX_JsonResponse(cHTTPX_StatusRequestTimeout, "{\"error\": \"read timeout\"}");
            send_response(&dummy_req, res, client_sock);
        }

        close(client_sock);
        return NULL;
    }

    /* REQUEST */
    chttpx_request_t* req = parse_req_buffer(buf, received);
    if (!req)
    {
        close(client_sock);
        return NULL;
    }

    /* ALLOWED OPTIONS METHOD */
    is_method_options(req, client_sock);

    chttpx_route_t* r = find_route(req);
    chttpx_response_t res;

    if (r)
    {
        /* Logger */
        printf("%s %s\n", req->method, req->path);

        /* Use middlewares */
        for (size_t i = 0; i < serv->middleware.middleware_count; i++)
        {
            if (!serv->middleware.middlewares[i](req, &res))
            {
                send_response(req, res, client_sock);
                close(client_sock);
                return NULL;
            }
        }

        /* Handler */
        res = r->handler(req);
    }
    else
    {
        res = cHTTPX_JsonResponse(cHTTPX_StatusNotFound, "{\"error\": \"not found\"}");
    }

    send_response(req, res, client_sock);

    free(req->method);
    free(req->path);
    free(req->body);

    for (size_t i = 0; i < req->query_count; i++)
    {
        free(req->query[i].name);
        free(req->query[i].value);
    }

    free(req->query);
    free(req);

    close(client_sock);
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
chttpx_response_t cHTTPX_JsonResponse(int status, const char* fmt, ...)
{
    char buffer[BUFFER_SIZE];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    return (chttpx_response_t){status, cHTTPX_CTYPE_JSON, strdup(buffer)};
}
