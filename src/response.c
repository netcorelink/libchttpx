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

#include "response.h"

#include "inet.h"
#include "body.h"
#include "http.h"
#include "serv.h"
#include "media.h"
#include "headers.h"
#include "cookies.h"
#include "queries.h"
#include "crosspltm.h"

#include <errno.h>
#include <stdarg.h>

chttpx_response_t cHTTPX_ResJson(uint16_t status, const char* fmt, ...);

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
            req->params_count = count;
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

static const char* generate_etag(const unsigned char* body, size_t body_size);

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
                     res.status, res.content_type, res.body_size);

    /* Etag */
    const char* etag = generate_etag(res.body, res.body_size);
    if (etag)
    {
        n += snprintf(buffer + n, sizeof(buffer) - n, "Etag: %s\r\n", etag);
        free((void*)etag);
    }

    if (allowed_origin)
    {
        n += snprintf(buffer + n, sizeof(buffer) - n,
                      "Access-Control-Allow-Origin: %s\r\n"
                      "Access-Control-Allow-Methods: %s\r\n"
                      "Access-Control-Allow-Headers: %s\r\n",
                      allowed_origin, serv->cors.methods, serv->cors.headers);
    }

    strcat(buffer, "\r\n");

    /* LOG */
    /* --- */
    time_t rawtime;
    struct tm* timeinfo;
    char time_str[64];

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%d/%b/%Y:%H:%M:%S %z", timeinfo);

    printf("[%s] - - [%s] \"%s %s %s\" %d %zu \"%s\"\n", req->client_ip, time_str,
           req->protocol[0] ? req->protocol : "HTTP/1.1", req->method ? req->method : "-",
           req->path ? req->path : "-", res.status, res.body_size,
           req->user_agent[0] ? (const char*)req->user_agent : "-");
    /* --- */
    /* LOG */

    send(client_fd, buffer, strlen(buffer), 0);

    if (res.body && res.body_size > 0)
        send(client_fd, res.body, res.body_size, 0);
}

static void is_method_options(chttpx_request_t* req, int client_fd)
{
    if (strcasecmp(req->method, cHTTPX_MethodOptions) == 0)
    {
        chttpx_response_t res = {.status = cHTTPX_StatusNoContent,
                                 .content_type = cHTTPX_CTYPE_TEXT,
                                 .body = NULL,
                                 .body_size = 0};
        send_response(req, res, client_fd);
        close(client_fd);
    }
}

static chttpx_request_t* parse_req_buffer(int client_fd, char* buffer, size_t received)
{
    chttpx_request_t* req = calloc(1, sizeof(chttpx_request_t));
    if (!req)
    {
        perror("calloc failed");
        return NULL;
    }

    buffer[received] = '\0';

    char method[16], path[MAX_PATH];
    sscanf(buffer, "%15s %4095s", method, path);

    memset(req, 0, sizeof(*req));
    req->method = strdup(method);
    req->path = strdup(path);

    /* Client IP */
    const char* client_ip = cHTTPX_ClientInetIP(client_fd);
    if (client_ip)
    {
        snprintf(req->client_ip, sizeof(req->client_ip), "%s", client_ip);
    }

    /* Parse headers */
    _parse_req_headers(req, buffer, received);

    /* Parse cookies */
    _parse_req_cookies(req);

    /* User-Agent */
    const char* user_agent = cHTTPX_Header(req, "User-Agent");
    if (user_agent)
    {
        snprintf(req->user_agent, sizeof(req->user_agent), "%s", user_agent);
    }

    /* Protocol */
    const char* host = cHTTPX_Header(req, "Host");
    if (host)
    {
        strncpy(req->protocol, "HTTP/1.1", sizeof(req->protocol) - 1);
    }

    /* Parse query request */
    char* query = strchr(req->path, '?');
    if (query)
    {
        *query = '\0';
        _parse_req_query(req, query + 1);
    }

    /* Parse body request */
    _parse_req_body(req, buffer, received);

    /* Parse media request */
    _parse_media(req);

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
                cHTTPX_ResJson(cHTTPX_StatusRequestTimeout, "{\"error\": \"read timeout\"}");
            send_response(&dummy_req, res, client_sock);
        }

        close(client_sock);
        return NULL;
    }

    /* REQUEST */
    chttpx_request_t* req = parse_req_buffer(client_sock, buf, received);
    if (!req)
    {
        close(client_sock);
        return NULL;
    }

    /* ALLOWED OPTIONS METHOD */
    is_method_options(req, client_sock);

    chttpx_route_t* r = find_route(req);
    chttpx_response_t res = {0};

    if (r)
    {
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
        r->handler(req, &res);
    }
    else
    {
        res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"not found\"}");
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

