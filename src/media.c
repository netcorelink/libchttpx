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

#include "media.h"

#include "http.h"
#include "headers.h"
#include "request.h"
#include "crosspltm.h"

#include <string.h>

static void _parse_multipart(chttpx_request_t* req);

void _parse_media(chttpx_request_t* req)
{
    const char* content_type = cHTTPX_Header(req, "Content-Type");
    if (!content_type || !req->body)
        return;

    if (strstr(content_type, cHTTPX_CTYPE_MULTI))
    {
        _parse_multipart(req);
    }
    else
    {
        const char* name = strchr(req->path, '/');
        if (name)
        {
            strncpy(req->filename, name + 1, sizeof(req->filename) - 1);
        }
        else
        {
            strcpy(req->filename, "upload.bin");
        }
    }
}

static void _parse_multipart(chttpx_request_t* req)
{
    unsigned char* data = req->body;
    size_t data_len = req->body_size;

    unsigned char* filename_pos = memmem(data, data_len, "filename=\"", 10);
    if (!filename_pos)
        return;

    unsigned char* end_quote = memmem(filename_pos, data + data_len - filename_pos, "\"", 1);
    if (!end_quote)
        return;

    size_t fname_len = end_quote - filename_pos;
    if (fname_len >= sizeof(req->filename))
        fname_len = sizeof(req->filename) - 1;

    memcpy(req->filename, filename_pos, fname_len);
    req->filename[fname_len] = '\0';

    unsigned char* file_start = memmem(end_quote, data + data_len - end_quote, "\r\n\r\n", 4);
    if (!file_start)
        return;
    file_start += 4;

    unsigned char* file_end = memmem(file_start, data + data_len - file_start, "\r\n--", 4);
    if (!file_end)
        file_end = data + data_len;

    req->body = file_start;
    req->body_size = file_end - file_start;
}