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
#include <ctype.h>
#include <limits.h>

int cHTTPX_SendAll(chttpx_socket_t fd, const void* data, size_t size)
{
    const unsigned char* cursor = data;
    size_t sent = 0;

#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
#endif

    while (sent < size)
    {
        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        size_t wanted = size - sent;
#ifdef CHTTPX_PLATFORM_WINDOWS
        if (wanted > INT_MAX)
            wanted = INT_MAX;
#endif
        ssize_t result = send(fd, (const char*)cursor + sent, wanted, flags);
        if (result < 0)
        {
#ifdef CHTTPX_PLATFORM_POSIX
            if (errno == EINTR)
                continue;
#endif
            return -1;
        }
        if (result == 0)
            return -1;
        sent += (size_t)result;
    }
    return 0;
}

const char* cHTTPX_StatusReason(uint16_t status)
{
    switch (status)
    {
    case 100:
        return "Continue";
    case 101:
        return "Switching Protocols";
    case 102:
        return "Processing";
    case 103:
        return "Early Hints";
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 203:
        return "Non-Authoritative Information";
    case 204:
        return "No Content";
    case 205:
        return "Reset Content";
    case 206:
        return "Partial Content";
    case 207:
        return "Multi-Status";
    case 208:
        return "Already Reported";
    case 226:
        return "IM Used";
    case 300:
        return "Multiple Choices";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 305:
        return "Use Proxy";
    case 306:
        return "Unused";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 402:
        return "Payment Required";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 406:
        return "Not Acceptable";
    case 407:
        return "Proxy Authentication Required";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 410:
        return "Gone";
    case 411:
        return "Length Required";
    case 412:
        return "Precondition Failed";
    case 413:
        return "Payload Too Large";
    case 414:
        return "URI Too Long";
    case 415:
        return "Unsupported Media Type";
    case 416:
        return "Range Not Satisfiable";
    case 417:
        return "Expectation Failed";
    case 418:
        return "I'm a Teapot";
    case 419:
        return "Authentication Timeout";
    case 421:
        return "Misdirected Request";
    case 422:
        return "Unprocessable Entity";
    case 423:
        return "Locked";
    case 424:
        return "Failed Dependency";
    case 425:
        return "Too Early";
    case 426:
        return "Upgrade Required";
    case 428:
        return "Precondition Required";
    case 429:
        return "Too Many Requests";
    case 431:
        return "Request Header Fields Too Large";
    case 449:
        return "Retry With";
    case 451:
        return "Unavailable For Legal Reasons";
    case 499:
        return "Client Closed Request";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    case 505:
        return "HTTP Version Not Supported";
    case 506:
        return "Variant Also Negotiates";
    case 507:
        return "Insufficient Storage";
    case 508:
        return "Loop Detected";
    case 509:
        return "Bandwidth Limit Exceeded";
    case 510:
        return "Not Extended";
    case 511:
        return "Network Authentication Required";
    case 520:
        return "Unknown Error";
    case 521:
        return "Web Server Is Down";
    case 522:
        return "Connection Timed Out";
    case 523:
        return "Origin Is Unreachable";
    case 524:
        return "A Timeout Occurred";
    case 525:
        return "SSL Handshake Failed";
    case 526:
        return "Invalid SSL Certificate";
    default:
        return "Unknown Status";
    }
}

chttpx_response_t cHTTPX_ResJson(uint16_t status, const char* fmt, ...);

static int match_route(const char* template, const char* path, chttpx_param_t* params, int* param_count)
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
            if (name_len == 0 || name_len >= MAX_PARAM_NAME)
                return 0;
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
        chttpx_route_t* registered = serv->routes[i];
        if (!registered || strcmp(registered->method, req->method) != 0)
            continue;

        int count = 0;
        if (match_route(registered->path, req->path, req->params, &count))
        {
            req->params_count = count;
            return registered;
        }
    }

    return NULL;
}

static ssize_t read_req(chttpx_socket_t fd, char* buffer, size_t buffer_size)
{
    size_t total = 0;
    size_t limit = serv && serv->max_header_size && serv->max_header_size < buffer_size ? serv->max_header_size : buffer_size - 1;

    while (1)
    {
        if (total >= buffer_size - 1)
            return -2;

        ssize_t n = recv(fd, buffer + total, buffer_size - 1 - total, 0);
        if (n < 0)
        {
#ifdef CHTTPX_PLATFORM_POSIX
            if (errno == EINTR)
                continue;
#endif
            return -1;
        }
        if (n == 0)
            return -1;

        total += (size_t)n;
        buffer[total] = '\0';

        char* delimiter = chttpx_memmem(buffer, total, "\r\n\r\n", 4);
        if (delimiter)
        {
            size_t header_size = (size_t)(delimiter - buffer) + 4;
            return header_size > limit ? -2 : (ssize_t)total;
        }

        /*
         * If the complete header delimiter is not present by the configured
         * limit, the header block itself is already too large. Body bytes
         * received together with a valid small header are intentionally not
         * counted against max_header_size.
         */
        if (total >= limit)
            return -2;
    }
}

