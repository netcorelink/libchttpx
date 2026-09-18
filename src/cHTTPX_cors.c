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

#include "cHTTPX_cors.h"

#include "cHTTPX_crosspltm.h"
#include "cHTTPX_serv.h"

#include <stdio.h>
#include <stdlib.h>

/**
 * Enable and configure CORS (Cross-Origin Resource Sharing).
 *
 * This function enables CORS support for the HTTP server and configures
 * which origins, HTTP methods, and request headers are allowed.
 *
 * The CORS configuration is applied globally and is typically used together
 * with the built-in CORS middleware.
 *
 * @param origins        Array of allowed origin strings (e.g. "https://example.com").
 *                       Each origin must match exactly the value of the "Origin" header.
 * @param origins_count Number of elements in the origins array.
 * @param methods       Comma-separated list of allowed HTTP methods.
 *                       If NULL, defaults to:
 *                       "GET, POST, PUT, DELETE, OPTIONS"
 * @param headers       Comma-separated list of allowed request headers.
 *                       If NULL, defaults to:
 *                       "Content-Type"
 */
void cHTTPX_Cors(chttpx_serv_t* server, const char** origins, size_t origins_count, const char* methods, const char* headers)
{
    if (!server || !server->initialized)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    const char* selected_methods = methods ? methods : "GET, POST, PUT, DELETE, OPTIONS";
    const char* selected_headers = headers ? headers : "Content-Type";
    const char** owned_origins = calloc(origins_count, sizeof(*owned_origins));
    char* owned_methods = strdup(selected_methods);
    char* owned_headers = strdup(selected_headers);
    if ((origins_count && !owned_origins) || !owned_methods || !owned_headers)
        goto memory_error;
    for (size_t i = 0; i < origins_count; i++)
    {
        owned_origins[i] = origins && origins[i] ? strdup(origins[i]) : NULL;
        if (!owned_origins[i])
        {
            for (size_t j = 0; j < i; j++)
                free((void*)owned_origins[j]);
            goto memory_error;
        }
    }
    for (size_t i = 0; i < server->cors.origins_count; i++)
        free((void*)server->cors.origins[i]);
    free((void*)server->cors.origins);
    free((void*)server->cors.methods);
    free((void*)server->cors.headers);
    server->cors.enabled = 1;
    server->cors.origins = owned_origins;
    server->cors.origins_count = origins_count;
    server->cors.methods = owned_methods;
    server->cors.headers = owned_headers;
    return;

memory_error:
    free(owned_origins);
    free(owned_methods);
    free(owned_headers);
}
