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

#include "include/queries.h"

#include "include/crosspltm.h"

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
const char* cHTTPX_Query(chttpx_request_t *req, const char *name) {
    if (!req || !req->query || req->query_count == 0) return NULL;

    for (size_t i = 0; i < req->query_count; i++) {
        if (strcmp(req->query[i].name, name) == 0) {
            return req->query[i].value;
        }
    }

    return NULL;
}

void _parse_req_query(chttpx_request_t *req, char *query) {
    char *token = strtok(query, "&");

    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';

            req->query = realloc(req->query, sizeof(chttpx_query_t) * (req->query_count + 1));

            req->query[req->query_count].name = strdup(token);
            req->query[req->query_count].value = strdup(eq+1);
            req->query_count++;
        }

        token = strtok(NULL, "&");
    }
}