static void set_client_timeout(chttpx_socket_t client_fd)
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

#ifdef CHTTPX_PLATFORM_WINDOWS
    /*
     * Winsock expects SO_RCVTIMEO/SO_SNDTIMEO as millisecond DWORD values,
     * unlike POSIX where these options use struct timeval.
     */
    DWORD read_timeout_ms = (DWORD)serv->read_timeout_sec * 1000U;
    DWORD write_timeout_ms = (DWORD)serv->write_timeout_sec * 1000U;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&read_timeout_ms, sizeof(read_timeout_ms));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&write_timeout_ms, sizeof(write_timeout_ms));
#else
    struct timeval tv;
    tv.tv_usec = 0;

    tv.tv_sec = serv->read_timeout_sec;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    tv.tv_sec = serv->write_timeout_sec;
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/* Cors */
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

/* Etag for response cache */
static const char* generate_etag(const unsigned char* body, size_t body_size);

static int append_response_header(char* buffer, size_t capacity, size_t* length, const char* format, ...)
{
    if (*length >= capacity)
        return 0;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + *length, capacity - *length, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *length)
        return 0;
    *length += (size_t)written;
    return 1;
}

/**
 * Send an HTTP response to a connected client socket.
 * @param req Pointer to the HTTP request.
 * @param res httpx_response_t structure containing status, content type, and body.
 * @param client_fd File descriptor of the connected client socket.
 *
 * This function formats the HTTP response headers and body according to HTTP/1.1.
 */
static void send_response(chttpx_request_t* req, chttpx_response_t res)
{
    size_t capacity = 1024;
    for (size_t i = 0; i < res.headers_count; i++)
        capacity += strlen(res.headers[i].name) + strlen(res.headers[i].value) + 4;
    if (serv && serv->cors.enabled)
        capacity += strlen(serv->cors.methods) + strlen(serv->cors.headers) + MAX_HEADER_VALUE + 512;
    char* buffer = malloc(capacity);
    if (!buffer)
        return;
    size_t length = 0;

    /* Cors */
    const char* allowed_origin = req ? allowed_origin_cors(cHTTPX_HeaderGet(req, "Origin")) : NULL;

    if (!append_response_header(buffer, capacity, &length,
                                "HTTP/1.1 %d %s\r\n"
                                "Content-Type: %s\r\n"
                                "Content-Length: %zu\r\n"
                                "Connection: close\r\n",
                                res.status, cHTTPX_StatusReason((uint16_t)res.status), res.content_type ? res.content_type : cHTTPX_CTYPE_OCTET,
                                res.body_size))
        goto done;

    /* Etag */
    const char* etag = generate_etag(res.body, res.body_size);
    if (etag)
    {
        append_response_header(buffer, capacity, &length, "Etag: %s\r\n", etag);
        free((void*)etag);
    }

    if (allowed_origin)
    {
        if (!append_response_header(buffer, capacity, &length,
                                    "Access-Control-Allow-Origin: %s\r\n"
                                    "Access-Control-Allow-Methods: %s\r\n"
                                    "Access-Control-Allow-Headers: %s\r\n"
                                    "Access-Control-Allow-Credentials: true\r\n",
                                    allowed_origin, serv->cors.methods, serv->cors.headers))
            goto done;
    }

    if (req && req->request_id[0])
        if (!append_response_header(buffer, capacity, &length, "X-Request-ID: %s\r\n", req->request_id))
            goto done;

    /* Add all request headers */
    for (size_t i = 0; i < res.headers_count; i++)
    {
        if (!append_response_header(buffer, capacity, &length, "%s: %s\r\n", res.headers[i].name, res.headers[i].value))
            goto done;
    }

    if (!append_response_header(buffer, capacity, &length, "\r\n"))
        goto done;

    if (cHTTPX_SendAll(req->client_fd, buffer, length) != 0)
        goto done;

    if (res.body && res.body_size > 0)
        cHTTPX_SendAll(req->client_fd, res.body, res.body_size);

done:
    free(buffer);
}

