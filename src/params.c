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

#include "params.h"

#include "crosspltm.h"

#include <string.h>

int cHTTPX_MatchPath(const char* template, const char* path, chttpx_param_t* params, int* param_count)
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

            size_t name_len = (size_t)(t_end - t - 1);
            if (name_len >= MAX_PARAM_NAME)
                name_len = MAX_PARAM_NAME - 1;
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