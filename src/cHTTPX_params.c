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

#include "cHTTPX_params.h"

#include "cHTTPX_crosspltm.h"

#include <errno.h>
#include <limits.h>

#include "cHTTPX_crosspltm.h"

/**
 * Get a route parameter value by its name.
 * @param req  Pointer to the current HTTP request structure.
 * @param name Name of the route parameter (e.g., "uuid").
 *
 * @return Pointer to the parameter value string if found, or NULL if the parameter does not exist.
 */
const char* cHTTPX_Param(chttpx_request_t* req, const char* name)
{
    if (!req || !name || req->params_count == 0)
        return NULL;

    for (size_t i = 0; i < req->params_count; i++)
    {
        if (strcmp(req->params[i].name, name) == 0)
        {
            return req->params[i].value;
        }
    }

    return NULL;
}

static int parse_i64(const char* text, long long min_value, long long max_value, long long* value)
{
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

int cHTTPX_ParamInt(chttpx_request_t* req, const char* name, int* value)
{
    long long parsed;
    if (!value || !parse_i64(cHTTPX_Param(req, name), INT_MIN, INT_MAX, &parsed))
        return 0;
    *value = (int)parsed;
    return 1;
}

int cHTTPX_ParamU64(chttpx_request_t* req, const char* name, uint64_t* value)
{
    const char* text = cHTTPX_Param(req, name);
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

int cHTTPX_ParamBool(chttpx_request_t* req, const char* name, bool* value)
{
    const char* text = cHTTPX_Param(req, name);
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