/* Handle browser CORS preflight without hijacking ordinary OPTIONS routes. */
static int is_cors_preflight(chttpx_request_t* req)
{
    if (!req || !serv || !serv->cors.enabled || strcasecmp(req->method, cHTTPX_MethodOptions) != 0)
        return 0;

    if (!cHTTPX_HeaderGet(req, "Origin") || !cHTTPX_HeaderGet(req, "Access-Control-Request-Method"))
        return 0;

    chttpx_response_t res = {.status = cHTTPX_StatusNoContent, .content_type = cHTTPX_CTYPE_TEXT, .body = NULL, .body_size = 0};
    send_response(req, res);
    return 1;
}

static int valid_request_id(const char* value)
{
    if (!value || !*value || strlen(value) > 64)
        return 0;
    for (const unsigned char* p = (const unsigned char*)value; *p; p++)
    {
        if (!isalnum(*p) && *p != '-' && *p != '_' && *p != '.' && *p != ':')
            return 0;
    }
    return 1;
}

static void set_request_id(chttpx_request_t* req)
{
    if (!serv || !serv->request_id_enabled)
        return;
    const char* supplied = cHTTPX_HeaderGet(req, "X-Request-ID");
    if (valid_request_id(supplied))
    {
        snprintf(req->request_id, sizeof(req->request_id), "%s", supplied);
        return;
    }
    static unsigned long long counter = 0;
    unsigned long long sequence = __atomic_add_fetch(&counter, 1, __ATOMIC_SEQ_CST);
    struct timespec now = {0};
    clock_gettime(CLOCK_REALTIME, &now);
    snprintf(req->request_id, sizeof(req->request_id), "%08llx-%08llx-%08llx", (unsigned long long)now.tv_sec, (unsigned long long)now.tv_nsec,
             sequence);
}

static int language_allowed(const char* language)
{
    if (!serv || !language || !*language)
        return 0;
    if (serv->languages_count == 0)
        return serv->default_language && strcasecmp(language, serv->default_language) == 0;
    for (size_t i = 0; i < serv->languages_count; i++)
    {
        if (serv->languages[i] && strcasecmp(language, serv->languages[i]) == 0)
            return 1;
    }
    return 0;
}

static void set_request_language(chttpx_request_t* req)
{
    snprintf(req->language, sizeof(req->language), "%s", serv && serv->default_language ? serv->default_language : "en");
    const char* header = cHTTPX_HeaderGet(req, "Accept-Language");
    if (!header)
        return;

    double best_quality = -1.0;
    const char* cursor = header;
    while (*cursor)
    {
        while (*cursor == ' ' || *cursor == ',')
            cursor++;
        const char* end = strchr(cursor, ',');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        char item[128];
        if (length >= sizeof(item))
            length = sizeof(item) - 1;
        memcpy(item, cursor, length);
        item[length] = '\0';

        double quality = 1.0;
        char* semicolon = strchr(item, ';');
        if (semicolon)
        {
            *semicolon++ = '\0';
            while (*semicolon == ' ')
                semicolon++;
            if (strncmp(semicolon, "q=", 2) == 0)
                quality = strtod(semicolon + 2, NULL);
        }
        char* tail = item + strlen(item);
        while (tail > item && isspace((unsigned char)tail[-1]))
            *--tail = '\0';
        char* dash = strchr(item, '-');
        if (dash)
            *dash = '\0';
        if (quality > best_quality && language_allowed(item))
        {
            snprintf(req->language, sizeof(req->language), "%s", item);
            best_quality = quality;
        }
        if (!end)
            break;
        cursor = end + 1;
    }
}

