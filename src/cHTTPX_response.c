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

#include "cHTTPX_response.h"

#include "cHTTPX_inet.h"
#include "cHTTPX_body.h"
#include "cHTTPX_http.h"
#include "cHTTPX_serv.h"
#include "cHTTPX_media.h"
#include "cHTTPX_headers.h"
#include "cHTTPX_cookies.h"
#include "cHTTPX_queries.h"
#include "cHTTPX_crosspltm.h"
#include "cHTTPX_tls.h"
#include "cHTTPX_http2.h"
#include "cHTTPX_metrics.h"

#include <errno.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>

int cHTTPX_SendAll(chttpx_socket_t fd, const void* data, size_t size)
{
    return _chttpx_io_send_all(fd, NULL, data, size);
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

/**
 * Match route.
 *
 * @param template Parameter `template`.
 * @param path Parameter `path`.
 * @param params Parameter `params`.
 * @param param_count Parameter `param_count`.
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
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
    chttpx_serv_t* server = req ? req->_server : NULL;
    if (!server || !server->initialized)
        return NULL;

    for (size_t i = 0; i < server->routes_count; i++)
    {
        chttpx_route_t* registered = server->routes[i];
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

/**
 * Socket read timed out.
 *
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
static int socket_read_timed_out(void)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    int error = WSAGetLastError();
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT;
#endif
}

/**
 * Read req.
 *
 * @param server HTTP server instance.
 * @param fd Parameter `fd`.
 * @param tls_session Parameter `tls_session`.
 * @param buffer Parameter `buffer`.
 * @param buffer_size Parameter `buffer_size`.
 * @return Bytes read or a negative error code.
 */
static ssize_t read_req(chttpx_serv_t* server, chttpx_socket_t fd, void* tls_session, char* buffer, size_t buffer_size)
{
    size_t total = 0;
    size_t limit = server && server->max_header_size && server->max_header_size < buffer_size ? server->max_header_size : buffer_size - 1;

    while (1)
    {
        if (total >= buffer_size - 1)
            return -2;

        int n = _chttpx_io_recv(fd, tls_session, buffer + total, buffer_size - 1 - total);
        if (n == cHTTPX_ERR_TLS)
            return cHTTPX_ERR_TLS;
        if (n < 0)
        {
#ifdef CHTTPX_PLATFORM_POSIX
            if (errno == EINTR)
                continue;
#endif
            if (socket_read_timed_out())
                return cHTTPX_ERR_TIMEOUT;
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

/**
 * Set client timeout.
 *
 * @param server HTTP server instance.
 * @param client_fd Parameter `client_fd`.
 */
static void set_client_timeout(chttpx_serv_t* server, chttpx_socket_t client_fd)
{
    if (!server)
        return;

#ifdef CHTTPX_PLATFORM_WINDOWS
    DWORD read_timeout_ms = (DWORD)server->read_timeout_sec * 1000U;
    DWORD write_timeout_ms = (DWORD)server->write_timeout_sec * 1000U;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&read_timeout_ms, sizeof(read_timeout_ms));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&write_timeout_ms, sizeof(write_timeout_ms));
#else
    struct timeval tv;
    tv.tv_usec = 0;

    tv.tv_sec = server->read_timeout_sec;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    tv.tv_sec = server->write_timeout_sec;
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

/**
 * Find an allowed CORS origin in the sorted server origin table.
 *
 * @param server HTTP server containing the immutable CORS configuration.
 * @param req_origin Origin header value supplied by the client.
 * @return Borrowed configured origin string or NULL when it is not allowed.
 */
static const char* allowed_origin_cors(chttpx_serv_t* server, const char* req_origin)
{
    if (!server || !server->cors.enabled || !req_origin)
        return NULL;

    size_t left = 0;
    size_t right = server->cors.origins_count;
    while (left < right)
    {
        size_t middle = left + (right - left) / 2;
        int order = strcmp(server->cors.origins[middle], req_origin);
        if (order == 0)
            return server->cors.origins[middle];
        if (order < 0)
            left = middle + 1;
        else
            right = middle;
    }

    return NULL;
}

/* Etag for response cache */
/**
 * Generate etag.
 *
 * @param body Parameter `body`.
 * @param body_size Parameter `body_size`.
 * @return Pointer or NULL on failure.
 */
static const char* generate_etag(const unsigned char* body, size_t body_size);

/**
 * Append response header.
 *
 * @param buffer Parameter `buffer`.
 * @param capacity Parameter `capacity`.
 * @param length Parameter `length`.
 * @param format Parameter `format`.
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
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
 * This function serializes the application response for the HTTP/2 transport.
 */
static int build_response_buffer(chttpx_request_t* req, chttpx_response_t res, char** output, size_t* output_size)
{
    if (!req || !output || !output_size)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    *output = NULL;
    *output_size = 0;
    chttpx_serv_t* server = req->_server;
    size_t capacity = 1024;
    for (size_t i = 0; i < res.headers_count; i++)
    {
        size_t name_size = strlen(res.headers[i].name);
        size_t value_size = strlen(res.headers[i].value);
        if (capacity > SIZE_MAX - name_size - value_size - 4)
            return cHTTPX_ERR_LIMIT;
        capacity += name_size + value_size + 4;
    }
    if (server && server->cors.enabled)
    {
        size_t methods_size = server->cors.methods ? strlen(server->cors.methods) : 0;
        size_t headers_size = server->cors.headers ? strlen(server->cors.headers) : 0;
        if (capacity > SIZE_MAX - methods_size - headers_size - MAX_HEADER_VALUE - 512)
            return cHTTPX_ERR_LIMIT;
        capacity += methods_size + headers_size + MAX_HEADER_VALUE + 512;
    }

    char* header = malloc(capacity);
    if (!header)
        return cHTTPX_ERR_MEMORY;
    size_t length = 0;
    const char* allowed_origin = server && server->cors.enabled ? allowed_origin_cors(server, cHTTPX_HeaderGet(req, "Origin")) : NULL;

    if (!append_response_header(header, capacity, &length, "HTTP/2 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n", res.status, cHTTPX_StatusReason((uint16_t)res.status), res.content_type ? res.content_type : cHTTPX_CTYPE_OCTET, res.body_size))
        goto limit_error;

    const char* etag = generate_etag(res.body, res.body_size);
    if (etag)
    {
        if (!append_response_header(header, capacity, &length, "Etag: %s\r\n", etag))
        {
            free((void*)etag);
            goto limit_error;
        }
        free((void*)etag);
    }

    if (allowed_origin)
    {
        if (!append_response_header(header, capacity, &length, "Access-Control-Allow-Origin: %s\r\nAccess-Control-Allow-Methods: %s\r\nAccess-Control-Allow-Headers: %s\r\nAccess-Control-Allow-Credentials: true\r\n", allowed_origin, server->cors.methods, server->cors.headers))
            goto limit_error;
    }

    if (req->request_id[0] && !append_response_header(header, capacity, &length, "X-Request-ID: %s\r\n", req->request_id))
        goto limit_error;

    for (size_t i = 0; i < res.headers_count; i++)
        if (!append_response_header(header, capacity, &length, "%s: %s\r\n", res.headers[i].name, res.headers[i].value))
            goto limit_error;

    if (!append_response_header(header, capacity, &length, "\r\n"))
        goto limit_error;
    if (res.body_size > SIZE_MAX - length)
        goto limit_error;

    size_t total = length + res.body_size;
    char* response = malloc(total ? total : 1);
    if (!response)
    {
        free(header);
        return cHTTPX_ERR_MEMORY;
    }
    memcpy(response, header, length);
    if (res.body && res.body_size)
        memcpy(response + length, res.body, res.body_size);
    free(header);
    *output = response;
    *output_size = total;
    return cHTTPX_OK;

limit_error:
    free(header);
    return cHTTPX_ERR_LIMIT;
}

/**
 * Send response.
 *
 * @param req Current HTTP request.
 * @param response HTTP response.
 */
static void send_response(chttpx_request_t* req, chttpx_response_t res)
{
    char* response = NULL;
    size_t response_size = 0;
    int build_result = build_response_buffer(req, res, &response, &response_size);
    if (build_result != cHTTPX_OK)
        return;
    int write_result = _chttpx_io_send_all(req->client_fd, req->_tls_session, response, response_size);
    if (write_result == cHTTPX_ERR_TLS)
        _chttpx_tls_log_error(req->_server, req->request_id, "TLS response write failed");
    free(response);
}

/* Handle browser CORS preflight without hijacking ordinary OPTIONS routes. */
/**
 * Build cors preflight.
 *
 * @param req Current HTTP request.
 * @param response HTTP response.
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
static int build_cors_preflight(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    if (!req || !res || !server || !server->cors.enabled || strcasecmp(req->method, cHTTPX_MethodOptions) != 0)
        return 0;
    if (!cHTTPX_HeaderGet(req, "Origin") || !cHTTPX_HeaderGet(req, "Access-Control-Request-Method"))
        return 0;
    *res = (chttpx_response_t){.status = cHTTPX_StatusNoContent, .content_type = cHTTPX_CTYPE_TEXT, .body = NULL, .body_size = 0};
    return 1;
}

/**
 * Is cors preflight.
 *
 * @param req Current HTTP request.
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
static int is_cors_preflight(chttpx_request_t* req)
{
    chttpx_response_t res = {0};
    if (!build_cors_preflight(req, &res))
        return 0;
    send_response(req, res);
    cHTTPX_ResponseCleanup(&res);
    return 1;
}

/**
 * Valid request id.
 *
 * @param value Parameter `value`.
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
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

/**
 * Set request id.
 *
 * @param req Current HTTP request.
 */
static void set_request_id(chttpx_request_t* req)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    if (!server || !server->request_id_enabled)
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

/**
 * Language allowed.
 *
 * @param server HTTP server instance.
 * @param language Parameter `language`.
 * @return Non-zero on success, 0 on failure, or a negative error code.
 */
static int language_allowed(chttpx_serv_t* server, const char* language)
{
    if (!server || !language || !*language)
        return 0;
    if (server->languages_count == 0)
        return server->default_language && strcasecmp(language, server->default_language) == 0;
    for (size_t i = 0; i < server->languages_count; i++)
    {
        if (server->languages[i] && strcasecmp(language, server->languages[i]) == 0)
            return 1;
    }
    return 0;
}

/**
 * Set request language.
 *
 * @param req Current HTTP request.
 */
static void set_request_language(chttpx_request_t* req)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    snprintf(req->language, sizeof(req->language), "%s", server && server->default_language ? server->default_language : "en");
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
        if (quality > best_quality && language_allowed(server, item))
        {
            snprintf(req->language, sizeof(req->language), "%s", item);
            best_quality = quality;
        }
        if (!end)
            break;
        cursor = end + 1;
    }
}

/**
 * Parse req buffer.
 *
 * @param server HTTP server instance.
 * @param client_fd Parameter `client_fd`.
 * @param tls_session Parameter `tls_session`.
 * @param buffer Parameter `buffer`.
 * @param received Parameter `received`.
 * @return Pointer or NULL on failure.
 */
static chttpx_request_t* parse_req_buffer(chttpx_serv_t* server, chttpx_socket_t client_fd, void* tls_session, char* buffer, size_t received)
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
    req->_server = server;
    req->_tls_session = tls_session;

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
 * Close prefetched stream.
 *
 * @param resource Parameter `resource`.
 */
static void close_prefetched_stream(void* resource)
{
    if (resource)
        fclose((FILE*)resource);
}

/**
 * Free request object.
 *
 * @param req Current HTTP request.
 */
static void free_request_object(chttpx_request_t* req)
{
    if (!req)
        return;
    cHTTPX_RequestCleanup(req);
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
}

/**
 * Parse prefetched request.
 *
 * @param server HTTP server instance.
 * @param client_fd Parameter `client_fd`.
 * @param tls_session Parameter `tls_session`.
 * @param headers Parameter `headers`.
 * @param header_size Parameter `header_size`.
 * @param body Parameter `body`.
 * @param body_size Parameter `body_size`.
 * @param body_stream Parameter `body_stream`.
 * @param content_length Parameter `content_length`.
 * @return Open temporary file or NULL on failure.
 */
static chttpx_request_t* parse_prefetched_request(chttpx_serv_t* server, chttpx_socket_t client_fd, void* tls_session, char* headers, size_t header_size, unsigned char* body, size_t body_size, FILE* body_stream, size_t content_length)
{
    chttpx_request_t* req = calloc(1, sizeof(*req));
    if (!req)
    {
        free(body);
        if (body_stream)
            fclose(body_stream);
        return NULL;
    }

    req->body = body;
    req->body_size = body_size;
    req->content_length = content_length;
    req->client_fd = client_fd;
    req->_server = server;
    req->_tls_session = tls_session;

    char method[16];
    char path[CHTTPX_MAX_PATH];
    char protocol[16];
    if (!headers || sscanf(headers, "%15s %4095s %15s", method, path, protocol) != 3)
    {
        req->_parse_status = cHTTPX_StatusBadRequest;
        return req;
    }

    req->method = strdup(method);
    req->path = strdup(path);
    if (!req->method || !req->path)
    {
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return req;
    }

    const char* client_ip = cHTTPX_ClientInetIP(client_fd);
    if (client_ip)
        snprintf(req->client_ip, sizeof(req->client_ip), "%s", client_ip);

    _parse_req_headers(req, headers, header_size);
    if (req->_parse_status)
        return req;

    _parse_req_cookies(req);
    if (req->_parse_status)
        return req;

    const char* content_type = cHTTPX_HeaderGet(req, "Content-Type");
    if (content_type)
        snprintf(req->content_type, sizeof(req->content_type), "%s", content_type);

    const char* user_agent = cHTTPX_HeaderGet(req, "User-Agent");
    if (user_agent)
        snprintf(req->user_agent, sizeof(req->user_agent), "%s", user_agent);

    snprintf(req->protocol, sizeof(req->protocol), "%s", protocol);
    set_request_id(req);
    set_request_language(req);

    char* query = strchr(req->path, '?');
    if (query)
    {
        *query = '\0';
        _parse_req_query(req, query + 1);
        if (req->_parse_status)
            return req;
    }

    if (body_stream)
    {
        if (cHTTPX_Defer(req, body_stream, close_prefetched_stream) != 0)
        {
            fclose(body_stream);
            req->_parse_status = cHTTPX_StatusInternalServerError;
            return req;
        }
        req->_multipart_stream = body_stream;
    }

    _parse_media(req, headers, header_size);
    return req;
}

int _chttpx_dispatch(chttpx_serv_t* server, chttpx_request_t* req, chttpx_response_t* res)
{
    if (!server || !server->initialized || !req || !res || !req->method || !req->path)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    req->_server = server;

    struct timespec request_start;
    clock_gettime(CLOCK_MONOTONIC, &request_start);
    _chttpx_metrics_request_begin(server, req->body_size);

    chttpx_route_t* route = find_route(req);
    memset(res, 0, sizeof(*res));
    res->start_ts = request_start;

    if (route)
    {
        for (size_t i = 0; i < server->middleware.middleware_count; i++)
        {
            if (!server->middleware.middlewares[i](req, res))
                goto after_route_middlewares;
        }

        if (route->has_upload_policy && req->files_count > 0)
        {
            for (size_t file_index = 0; file_index < req->files_count; file_index++)
            {
                const chttpx_file_t* file = &req->files[file_index];
                if (route->upload_policy.max_size && file->size > route->upload_policy.max_size)
                {
                    *res = cHTTPX_ResError(cHTTPX_StatusPayloadTooLarge, "upload is too large");
                    goto after_route_middlewares;
                }

                if (route->upload_policy.allowed_types_count > 0)
                {
                    bool allowed = false;
                    for (size_t type_index = 0; type_index < route->upload_policy.allowed_types_count; type_index++)
                    {
                        if (cHTTPX_MimeMatch(file->content_type, route->upload_policy.allowed_types[type_index]))
                        {
                            allowed = true;
                            break;
                        }
                    }

                    if (!allowed)
                    {
                        *res = cHTTPX_ResError(cHTTPX_StatusUnsupportedMediaType, "unsupported upload media type");
                        goto after_route_middlewares;
                    }
                }
            }
        }

        for (size_t i = 0; i < route->middleware_count; i++)
        {
            if (!route->middlewares[i](req, res))
                goto after_route_middlewares;
        }

        route->handler(req, res);

    after_route_middlewares:
        for (size_t i = route->after_middleware_count; i > 0; i--)
            route->after_middlewares[i - 1](req, res);

        if (route->compression_disabled)
            res->compression_disabled = true;
    }
    else
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\": \"not found\"}");
    }

    for (size_t i = server->middleware.after_middleware_count; i > 0; i--)
        server->middleware.after_middlewares[i - 1](req, res);

    if (res->status < 100 || res->status > 599)
    {
        cHTTPX_ResponseCleanup(res);
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "handler did not produce a valid response");
    }

    res->start_ts = request_start;
    clock_gettime(CLOCK_MONOTONIC, &res->end_ts);

    double duration_seconds =
        (double)(res->end_ts.tv_sec - request_start.tv_sec) +
        (double)(res->end_ts.tv_nsec - request_start.tv_nsec) / 1000000000.0;

    _chttpx_metrics_request_end(server,
                                route ? route->method : req->method,
                                route ? route->path : NULL,
                                res->status,
                                res->body_size,
                                duration_seconds);

    postmiddleware_logging_write(req, res);
    return cHTTPX_OK;
}

