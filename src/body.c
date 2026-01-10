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

#include "include/body.h"

#include "include/crosspltm.h"

#include <stdio.h>

void _parse_req_body(chttpx_request_t *req, char *buffer, size_t buffer_len) {
    const char *body_start = memmem(buffer, buffer_len, "\r\n\r\n", 4);
    if (!body_start) {
        req->body = NULL;
        return;
    }

    body_start += 4;

    int content_length = 0;
    const char *cl_header = memmem(buffer, buffer_len, "Content-Length:", 15);
    if (!cl_header || sscanf(cl_header, "Content-Length: %d", &content_length) != 1) {
        req->body = NULL;
        return;
    }

    if (content_length <= 0) {
        req->body = NULL;
        return;
    }

    size_t body_in_buffer = buffer_len - (body_start - buffer);
    if (body_in_buffer < (size_t)content_length) {
        req->body =  NULL;
        return;
    }

    req->body = malloc(content_length + 1);
    memcpy(req->body, body_start, content_length);
    req->body[content_length] = '\0';
}