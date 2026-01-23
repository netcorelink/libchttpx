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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Save body(file) to temp file */
static int _save_body_to_temp_file(chttpx_socket_t client_fd, size_t content_length, char* tmp_filename, size_t tmp_filename_size);

/* Parse media in request */
void _parse_media(chttpx_request_t* req)
{
    const char* content_type = cHTTPX_Header(req, "Content-Type");
    if (!content_type || !req->body)
        return;

    char tmp_filename[512];

    if (strstr(content_type, cHTTPX_CTYPE_MULTI))
    {
        if (_save_body_to_temp_file(req->client_fd, req->content_length, tmp_filename, sizeof(tmp_filename)) != 0)
        {
            fprintf(stderr, "error write body in temp file\n");
            return;
        }

        FILE* f = fopen(tmp_filename, "rb");
        if (!f)
            return;

        char line[1024];
        while (fgets(line, sizeof(line), f))
        {
            char* start = strstr(line, "filename=\"");
            if (start)
            {
                start += 10;
                char* end = strchr(start, '"');
                if (end)
                {
                    size_t len = end - start;
                    if (len >= sizeof(req->filename))
                        len = sizeof(req->filename) - 1;
                    memcpy(req->filename, start, len);
                    req->filename[len] = '\0';
                    break;
                }
            }
        }

        fclose(f);

        snprintf(req->filename, sizeof(req->filename), "%.*s", (int)(sizeof(req->filename) - 1), tmp_filename);
    }
    else
    {
        if (_save_body_to_temp_file(req->client_fd, req->content_length, tmp_filename, sizeof(tmp_filename)) != 0)
        {
            fprintf(stderr, "error write body in temp file\n");
            return;
        }

        const char* name = strrchr(req->path, '/');
        if (name && *(name + 1) != '\0')
        {
            strncpy(req->filename, name + 1, sizeof(req->filename) - 1);
        }
        else
        {
            strcpy(req->filename, tmp_filename);
        }
    }
}

/* Save body(file) to temp file */
static int _save_body_to_temp_file(chttpx_socket_t client_fd, size_t content_length, char* tmp_filename, size_t tmp_filename_size)
{
    snprintf(tmp_filename, tmp_filename_size, "/tmp/upload_%ld.tmp", random());
    FILE* f = fopen(tmp_filename, "wb");
    if (!f)
        return 1;

    unsigned char buffer[FILE_BUFFER];
    size_t total = 0;
    ssize_t n;

    while (total < content_length)
    {
        size_t to_read = sizeof(buffer);
        if (content_length - total < to_read)
            to_read = content_length - total;

        n = recv(client_fd, buffer, to_read, 0);
        if (n <= 0)
            break;

        fwrite(buffer, 1, n, f);
        total += n;
    }

    fclose(f);

    return total == content_length ? 0 : 1;
}