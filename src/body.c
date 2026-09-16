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

#include "body.h"

#include "utils.h"
#include "headers.h"
#include "crosspltm.h"
#include "serv.h"
#include "http.h"

#include <stdio.h>
#include <errno.h>

static int parse_content_length(chttpx_request_t* req, size_t* content_length)
{
    const char* value = cHTTPX_HeaderGet(req, "Content-Length");
    if (!value)
    {
        *content_length = 0;
        return 1;
    }
    if (!*value || *value == '-')
        return 0;
    errno = 0;
    char* end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno == ERANGE || !end || *end || parsed > SIZE_MAX)
        return 0;
    *content_length = (size_t)parsed;
    return 1;
}

static int append_bytes(unsigned char** data, size_t* size, size_t* capacity, const unsigned char* bytes, size_t count, size_t limit)
{
    if (count > limit - *size)
        return 0;
    if (*size + count + 1 > *capacity)
    {
        size_t new_capacity = *capacity ? *capacity : 4096;
        while (new_capacity < *size + count + 1)
            new_capacity *= 2;
        if (new_capacity > limit + 1)
            new_capacity = limit + 1;
        unsigned char* resized = realloc(*data, new_capacity);
        if (!resized)
            return 0;
        *data = resized;
        *capacity = new_capacity;
    }
    memcpy(*data + *size, bytes, count);
    *size += count;
    (*data)[*size] = '\0';
    return 1;
}

static int decode_chunked(chttpx_request_t* req, chttpx_socket_t client_fd, const unsigned char* initial, size_t initial_size, size_t limit)
{
    unsigned char* wire = NULL;
    size_t wire_size = 0;
    size_t wire_capacity = 0;
    unsigned char* decoded = NULL;
    size_t decoded_size = 0;
    size_t decoded_capacity = 0;
    size_t cursor = 0;

    if (!append_bytes(&wire, &wire_size, &wire_capacity, initial, initial_size, limit + BUFFER_SIZE))
        goto fail;

    for (;;)
    {
        unsigned char* line_end = chttpx_memmem(wire + cursor, wire_size - cursor, "\r\n", 2);
        while (!line_end)
        {
            unsigned char incoming[BUFFER_SIZE];
            int received = recv(client_fd, (char*)incoming, sizeof(incoming), 0);
            if (received <= 0 || !append_bytes(&wire, &wire_size, &wire_capacity, incoming, (size_t)received, limit + BUFFER_SIZE))
                goto fail;
            line_end = chttpx_memmem(wire + cursor, wire_size - cursor, "\r\n", 2);
        }

        size_t line_size = (size_t)(line_end - (wire + cursor));
        if (line_size == 0 || line_size >= 32)
            goto fail;
        char line[32];
        memcpy(line, wire + cursor, line_size);
        line[line_size] = '\0';
        char* extension = strchr(line, ';');
        if (extension)
            *extension = '\0';
        errno = 0;
        char* end = NULL;
        unsigned long long chunk_size = strtoull(line, &end, 16);
        if (errno == ERANGE || !end || *end || chunk_size > SIZE_MAX)
            goto fail;
        cursor = (size_t)(line_end - wire) + 2;
        if (chunk_size == 0)
            break;
        if (chunk_size > limit - decoded_size)
        {
            req->_parse_status = cHTTPX_StatusPayloadTooLarge;
            goto fail;
        }

        while (wire_size - cursor < (size_t)chunk_size + 2)
        {
            unsigned char incoming[BUFFER_SIZE];
            int received = recv(client_fd, (char*)incoming, sizeof(incoming), 0);
            if (received <= 0 || !append_bytes(&wire, &wire_size, &wire_capacity, incoming, (size_t)received, limit + BUFFER_SIZE))
                goto fail;
        }
        if (wire[cursor + chunk_size] != '\r' || wire[cursor + chunk_size + 1] != '\n' ||
            !append_bytes(&decoded, &decoded_size, &decoded_capacity, wire + cursor, (size_t)chunk_size, limit))
            goto fail;
        cursor += (size_t)chunk_size + 2;
    }

    free(wire);
    req->body = decoded;
    req->body_size = decoded_size;
    req->content_length = decoded_size;
    return 1;

fail:
    free(wire);
    free(decoded);
    if (!req->_parse_status)
        req->_parse_status = cHTTPX_StatusBadRequest;
    return 0;
}

/* Parse body in request */
void _parse_req_body(chttpx_request_t* req, chttpx_socket_t client_fd, char* buffer, size_t buffer_len)
{
    req->client_fd = client_fd;

    size_t content_length;
    if (!parse_content_length(req, &content_length))
    {
        req->_parse_status = cHTTPX_StatusBadRequest;
        return;
    }
    req->content_length = content_length;

    int memory_body = strstr(req->content_type, cHTTPX_CTYPE_JSON) || strstr(req->content_type, "text/") ||
                      strstr(req->content_type, cHTTPX_CTYPE_FORM) || strstr(req->content_type, cHTTPX_CTYPE_MULTI);

    const char* body_start = chttpx_memmem(buffer, buffer_len, "\r\n\r\n", 4);
    if (!body_start)
    {
        req->body = NULL;
        req->body_size = 0;
        return;
    }

    body_start += 4;
    size_t body_in_buffer = buffer_len - (body_start - buffer);

    const char* transfer_encoding = cHTTPX_HeaderGet(req, "Transfer-Encoding");
    if (transfer_encoding && strcasecmp(transfer_encoding, "chunked") == 0)
    {
        if (cHTTPX_HeaderGet(req, "Content-Length"))
        {
            req->_parse_status = cHTTPX_StatusBadRequest;
            return;
        }
        size_t limit = memory_body && !strstr(req->content_type, cHTTPX_CTYPE_MULTI) ? serv->max_body_size : serv->max_upload_size;
        decode_chunked(req, client_fd, (const unsigned char*)body_start, body_in_buffer, limit);
        return;
    }

    if (req->content_length == 0)
    {
        req->body = NULL;
        req->body_size = 0;
        return;
    }

    size_t limit = memory_body && !strstr(req->content_type, cHTTPX_CTYPE_MULTI) ? serv->max_body_size : serv->max_upload_size;
    if (req->content_length > limit)
    {
        req->_parse_status = cHTTPX_StatusPayloadTooLarge;
        return;
    }

    if (!memory_body)
    {
        req->body = NULL;
        req->body_size = 0;
        return;
    }

    req->body = malloc(req->content_length + 1);
    if (!req->body)
    {
        perror("malloc failed");
        req->body_size = 0;
        return;
    }

    if (body_in_buffer > req->content_length)
        body_in_buffer = req->content_length;
    memcpy(req->body, body_start, body_in_buffer);

    size_t remaining = req->content_length - body_in_buffer;
    size_t total_read = body_in_buffer;

    while (remaining > 0)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(client_fd, &fds);

        struct timeval tv;
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        int r = select(client_fd + 1, &fds, NULL, NULL, &tv);
        if (r <= 0)
            break;

        ssize_t n = recv(client_fd, (char*)req->body + total_read, remaining, 0);
        if (n <= 0)
            break;

        total_read += n;
        remaining -= n;
    }

    req->body_size = total_read;
    ((char*)req->body)[req->body_size] = '\0';
}
