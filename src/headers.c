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

#include "headers.h"

#include "crosspltm.h"

/**
 * Get a request header by name.
 * @param req Pointer to the HTTP request.
 * @param name Header name (case-insensitive).
 * @return Pointer to header value if found, otherwise NULL.
 */
const char* cHTTPX_Header(chttpx_request_t* req, const char* name)
{
    if (!req || req->headers_count == 0 || !name)
        return NULL;

    for (size_t i = 0; i < req->headers_count; i++)
    {
        if (strcasecmp(req->headers[i].name, name) == 0)
        {
            return req->headers[i].value;
        }
    }

    return NULL;
}

/**
 * Get the client's IP from the HEADER request.
 *
 * @param req a pointer to the query structure
 * @return const char* Client's IP
 */
const char* cHTTPX_ClientIP(chttpx_request_t* req)
{
    if (!req) return "";

    const char* ip = cHTTPX_Header(req, "X-Forwarded-For");
    if (!ip)
        ip = cHTTPX_Header(req, "Remote-Addr");

    return ip;
}

static void add_header(chttpx_request_t* req, const char* name, const char* value)
{
    if (req->headers_count >= MAX_HEADERS)
        return;

    chttpx_header_t* h = &req->headers[req->headers_count++];

    size_t len_name = strlen(name);
    if (len_name >= MAX_HEADER_NAME)
        len_name = MAX_HEADER_NAME - 1;
    memcpy(h->name, name, len_name);
    h->name[len_name] = '\0';

    size_t len_value = strlen(value);
    if (len_value >= MAX_HEADER_VALUE)
        len_value = MAX_HEADER_VALUE - 1;
    memcpy(h->value, value, len_value);
    h->value[len_value] = '\0';
}

/* Parse headers in request */
void _parse_req_headers(chttpx_request_t* req, char* buffer, size_t buffer_len)
{
    char* line_start = buffer;
    char* buffer_end = buffer + buffer_len;

    /* Meta data - continue one line */
    char* newline = memchr(line_start, '\n', buffer_end - line_start);
    if (!newline)
        return;
    line_start = newline + 1;

    while (line_start < buffer_end)
    {
        newline = memchr(line_start, '\n', buffer_end - line_start);
        if (!newline)
            break;

        size_t line_len = newline - line_start;
        if (line_len > 0 && line_start[line_len - 1] == '\r')
            line_len--;

        char* colon = memchr(line_start, ':', line_len);
        if (colon)
        {
            size_t name_len = colon - line_start;
            size_t value_len = line_len - name_len - 1;

            char* value_start = colon + 1;
            while (value_len > 0 && *value_start == ' ')
            {
                value_start++;
                value_len--;
            }

            char name_buf[MAX_HEADER_NAME];
            char value_buf[MAX_HEADER_VALUE];

            size_t copy_name = name_len < MAX_HEADER_NAME - 1 ? name_len : MAX_HEADER_NAME - 1;
            size_t copy_value = value_len < MAX_HEADER_VALUE - 1 ? value_len : MAX_HEADER_VALUE - 1;

            memcpy(name_buf, line_start, copy_name);
            name_buf[copy_name] = '\0';

            memcpy(value_buf, value_start, copy_value);
            value_buf[copy_value] = '\0';

            add_header(req, name_buf, value_buf);
        }

        line_start = newline + 1;
    }
}
