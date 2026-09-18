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
#include <string.h>
#include <limits.h>

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
        size_t required = *size + count + 1;
        size_t maximum = limit < SIZE_MAX ? limit + 1 : SIZE_MAX;
        size_t new_capacity = *capacity ? *capacity : 4096;
        while (new_capacity < required)
        {
            if (new_capacity > maximum / 2)
            {
                new_capacity = maximum;
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

typedef struct
{
    chttpx_socket_t client_fd;
    const unsigned char* initial;
    size_t initial_size;
    size_t initial_offset;
} chunked_reader_t;

static int chunked_reader_read(chunked_reader_t* reader, unsigned char* output, size_t output_size)
{
    if (!reader || !output || output_size == 0)
        return -1;

    if (reader->initial_offset < reader->initial_size)
    {
        size_t available = reader->initial_size - reader->initial_offset;
        size_t count = available < output_size ? available : output_size;
        memcpy(output, reader->initial + reader->initial_offset, count);
        reader->initial_offset += count;
        return (int)count;
    }

    size_t wanted = output_size;
#ifdef CHTTPX_PLATFORM_WINDOWS
    if (wanted > INT_MAX)
        wanted = INT_MAX;
#endif
    return recv(reader->client_fd, (char*)output, wanted, 0);
}

static int chunked_reader_exact(chunked_reader_t* reader, unsigned char* output, size_t output_size)
{
    size_t offset = 0;
    while (offset < output_size)
    {
        int received = chunked_reader_read(reader, output + offset, output_size - offset);
        if (received <= 0)
            return 0;
        offset += (size_t)received;
    }
    return 1;
}

static int chunked_reader_line(chunked_reader_t* reader, char* line, size_t line_size)
{
    if (!reader || !line || line_size < 2)
        return -1;

    size_t length = 0;
    bool saw_cr = false;
    for (;;)
    {
        unsigned char byte;
        if (!chunked_reader_exact(reader, &byte, 1))
            return -1;

        if (saw_cr)
        {
            if (byte != '\n')
                return -1;
            line[length] = '\0';
            return (int)length;
        }

        if (byte == '\r')
        {
            saw_cr = true;
            continue;
        }

        if (length + 1 >= line_size)
            return -1;
        line[length++] = (char)byte;
    }
}

static void close_stream(void* resource)
{
    if (resource)
        fclose((FILE*)resource);
}

static int decode_chunked(chttpx_request_t* req, chttpx_socket_t client_fd, const unsigned char* initial, size_t initial_size, size_t limit,
                          bool spool_to_disk)
{
    unsigned char* decoded = NULL;
    size_t decoded_size = 0;
    size_t decoded_capacity = 0;
    FILE* spool = NULL;

    if (spool_to_disk)
    {
        spool = tmpfile();
        if (!spool)
        {
            req->_parse_status = cHTTPX_StatusInternalServerError;
            return 0;
        }
    }

    chunked_reader_t reader = {
        .client_fd = client_fd,
        .initial = initial,
        .initial_size = initial_size,
        .initial_offset = 0,
    };

    for (;;)
    {
        char line[64];
        int line_length = chunked_reader_line(&reader, line, sizeof(line));
        if (line_length < 0)
            goto bad_request;

        char* extension = strchr(line, ';');
        if (extension)
            *extension = '\0';
        if (!*line)
            goto bad_request;

        errno = 0;
        char* end = NULL;
        unsigned long long chunk_size = strtoull(line, &end, 16);
        if (errno == ERANGE || !end || *end || chunk_size > SIZE_MAX)
            goto bad_request;

        if (chunk_size > limit - decoded_size)
        {
            req->_parse_status = cHTTPX_StatusPayloadTooLarge;
            goto fail;
        }

        if (chunk_size == 0)
        {
            for (;;)
            {
                int trailer_length = chunked_reader_line(&reader, line, sizeof(line));
                if (trailer_length < 0)
                    goto bad_request;
                if (trailer_length == 0)
                    break;
            }
            break;
        }

        size_t remaining = (size_t)chunk_size;
        unsigned char buffer[BUFFER_SIZE];
        while (remaining > 0)
        {
            size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (!chunked_reader_exact(&reader, buffer, wanted))
                goto bad_request;

            if (spool)
            {
                if (fwrite(buffer, 1, wanted, spool) != wanted)
                {
                    req->_parse_status = cHTTPX_StatusInternalServerError;
                    goto fail;
                }
            }
            else if (!append_bytes(&decoded, &decoded_size, &decoded_capacity, buffer, wanted, limit))
            {
                req->_parse_status = cHTTPX_StatusInternalServerError;
                goto fail;
            }

            if (spool)
                decoded_size += wanted;
            remaining -= wanted;
        }

        unsigned char crlf[2];
        if (!chunked_reader_exact(&reader, crlf, sizeof(crlf)) || crlf[0] != '\r' || crlf[1] != '\n')
            goto bad_request;
    }

    req->content_length = decoded_size;
    if (spool)
    {
        if (fflush(spool) != 0 || fseek(spool, 0, SEEK_SET) != 0 || cHTTPX_Defer(req, spool, close_stream) != 0)
        {
            req->_parse_status = cHTTPX_StatusInternalServerError;
            goto fail;
        }
        req->_multipart_stream = spool;
        req->body = NULL;
        req->body_size = 0;
    }
    else
    {
        req->body = decoded;
        req->body_size = decoded_size;
    }
    return 1;

bad_request:
    req->_parse_status = cHTTPX_StatusBadRequest;
fail:
    free(decoded);
    if (spool)
        fclose(spool);
    return 0;
}

static int spool_multipart_body(chttpx_request_t* req, chttpx_socket_t client_fd, const unsigned char* initial, size_t initial_size)
{
    FILE* spool = tmpfile();
    if (!spool)
    {
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return 0;
    }

    size_t buffered = initial_size;
    if (buffered > req->content_length)
        buffered = req->content_length;

    if (buffered > 0 && fwrite(initial, 1, buffered, spool) != buffered)
    {
        fclose(spool);
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return 0;
    }

    size_t total = buffered;
    unsigned char chunk[BUFFER_SIZE];
    while (total < req->content_length)
    {
        size_t wanted = req->content_length - total;
        if (wanted > sizeof(chunk))
            wanted = sizeof(chunk);

        int received = recv(client_fd, (char*)chunk, wanted, 0);
        if (received <= 0)
        {
            fclose(spool);
            req->_parse_status = cHTTPX_StatusBadRequest;
            return 0;
        }

        if (fwrite(chunk, 1, (size_t)received, spool) != (size_t)received)
        {
            fclose(spool);
            req->_parse_status = cHTTPX_StatusInternalServerError;
            return 0;
        }
        total += (size_t)received;
    }

    if (fflush(spool) != 0 || fseek(spool, 0, SEEK_SET) != 0 || cHTTPX_Defer(req, spool, close_stream) != 0)
    {
        fclose(spool);
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return 0;
    }

    req->_multipart_stream = spool;
    req->body = NULL;
    req->body_size = 0;
    return 1;
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

    bool multipart_body = strstr(req->content_type, cHTTPX_CTYPE_MULTI) != NULL;
    int memory_body = strstr(req->content_type, cHTTPX_CTYPE_JSON) || strstr(req->content_type, "text/") || strstr(req->content_type, cHTTPX_CTYPE_FORM);

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
        size_t limit = multipart_body ? serv->max_upload_size : (memory_body ? serv->max_body_size : serv->max_upload_size);
        decode_chunked(req, client_fd, (const unsigned char*)body_start, body_in_buffer, limit, multipart_body);
        return;
    }

    if (req->content_length == 0)
    {
        req->body = NULL;
        req->body_size = 0;
        return;
    }

    size_t limit = multipart_body ? serv->max_upload_size : (memory_body ? serv->max_body_size : serv->max_upload_size);
    if (req->content_length > limit)
    {
        req->_parse_status = cHTTPX_StatusPayloadTooLarge;
        return;
    }

    if (multipart_body)
    {
        spool_multipart_body(req, client_fd, (const unsigned char*)body_start, body_in_buffer);
        return;
    }

    if (!memory_body)
    {
        req->body = NULL;
        req->body_size = 0;
        return;
    }

    if (req->content_length == SIZE_MAX)
    {
        req->_parse_status = cHTTPX_StatusPayloadTooLarge;
        return;
    }

    req->body = malloc(req->content_length + 1);
    if (!req->body)
    {
        perror("malloc failed");
        req->_parse_status = cHTTPX_StatusInternalServerError;
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

        total_read += (size_t)n;
        remaining -= (size_t)n;
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
    ((char*)req->body)[req->body_size] = '\0';
}
