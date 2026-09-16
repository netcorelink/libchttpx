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

#include "queries.h"

#include "crosspltm.h"

#include <errno.h>
#include <limits.h>

/**
 * Get a query parameter value by name.
 *
 * Searches the parsed URL query parameters (e.g. ?name=value&age=10)
 * and returns the value associated with the given parameter name.
 *
 * @param req   Pointer to the current HTTP request.
 * @param name  Name of the query parameter.
 * @return Pointer to the parameter value string if found, or NULL if not present.
 */
const char* cHTTPX_Query(chttpx_request_t* req, const char* name)
{
    if (!req || !req->query || req->query_count == 0)
        return NULL;

    for (size_t i = 0; i < req->query_count; i++)
    {
        if (strcmp(req->query[i].name, name) == 0)
        {
            return req->query[i].value;
        }
    }

    return NULL;
}

/* Parse queries in request */
void _parse_req_query(chttpx_request_t* req, char* query)
{
    if (!req || !query)
        return;

    char* token = query;
    while (token && *token)
    {
        char* next = strchr(token, '&');
        if (next)
            *next++ = '\0';
        char* eq = strchr(token, '=');
        if (eq)
        {
            *eq = '\0';
            chttpx_query_t* resized = realloc(req->query, sizeof(chttpx_query_t) * (req->query_count + 1));
            if (!resized)
            {
                req->_parse_status = 500;
                return;
            }
            req->query = resized;

            size_t name_size = strlen(token) + 1;
            size_t value_size = strlen(eq + 1) + 1;
            req->query[req->query_count].name = malloc(name_size);
            req->query[req->query_count].value = malloc(value_size);
            if (!req->query[req->query_count].name || !req->query[req->query_count].value)
            {
                free(req->query[req->query_count].name);
                free(req->query[req->query_count].value);
                req->_parse_status = 500;
                return;
            }
            if (!cHTTPX_UrlDecode(req->query[req->query_count].name, name_size, token, true) ||
                !cHTTPX_UrlDecode(req->query[req->query_count].value, value_size, eq + 1, true))
            {
                free(req->query[req->query_count].name);
                free(req->query[req->query_count].value);
                req->_parse_status = 400;
                return;
            }
            req->query_count++;
        }
        token = next;
    }
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

int cHTTPX_UrlDecode(char* destination, size_t destination_size, const char* source, bool plus_as_space)
{
    if (!destination || destination_size == 0 || !source)
        return 0;
    size_t output = 0;
    for (size_t input = 0; source[input]; input++)
    {
        unsigned char value = (unsigned char)source[input];
        if (value == '%' && source[input + 1] && source[input + 2])
        {
            int high = hex_value(source[input + 1]);
            int low = hex_value(source[input + 2]);
            if (high < 0 || low < 0)
                return 0;
            value = (unsigned char)((high << 4) | low);
            if (value == 0)
                return 0;
            input += 2;
        }
        else if (value == '%' || (value == '+' && plus_as_space))
        {
            if (value == '%')
                return 0;
            value = ' ';
        }
        if (output + 1 >= destination_size)
            return 0;
        destination[output++] = (char)value;
    }
    destination[output] = '\0';
    return 1;
}

static int query_i64(chttpx_request_t* req, const char* name, long long min_value, long long max_value, long long* value)
{
    const char* text = cHTTPX_Query(req, name);
    if (!text || !*text || !value)
        return 0;
    errno = 0;
    char* end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (errno == ERANGE || !end || *end || parsed < min_value || parsed > max_value)
        return 0;
    *value = parsed;
    return 1;
}

int cHTTPX_QueryInt(chttpx_request_t* req, const char* name, int* value)
{
    long long parsed;
    if (!value || !query_i64(req, name, INT_MIN, INT_MAX, &parsed))
        return 0;
    *value = (int)parsed;
    return 1;
}

int cHTTPX_QueryU64(chttpx_request_t* req, const char* name, uint64_t* value)
{
    const char* text = cHTTPX_Query(req, name);
    if (!text || !*text || *text == '-' || !value)
        return 0;
    errno = 0;
    char* end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || !end || *end)
        return 0;
    *value = (uint64_t)parsed;
    return 1;
}

int cHTTPX_QueryBool(chttpx_request_t* req, const char* name, bool* value)
{
    const char* text = cHTTPX_Query(req, name);
    if (!text || !value)
        return 0;
    if (strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0)
        *value = true;
    else if (strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0)
        *value = false;
    else
        return 0;
    return 1;
}

int cHTTPX_QueryDouble(chttpx_request_t* req, const char* name, double* value)
{
    const char* text = cHTTPX_Query(req, name);
    if (!text || !*text || !value)
        return 0;
    errno = 0;
    char* end = NULL;
    double parsed = strtod(text, &end);
    if (errno == ERANGE || !end || *end)
        return 0;
    *value = parsed;
    return 1;
}

int cHTTPX_QueryU64Default(chttpx_request_t* req, const char* name, uint64_t* value, uint64_t default_value)
{
    if (!value)
        return 0;
    if (!cHTTPX_Query(req, name))
    {
        *value = default_value;
        return 1;
    }
    return cHTTPX_QueryU64(req, name, value);
}