static chttpx_request_t* parse_req_buffer(chttpx_socket_t client_fd, char* buffer, size_t received)
{
    chttpx_request_t* req = calloc(1, sizeof(chttpx_request_t));
    if (!req)
    {
        perror("calloc failed");
        return NULL;
    }

    if (received >= BUFFER_SIZE)
        received = BUFFER_SIZE - 1;

    buffer[received] = '\0';

    char method[16], path[CHTTPX_MAX_PATH], protocol[16];

    if (sscanf(buffer, "%15s %4095s %15s", method, path, protocol) != 3)
    {
        free(req);
        return NULL;
    }

    req->method = strdup(method);
    req->path = strdup(path);
    if (!req->method || !req->path)
    {
        free(req->method);
        free(req->path);
        free(req);
        return NULL;
    }
    req->client_fd = client_fd;

    /* Client IP */
    const char* client_ip = cHTTPX_ClientInetIP(client_fd);
    if (client_ip)
    {
        snprintf(req->client_ip, sizeof(req->client_ip), "%s", client_ip);
    }

    /* Parse headers */
    _parse_req_headers(req, buffer, received);
    if (req->_parse_status)
        return req;

    /* Parse cookies */
    _parse_req_cookies(req);
    if (req->_parse_status)
        return req;

    /* Content-Type */
    const char* content_type = cHTTPX_HeaderGet(req, "Content-Type");
    if (content_type)
        snprintf(req->content_type, sizeof(req->content_type), "%s", content_type ? content_type : cHTTPX_CTYPE_JSON);

    /* User-Agent */
    const char* user_agent = cHTTPX_HeaderGet(req, "User-Agent");
    if (user_agent)
        snprintf(req->user_agent, sizeof(req->user_agent), "%s", user_agent);

    /* Protocol */
    snprintf(req->protocol, sizeof(req->protocol), "%s", protocol);

    set_request_id(req);
    set_request_language(req);

    /* Parse query request */
    char* query = strchr(req->path, '?');
    if (query)
    {
        *query = '\0';
        _parse_req_query(req, query + 1);
        if (req->_parse_status)
            return req;
    }

    /* Parse body request */
    _parse_req_body(req, client_fd, buffer, received);
    if (req->_parse_status)
        return req;

    /* Parse media request */
    _parse_media(req, buffer, received);

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
    chttpx_socket_t client_sock = *(chttpx_socket_t*)arg;
    free(arg);

    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return NULL;
    }

    /* Timeouts */
    set_client_timeout(client_sock);

    char buf[BUFFER_SIZE];
    ssize_t received = read_req(client_sock, buf, BUFFER_SIZE);
    if (received == -2)
    {
        static const char too_large[] = "HTTP/1.1 431 Request Header Fields Too Large\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        cHTTPX_SendAll(client_sock, too_large, sizeof(too_large) - 1);
        chttpx_close(client_sock);
        return NULL;
    }
    if (received <= 0)
    {
        chttpx_close(client_sock);
        return NULL;
    }

    /* REQUEST */
    chttpx_request_t* req = parse_req_buffer(client_sock, buf, received);
    if (!req)
    {
        chttpx_close(client_sock);
        return NULL;
    }

    if (req->_parse_status)
    {
        const char* parse_message = req->_parse_status == cHTTPX_StatusPayloadTooLarge
                                        ? "payload too large"
                                        : (req->_parse_status == cHTTPX_StatusInternalServerError ? "internal server error" : "invalid request");
        chttpx_response_t parse_error = cHTTPX_ResError((uint16_t)req->_parse_status, parse_message);
        send_response(req, parse_error);
        cHTTPX_ResponseCleanup(&parse_error);
        goto cleanup_request;
    }

    /* Automatic handling is limited to actual CORS preflight requests. */
    if (is_cors_preflight(req))
        goto cleanup_request;

    chttpx_route_t* r = find_route(req);
    chttpx_response_t res = {0};

    /* Start time for logging */
    clock_gettime(CLOCK_MONOTONIC, &res.start_ts);

    if (r)
    {
        /* Use middlewares */
        for (size_t i = 0; i < serv->middleware.middleware_count; i++)
        {
            if (!serv->middleware.middlewares[i](req, &res))
                goto after_middlewares;
        }

        if (r->has_upload_policy && req->files_count > 0)
        {
            for (size_t file_index = 0; file_index < req->files_count; file_index++)
            {
                const chttpx_file_t* file = &req->files[file_index];
                if (r->upload_policy.max_size && file->size > r->upload_policy.max_size)
                {
                    res = cHTTPX_ResError(cHTTPX_StatusPayloadTooLarge, "upload is too large");
                    goto after_middlewares;
                }
                if (r->upload_policy.allowed_types_count > 0)
                {
                    bool allowed = false;
                    for (size_t type_index = 0; type_index < r->upload_policy.allowed_types_count; type_index++)
                    {
                        if (cHTTPX_MimeMatch(file->content_type, r->upload_policy.allowed_types[type_index]))
                        {
                            allowed = true;
                            break;
                        }
                    }
                    if (!allowed)
                    {
                        res = cHTTPX_ResError(cHTTPX_StatusUnsupportedMediaType, "unsupported upload media type");
                        goto after_middlewares;
                    }
                }
            }
        }

        for (size_t i = 0; i < r->middleware_count; i++)
        {
            if (!r->middlewares[i](req, &res))
                goto after_middlewares;
        }

        /* Handler */
        r->handler(req, &res);

    after_middlewares:
        for (size_t i = r->after_middleware_count; i > 0; i--)
            r->after_middlewares[i - 1](req, &res);
    }

    else
    {
        res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"not found\"}");
    }

    for (size_t i = serv->middleware.after_middleware_count; i > 0; i--)
        serv->middleware.after_middlewares[i - 1](req, &res);

    if (res.status < 100 || res.status > 599)
    {
        cHTTPX_ResponseCleanup(&res);
        res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "handler did not produce a valid response");
    }

    /* End time for logging */
    clock_gettime(CLOCK_MONOTONIC, &res.end_ts);

    send_response(req, res);

    /* Logging response */
    postmiddleware_logging_write(req, &res);

    cHTTPX_ResponseCleanup(&res);