int _chttpx_execute_prefetched(chttpx_serv_t* server, chttpx_socket_t client_fd, void* tls_session, char* headers, size_t header_size, unsigned char* body, size_t body_size, FILE* body_stream, size_t content_length, const chttpx_stream_transport_t* stream_transport, char** output, size_t* output_size)
{
    if (!server || !headers || !output || !output_size)
    {
        free(body);
        if (body_stream)
            fclose(body_stream);
        return cHTTPX_ERR_INVALID_ARGUMENT;
    }

    *output = NULL;
    *output_size = 0;
    chttpx_request_t* req = parse_prefetched_request(server, client_fd, tls_session, headers, header_size, body, body_size, body_stream, content_length);
    if (!req)
        return cHTTPX_ERR_MEMORY;
    if (stream_transport)
        req->_stream_transport = *stream_transport;

    chttpx_response_t res = {0};
    if (req->_parse_status)
    {
        _chttpx_metrics_parser_failure(server);
        const char* message = req->_parse_status == cHTTPX_StatusPayloadTooLarge ? "payload too large" : (req->_parse_status == cHTTPX_StatusInternalServerError ? "internal server error" : "invalid request");
        res = cHTTPX_ResError((uint16_t)req->_parse_status, message);
    }
    else if (!build_cors_preflight(req, &res))
    {
        int dispatch_result = _chttpx_dispatch(server, req, &res);
        if (dispatch_result != cHTTPX_OK)
            res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "request dispatch failed");
    }

    if (res._streaming_response)
    {
        if (req->_stream_transport.close)
            req->_stream_transport.close(req->_stream_transport.context);
        *output = NULL;
        *output_size = SIZE_MAX;
        cHTTPX_ResponseCleanup(&res);
        free_request_object(req);
        return cHTTPX_OK;
    }

    int result = build_response_buffer(req, res, output, output_size);
    cHTTPX_ResponseCleanup(&res);
    free_request_object(req);
    return result;
}

