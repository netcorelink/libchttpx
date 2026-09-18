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

#include "crosspltm.h"
#include "headers.h"
#include "http.h"
#include "serv.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>

static void close_stream(void* resource)
{
    if (resource)
        fclose((FILE*)resource);
}

static int is_multipart(const chttpx_request_t* req)
{
    return req && strstr(req->content_type, cHTTPX_CTYPE_MULTI) != NULL;
}

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
    if (*size > limit || count > limit - *size || *size == SIZE_MAX || count > SIZE_MAX - *size - 1)
        return 0;

    size_t required = *size + count + 1;
    if (required > *capacity)
    {
        size_t max_capacity = limit == SIZE_MAX ? SIZE_MAX : limit + 1;
        size_t new_capacity = *capacity ? *capacity : 4096;

        while (new_capacity < required)
        {
            if (new_capacity > max_capacity / 2)
            {
                new_capacity = max_capacity;
                break;
            }
            new_capacity *= 2;
        }

        if (new_capacity < required)
            return 0;

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

static int stream_write(FILE* stream, const unsigned char* data, size_t size, size_t* total, size_t limit)
{
    if (size > limit - *total || (size && fwrite(data, 1, size, stream) != size))
        return 0;
    *total += size;
    return 1;
}

static int receive_fixed_multipart(chttpx_request_t* req, chttpx_socket_t client_fd, const unsigned char* initial, size_t initial_size)
{
    FILE* stream = tmpfile();
    if (!stream || cHTTPX_Defer(req, stream, close_stream) != 0)
    {
        if (stream)
            fclose(stream);
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return 0;
    }

    size_t total = initial_size > req->content_length ? req->content_length : initial_size;
    if (total && fwrite(initial, 1, total, stream) != total)
    {
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return 0;
    }

    unsigned char buffer[BUFFER_SIZE];
    while (total < req->content_length)
    {
        size_t wanted = req->content_length - total;
        if (wanted > sizeof(buffer))
            wanted = sizeof(buffer);

        int received = recv(client_fd, (char*)buffer, wanted, 0);
        if (received <= 0)
        {
            req->_parse_status = cHTTPX_StatusBadRequest;
            return 0;
        }

        if (fwrite(buffer, 1, (size_t)received, stream) != (size_t)received)
        {
            req->_parse_status = cHTTPX_StatusInternalServerError;
            return 0;
        }
        total += (size_t)received;
    }

    if (fflush(stream) != 0 || fseek(stream, 0, SEEK_SET) != 0)
    {
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return 0;
    }

    req->_multipart_stream = stream;
    req->body = NULL;
    req->body_size = 0;
    return 1;
}

static int decode_chunked(chttpx_request_t* req, chttpx_socket_t client_fd, const unsigned char* initial, size_t initial_size, size_t limit,
                          int spool_to_disk)
{
    unsigned char* wire = NULL;
    size_t wire_size = 0;
    size_t wire_capacity = 0;
    unsigned char* decoded = NULL;
    size_t decoded_size = 0;
    size_t decoded_capacity = 0;
    size_t cursor = 0;
    FILE* stream = NULL;

    size_t wire_limit = limit > SIZE_MAX - BUFFER_SIZE ? SIZE_MAX : limit + BUFFER_SIZE;
    if (!append_bytes(&wire, &wire_size, &wire_capacity, initial, initial_size, wire_limit))
        goto fail;

    if (spool_to_disk)
    {
        stream = tmpfile();
        if (!stream || cHTTPX_Defer(req, stream, close_stream) != 0)
        {
            if (stream)
                fclose(stream);
            stream = NULL;
            req->_parse_status = cHTTPX_StatusInternalServerError;
            goto fail;
        }
    }

    for (;;)
    {
        unsigned char* line_end = chttpx_memmem(wire + cursor, wire_size - cursor, "\r\n", 2);
        while (!line_end)
        {
            unsigned char incoming[BUFFER_SIZE];
            int received = recv(client_fd, (char*)incoming, sizeof(incoming), 0);
            if (received <= 0 || !append_bytes(&wire, &wire_size, &wire_capacity, incoming, (size_t)received, wire_limit))
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
        unsigned long long parsed = strtoull(line, &end, 16);
        if (errno == ERANGE || !end || *end || parsed > SIZE_MAX)
            goto fail;
        size_t chunk_size = (size_t)parsed;
        cursor = (size_t)(line_end - wire) + 2;

        if (chunk_size == 0)
        {
            for (;;)
            {
                unsigned char* trailer_end = chttpx_memmem(wire + cursor, wire_size - cursor, "\r\n", 2);
                while (!trailer_end)
                {
                    unsigned char incoming[BUFFER_SIZE];
                    int received = recv(client_fd, (char*)incoming, sizeof(incoming), 0);
                    if (received <= 0 || !append_bytes(&wire, &wire_size, &wire_capacity, incoming, (size_t)received, wire_limit))
                        goto fail;
                    trailer_end = chttpx_memmem(wire + cursor, wire_size - cursor, "\r\n", 2);
                }
                if (trailer_end == wire + cursor)
                    break;
                cursor = (size_t)(trailer_end - wire) + 2;
            }
            break;
        }

        if (chunk_size > limit - decoded_size)
        {
            req->_parse_status = cHTTPX_StatusPayloadTooLarge;
            goto fail;
        }

        while (wire_size - cursor < chunk_size + 2)
        {
            unsigned char incoming[BUFFER_SIZE];
            int received = recv(client_fd, (char*)incoming, sizeof(incoming), 0);
            if (received <= 0 || !append_bytes(&wire, &wire_size, &wire_capacity, incoming, (size_t)received, wire_limit))
                goto fail;
        }

        if (wire[cursor + chunk_size] != '\r' || wire[cursor + chunk_size + 1] != '\n')
            goto fail;

        if (spool_to_disk)
        {
            if (!stream_write(stream, wire + cursor, chunk_size, &decoded_size, limit))
            {
                req->_parse_status = cHTTPX_StatusInternalServerError;
                goto fail;
            }
        }
        else if (!append_bytes(&decoded, &decoded_size, &decoded_capacity, wire + cursor, chunk_size, limit))
        {
            req->_parse_status = cHTTPX_StatusPayloadTooLarge;
            goto fail;
        }

        cursor += chunk_size + 2;
        if (cursor)
        {
            size_t left = wire_size - cursor;
            memmove(wire, wire + cursor, left);
            wire_size = left;
            if (wire)
                wire[wire_size] = '\0';
            cursor = 0;
        }
    }

    free(wire);
    req->content_length = decoded_size;

    if (spool_to_disk)
    {
        if (fflush(stream) != 0 || fseek(stream, 0, SEEK_SET) != 0)
        {
            req->_parse_status = cHTTPX_StatusInternalServerError;
            return 0;
        }
        req->_multipart_stream = stream;
        req->body = NULL;
        req->body_size = 0;
    }
    else
    {
        req->body = decoded;
        req->body_size = decoded_size;
    }
    return 1;

fail:
    free(wire);
    free(decoded);
    if (!req->_parse_status)
        req->_parse_status = cHTTPX_StatusBadRequest;
    return 0;
}

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

    const int multipart = is_multipart(req);
    const int memory_body = strstr(req->content_type, cHTTPX_CTYPE_JSON) || strstr(req->content_type, "text/") ||
                            strstr(req->content_type, cHTTPX_CTYPE_FORM);

    const char* body_start = chttpx_memmem(buffer, buffer_len, "\r\n\r\n", 4);
    if (!body_start)
    {
        req->body = NULL;
        req->body_size = 0;
        return;
    }

    body_start += 4;
    size_t body_in_buffer = buffer_len - (size_t)(body_start - buffer);

    const char* transfer_encoding = cHTTPX_HeaderGet(req, "Transfer-Encoding");
    if (transfer_encoding && strcasecmp(transfer_encoding, "chunked") == 0)
    {
        if (cHTTPX_HeaderGet(req, "Content-Length"))
        {
            req->_parse_status = cHTTPX_StatusBadRequest;
            return;
        }

        size_t limit = multipart ? serv->max_upload_size : (memory_body ? serv->max_body_size : serv->max_upload_size);
        decode_chunked(req, client_fd, (const unsigned char*)body_start, body_in_buffer, limit, multipart);
        return;
    }

    if (req->content_length == 0)
    {
        req->body = NULL;
        req->body_size = 0;
        return;
    }

    size_t limit = multipart ? serv->max_upload_size : (memory_body ? serv->max_body_size : serv->max_upload_size);
    if (req->content_length > limit)
    {
        req->_parse_status = cHTTPX_StatusPayloadTooLarge;
        return;
    }

    if (multipart)
    {
        receive_fixed_multipart(req, client_fd, (const unsigned char*)body_start, body_in_buffer);
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
        req->_parse_status = cHTTPX_StatusInternalServerError;
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

        int ready = select((int)client_fd + 1, &fds, NULL, NULL, &tv);
        if (ready <= 0)
            break;

        int received = recv(client_fd, (char*)req->body + total_read, remaining, 0);
        if (received <= 0)
            break;

        total_read += (size_t)received;
        remaining -= (size_t)received;
    }

    if (total_read != req->content_length)
    {
        free(req->body);
        req->body = NULL;
        req->body_size = 0;
        req->_parse_status = cHTTPX_StatusBadRequest;
        return;
    }

    req->body_size = total_read;
    req->body[req->body_size] = '\0';
}