cleanup_request:
    cHTTPX_RequestCleanup(req);

    /* Free REQuest cookie */
    chttpx_free_req_cookie(req);

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

    chttpx_close(client_sock);
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

    snprintf(buffer, 64, "\"%llx\"", (unsigned long long)hash);
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
static chttpx_response_t response_format(uint16_t status, const char* content_type, const char* fallback, const char* fmt, va_list args)
{
    if (!fmt)
        return (chttpx_response_t){.status = status, .content_type = content_type};
    va_list measured;
    va_copy(measured, args);
    int required = vsnprintf(NULL, 0, fmt, measured);
    va_end(measured);
    if (required < 0)
        return (chttpx_response_t){.status = cHTTPX_StatusInternalServerError,
                                   .content_type = content_type,
                                   .body = (const unsigned char*)fallback,
                                   .body_size = strlen(fallback)};
    size_t len = (size_t)required;
    unsigned char* body = malloc(len + 1);
    if (!body)
    {
        return (chttpx_response_t){.status = cHTTPX_StatusInternalServerError,
                                   .content_type = content_type,
                                   .body = (const unsigned char*)fallback,
                                   .body_size = strlen(fallback)};
    }
    vsnprintf((char*)body, len + 1, fmt, args);
    return (chttpx_response_t){.status = status, .content_type = content_type, .body = body, .body_size = len, .body_ownership = CHTTPX_BODY_OWNED};
}

chttpx_response_t cHTTPX_ResJson(uint16_t status, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    chttpx_response_t response = response_format(status, cHTTPX_CTYPE_JSON, "{\"error\":\"internal server error\"}", fmt, args);
    va_end(args);
    return response;
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
    va_list args;
    va_start(args, fmt);
    chttpx_response_t response = response_format(status, cHTTPX_CTYPE_HTML, "<h1>Internal Server Error</h1>", fmt, args);
    va_end(args);
    return response;
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
chttpx_response_t cHTTPX_ResBinary(uint16_t status, const char* content_type, const unsigned char* body, size_t body_size)
{
    if (body_size == 0)
        return (chttpx_response_t){.status = status, .content_type = content_type, .body = NULL, .body_size = 0};
    if (!body)
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, "invalid response body");
    unsigned char* buffer = malloc(body_size);
    if (!buffer)
    {
        perror("malloc failed");
        return cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"internal server error\"}");
    }

    memcpy(buffer, body, body_size);

    return (chttpx_response_t){.status = status,
                               .content_type = content_type,
                               .body = buffer,
                               .body_size = body_size,
                               .body_ownership = CHTTPX_BODY_OWNED,
                               .start_ts = {0},
                               .end_ts = {0}};
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

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, "failed to read file");
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, "failed to read file");
    }

    if (size == 0)
    {
        fclose(f);
        return cHTTPX_ResBinary(status, content_type, NULL, 0);
    }

    unsigned char* data = malloc(size);
    if (!data)
    {
        fclose(f);
        return cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"internal server error\"}");
    }

    if (fread(data, 1, (size_t)size, f) != (size_t)size)
    {
        free(data);
        fclose(f);
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, "failed to read file");
    }
    fclose(f);

    return (chttpx_response_t){.status = status,
                               .content_type = content_type,
                               .body = data,
                               .body_size = size,
                               .body_ownership = CHTTPX_BODY_OWNED,
                               .start_ts = {0},
                               .end_ts = {0}};
}

void cHTTPX_ResponseCleanup(chttpx_response_t* res)
{
    if (!res)
        return;
    if (res->body_ownership == CHTTPX_BODY_OWNED)
        free((void*)res->body);
    res->body = NULL;
    res->body_size = 0;
    res->body_ownership = CHTTPX_BODY_BORROWED;
}

chttpx_response_t cHTTPX_ResNoContent(void)
{
    return (chttpx_response_t){.status = cHTTPX_StatusNoContent, .content_type = cHTTPX_CTYPE_TEXT};
}