/**
 * Handle one accepted socket. The argument is a chttpx_client_ctx_t carrying
 * both the socket and the exact App-managed server that accepted it.
 */
void* chttpx_handle(void* arg)
{
    chttpx_client_ctx_t* context = arg;
    if (!context)
        return NULL;

    chttpx_serv_t* server = context->server;
    chttpx_socket_t client_sock = context->client_fd;
    free(context);

    if (!server || !server->initialized)
    {
        chttpx_close(client_sock);
        return NULL;
    }

    set_client_timeout(server, client_sock);

    void* tls_session = NULL;
    if (_chttpx_tls_accept(server, client_sock, &tls_session) != cHTTPX_OK)
    {
        _chttpx_metrics_connection_rejected(server);
        chttpx_close(client_sock);
        return NULL;
    }

    _chttpx_http2_serve(server, client_sock, tls_session);
    _chttpx_tls_session_close(tls_session);
    chttpx_close(client_sock);
    return NULL;
}

/**
 * Generate etag.
 *
 * @param body Parameter `body`.
 * @param body_size Parameter `body_size`.
 * @return Pointer or NULL on failure.
 */
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
/**
 * Response format.
 *
 * @param status Parameter `status`.
 * @param content_type Parameter `content_type`.
 * @param fallback Parameter `fallback`.
 * @param fmt Parameter `fmt`.
 * @param args Parameter `args`.
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
    return (chttpx_response_t){.status = status, .content_type = content_type, .body = body, .body_size = len, .body_ownership = cHTTPX_BODY_OWNED};
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
                               .body_ownership = cHTTPX_BODY_OWNED,
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
                               .body_ownership = cHTTPX_BODY_OWNED,
                               .start_ts = {0},
                               .end_ts = {0}};
}

void cHTTPX_ResponseCleanup(chttpx_response_t* res)
{
    if (!res)
        return;
    if (res->body_ownership == cHTTPX_BODY_OWNED)
        free((void*)res->body);
    res->body = NULL;
    res->body_size = 0;
    res->body_ownership = cHTTPX_BODY_BORROWED;
    res->_streaming_response = false;
}

chttpx_response_t cHTTPX_ResNoContent(void)
{
    return (chttpx_response_t){.status = cHTTPX_StatusNoContent, .content_type = cHTTPX_CTYPE_TEXT};
}