static const char* generate_etag(const unsigned char* body, size_t body_size)
{
    uint64_t hash = 5381;
    for (size_t i = 0; i < body_size; i++)
    {
        hash = ((hash << 5) + hash) + body[i];
    }

    char* buffer = malloc(64);
    if (!buffer)
        return NULL;

    snprintf(buffer, 64, "\"%lx\"", hash);
    return buffer;
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
chttpx_response_t cHTTPX_ResJson(uint16_t status, const char* fmt, ...)
{
    char buffer[BUFFER_SIZE];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    size_t len = strlen(buffer);
    unsigned char* body = malloc(len);
    if (!body)
    {
        perror("malloc failed");
        return (chttpx_response_t){cHTTPX_StatusInternalServerError, cHTTPX_CTYPE_JSON,
                                   (unsigned char*)"{\"error\": \"internal server error\"}", 34};
    }

    memcpy(body, buffer, len);

    return (chttpx_response_t){status, cHTTPX_CTYPE_JSON, body, len};
}

/**
 * Creates an HTTP response with HTML content.
 *
 * This function generates a chttpx_response_t structure with the specified
 * HTTP status code and HTML body. The body is created using a printf-style
 * format string (fmt) and additional arguments. Memory for the body is
 * dynamically allocated and must be freed after sending the response.
 *
 * @param status HTTP status code (e.g., 200, 404, 500).
 * @param fmt Format string containing the HTML content (like printf).
 * @param ... Arguments corresponding to the format string.
 */
chttpx_response_t cHTTPX_ResHtml(uint16_t status, const char* fmt, ...)
{
    char buffer[BUFFER_SIZE];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    size_t len = strlen(buffer);
    unsigned char* body = malloc(len + 1);
    if (!body)
    {
        perror("malloc failed");
        return (chttpx_response_t){cHTTPX_StatusInternalServerError, cHTTPX_CTYPE_HTML,
                                   (unsigned char*)"<h1>Internal Server Error</h1>",
                                   strlen("<h1>Internal Server Error</h1>")};
    }

    memcpy(body, buffer, len);
    body[len] = '\0';

    return (chttpx_response_t){status, cHTTPX_CTYPE_HTML, body, len};
}

/**
 * Create a binary HTTP response (file, media, etc.).
 *
 * Allocates memory for the response body and returns a fully initialized
 * chttpx_response_t structure.
 *
 * @param status HTTP status code (e.g. 200, 400, 404)
 * @param content_type MIME type of the response (e.g. "image/png")
 * @param body Pointer to the data buffer
 * @param body_size Size of the data buffer in bytes
 * @return Initialized chttpx_response_t
 */
chttpx_response_t cHTTPX_ResBinary(uint16_t status, const char* content_type,
                                   const unsigned char* body, size_t body_size)
{
    unsigned char* buffer = malloc(body_size);
    if (!buffer)
    {
        perror("malloc failed");
        return cHTTPX_ResJson(cHTTPX_StatusInternalServerError,
                              "{\"error\": \"internal server error\"}");
    }

    memcpy(buffer, body, body_size);

    return (chttpx_response_t){status, content_type, buffer, body_size};
}

/**
 * Create a binary HTTP response from FILE.
 *
 * @param status HTTP status code (e.g. 200, 400, 404)
 * @param content_type MIME type of the response (e.g. "image/png")
 * @param path Path from return file
 * @return Initialized chttpx_response_t
 */
chttpx_response_t cHTTPX_ResFile(uint16_t status, const char* content_type, const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        return cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"file not found\"}");
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    unsigned char* data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    return (chttpx_response_t){status, content_type, data, size};
}