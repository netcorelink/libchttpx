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

#include "cHTTPX_headers.h"

#include "cHTTPX_crosspltm.h"

static int valid_header_name(const char* name, size_t length);
static int valid_header_value(const char* value, size_t length);

/**
 * Get a request header by name.
 * @param req Pointer to the HTTP request.
 * @param name Header name (case-insensitive).
 * @return Pointer to header value if found, otherwise NULL.
 */
const char* cHTTPX_HeaderGet(chttpx_request_t* req, const char* name)
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
 * Add a new HTTP header.
 *
 * This function appends a header to the request/response header list.
 * Unlike HeaderSet, it does NOT replace existing headers with the same name.
 * This is required for headers like "Set-Cookie" that may appear multiple times.
 *
 * @param req   Pointer to HTTP request/response structure.
 * @param name  Header name.
 * @param value Header value.
 */
int cHTTPX_HeaderAdd(chttpx_response_t* res, const char* name, const char* value)
{
    if (!res || !name || !value)
        return -1;

    if (res->headers_count >= MAX_HEADERS)
        return -1;

    size_t name_length = strlen(name);
    size_t value_length = strlen(value);
    if (name_length >= MAX_HEADER_NAME || value_length >= MAX_HEADER_VALUE || !valid_header_name(name, name_length) ||
        !valid_header_value(value, value_length))
        return -1;

    chttpx_header_t* h = &res->headers[res->headers_count];
    memcpy(h->name, name, name_length + 1);
    memcpy(h->value, value, value_length + 1);
    res->headers_count++;

    return 0;
}

/**
 * Set or add a request header.
 * If header exists (case-insensitive), its value will be replaced.
 * Otherwise a new header will be added.
 *
 * @param req Pointer to the HTTP request.
 * @param name Header name.
 * @param value Header value.
 * @return 0 on success, -1 on error.
 */
int cHTTPX_HeaderSet(chttpx_request_t* req, const char* name, const char* value)
{
    if (!req || !name || !value)
        return -1;

    size_t name_length = strlen(name);
    size_t value_length = strlen(value);
    if (name_length >= MAX_HEADER_NAME || value_length >= MAX_HEADER_VALUE || !valid_header_name(name, name_length) ||
        !valid_header_value(value, value_length))
        return -1;

    for (size_t i = 0; i < req->headers_count; i++)
    {
        if (strcasecmp(req->headers[i].name, name) == 0)
        {
            memcpy(req->headers[i].value, value, value_length + 1);
            return 0;
        }
    }

    if (req->headers_count >= MAX_HEADERS)
        return -1;

    memcpy(req->headers[req->headers_count].name, name, name_length + 1);
    memcpy(req->headers[req->headers_count].value, value, value_length + 1);
    req->headers_count++;

    return 0;
}

/**
 * Get the client's IP from the HEADER request.
 *
 * @param req a pointer to the query structure
 * @return const char* Client's IP
 */
const char* cHTTPX_ClientIP(chttpx_request_t* req)
{
    if (!req)
        return "";

    const char* ip = cHTTPX_HeaderGet(req, "X-Forwarded-For");
    if (!ip)
        ip = cHTTPX_HeaderGet(req, "Remote-Addr");

    return ip;
}

static int add_header(chttpx_request_t* req, const char* name, const char* value)
{
    if (req->headers_count >= MAX_HEADERS)
        return 0;

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
    return 1;
}

static int valid_header_name(const char* name, size_t length)
{
    if (!name || length == 0)
        return 0;

    for (size_t i = 0; i < length; i++)
    {
        unsigned char ch = (unsigned char)name[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            continue;

        switch (ch)
        {
        case '!':
        case '#':
        case '$':
        case '%':
        case '&':
        case '\'':
        case '*':
        case '+':
        case '-':
        case '.':
        case '^':
        case '_':
        case '`':
        case '|':
        case '~':
            break;
        default:
            return 0;
        }
    }

    return 1;
}

static int valid_header_value(const char* value, size_t length)
{
    if (!value)
        return 0;

    for (size_t i = 0; i < length; i++)
    {
        unsigned char ch = (unsigned char)value[i];
        if ((ch < 0x20 && ch != '\t') || ch == 0x7f)
            return 0;
    }

    return 1;
}

/* Parse headers in request */
void _parse_req_headers(chttpx_request_t* req, char* buffer, size_t buffer_len)
{
    if (!req || !buffer)
        return;

    char* header_end = chttpx_memmem(buffer, buffer_len, "\r\n\r\n", 4);
    if (!header_end)
    {
        req->_parse_status = 400;
        return;
    }

    /*
     * The delimiter starts at the CRLF terminating the last header (or the
     * request line when there are no headers). Never scan beyond it: buffer
     * may already contain request-body bytes from the same recv().
     */
    char* parse_end = header_end + 2;
    char* line_start = buffer;
    char* newline = memchr(line_start, '\n', (size_t)(parse_end - line_start));
    if (!newline || newline == line_start || newline[-1] != '\r')
    {
        req->_parse_status = 400;
        return;
    }
    line_start = newline + 1;

    while (line_start < parse_end)
    {
        newline = memchr(line_start, '\n', (size_t)(parse_end - line_start));
        if (!newline || newline == line_start || newline[-1] != '\r')
        {
            req->_parse_status = 400;
            return;
        }

        size_t line_len = (size_t)(newline - line_start - 1);
        if (line_len == 0)
            break;

        char* colon = memchr(line_start, ':', line_len);
        if (!colon)
        {
            req->_parse_status = 400;
            return;
        }

        size_t name_len = (size_t)(colon - line_start);
        if (!valid_header_name(line_start, name_len) || name_len >= MAX_HEADER_NAME)
        {
            req->_parse_status = 400;
            return;
        }

        char* value_start = colon + 1;
        size_t value_len = line_len - name_len - 1;
        while (value_len > 0 && (*value_start == ' ' || *value_start == '\t'))
        {
            value_start++;
            value_len--;
        }
        while (value_len > 0 && (value_start[value_len - 1] == ' ' || value_start[value_len - 1] == '\t'))
            value_len--;

        if (value_len >= MAX_HEADER_VALUE)
        {
            req->_parse_status = 431;
            return;
        }

        if (!valid_header_value(value_start, value_len))
        {
            req->_parse_status = 400;
            return;
        }

        char name_buf[MAX_HEADER_NAME];
        char value_buf[MAX_HEADER_VALUE];
        memcpy(name_buf, line_start, name_len);
        name_buf[name_len] = '\0';
        memcpy(value_buf, value_start, value_len);
        value_buf[value_len] = '\0';

        if (!add_header(req, name_buf, value_buf))
        {
            req->_parse_status = 431;
            return;
        }

        line_start = newline + 1;
    }
}
