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

#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

/* Save body(file) to temp file */
static int _save_body_to_temp_file(chttpx_request_t* req, char* initial_buffer, size_t initial_len, char* tmp_filename, size_t tmp_filename_size);
/* Get ext file by content type */
static const char* content_type_to_ext(const char* content_type);

/* Ext map with content types */
static const content_type_map_t content_type_map[] = {
    { cHTTPX_CTYPE_HTML,  ".html" },
    { cHTTPX_CTYPE_TEXT,  ".txt" },
    { cHTTPX_CTYPE_XML,   ".xml" },
    { cHTTPX_CTYPE_CSS,   ".css" },
    { cHTTPX_CTYPE_CSV,   ".csv" },
    { cHTTPX_CTYPE_JSON,  ".json" },
    { cHTTPX_CTYPE_FORM,  ".txt" },
    { cHTTPX_CTYPE_MULTI, ".bin" },

    { cHTTPX_CTYPE_OCTET, ".bin" },

    { cHTTPX_CTYPE_JS,    ".js" },

    { cHTTPX_CTYPE_PNG,   ".png" },
    { cHTTPX_CTYPE_JPEG,  ".jpg" },
    { cHTTPX_CTYPE_GIF,   ".gif" },
    { cHTTPX_CTYPE_WEBP,  ".webp" },
    { cHTTPX_CTYPE_SVG,   ".svg" },
    { cHTTPX_CTYPE_BMP,   ".bmp" },

    { cHTTPX_CTYPE_MP3,   ".mp3" },
    { cHTTPX_CTYPE_WAV,   ".wav" },
    { cHTTPX_CTYPE_OGG,   ".ogg" },

    { cHTTPX_CTYPE_MP4,   ".mp4" },
    { cHTTPX_CTYPE_WEBM,  ".webm" },
    { cHTTPX_CTYPE_AVI,   ".avi" },

    { cHTTPX_CTYPE_ZIP,   ".zip" },
    { cHTTPX_CTYPE_RAR,   ".rar" },
    { cHTTPX_CTYPE_7Z,    ".7z" },

    { cHTTPX_CTYPE_PDF,   ".pdf" },
    { cHTTPX_CTYPE_DOC,   ".doc" },
    { cHTTPX_CTYPE_DOCX,  ".docx" },
    { cHTTPX_CTYPE_XLS,   ".xls" },
    { cHTTPX_CTYPE_XLSX,  ".xlsx" },

    { cHTTPX_CTYPE_WOFF,  ".woff" },
    { cHTTPX_CTYPE_WOFF2, ".woff2" },
    { cHTTPX_CTYPE_TTF,   ".ttf" },
    { cHTTPX_CTYPE_OTF,   ".otf" },

    { NULL, ".tmp" }
};

/* Parse media in request */
void _parse_media(chttpx_request_t* req, char* buffer, size_t buffer_len)
{
    const char* content_type = cHTTPX_Header(req, "Content-Type");
    if (!content_type)
        return;

    if (req->content_length > 0 && (!content_type || !strstr(content_type, cHTTPX_CTYPE_JSON)))
    {
        char tmp_filename[512];
        if (_save_body_to_temp_file(req, buffer, buffer_len, tmp_filename, sizeof(tmp_filename)) != 0)
        {
            fprintf(stderr, "error write body in temp file\n");
            return;
        }

        snprintf(req->filename, sizeof(req->filename), "%.*s", (int)(sizeof(req->filename) - 1), tmp_filename);

        req->body = NULL;
        req->body_size = 0;
    }
}

/* Save file to temp file */
static int _save_body_to_temp_file(chttpx_request_t* req, char* initial_buffer, size_t initial_len, char* tmp_filename, size_t tmp_filename_size)
{
    const char* ext = content_type_to_ext(cHTTPX_Header(req, "Content-Type"));

    snprintf(tmp_filename, tmp_filename_size, "/tmp/upload_%ld%s", random(), ext);
    FILE* f = fopen(tmp_filename, "wb");
    if (!f)
        return 1;

    size_t total_written = 0;

    const char* body_start = memmem(initial_buffer, initial_len, "\r\n\r\n", 4);
    if (!body_start)
    {
        fclose(f);
        return 1;
    }

    body_start += 4;
    size_t body_in_buffer = initial_len - (body_start - initial_buffer);
    if (body_in_buffer > req->content_length) body_in_buffer = req->content_length;

    if (fwrite(body_start, 1, body_in_buffer, f) != body_in_buffer)
    {
        fclose(f);
        return 1;
    }
    total_written += body_in_buffer;

    unsigned char tmp_buf[FILE_BUFFER];

    while (total_written < req->content_length)
    {
        size_t to_read = FILE_BUFFER;
        if (req->content_length - total_written < to_read) to_read = req->content_length - total_written;

        ssize_t n = recv(req->client_fd, tmp_buf, to_read, 0);
        if (n <= 0)
        {
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
                continue;
            fclose(f);
            return 1;
        }

        if (fwrite(tmp_buf, 1, n, f) != (size_t)n)
        {
            fclose(f);
            return 1;
        }

        total_written += n;
    }

    fclose(f);
    return 0;
}

/* Get ext file by content type */
static const char* content_type_to_ext(const char* content_type)
{
    if (!content_type) return ".tmp";

    for (size_t i = 0; content_type_map[i].ctype; i++)
    {
        if (strstr(content_type, content_type_map[i].ctype)) 
        {
            return content_type_map[i].ext;
        }
    }

    return ".tmp";
}