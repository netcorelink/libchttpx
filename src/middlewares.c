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

#include "include/middlewares.h"

#include "include/serv.h"
#include "include/request.h"
#include "include/response.h"
#include "include/crosspltm.h"

#include <stdio.h>

/**
 * Register a global middleware function.
 *
 * Middleware functions are executed in the order they are registered,
 * before the route handler is called.
 *
 * If a middleware returns 0, the middleware chain is aborted and the
 * response provided by the middleware is sent to the client.
 *
 * If a middleware returns 1, processing continues to the next middleware
 * or to the route handler.
 *
 * @param mw Middleware function pointer.
 */
void cHTTPX_MiddlewareUse(chttpx_middleware_t mw) {
    if (!serv) {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    if (serv->middleware.middleware_count >= MAX_MIDDLEWARES) {
        fprintf(stderr, "Error: the number of middleware (MAX_MIDDLEWARES) has been exceeded\n");
        return;
    }

    serv->middleware.middlewares[serv->middleware.middleware_count++] = mw;
}