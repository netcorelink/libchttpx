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

#include "cHTTPX_media.h"

#include "cHTTPX_headers.h"
#include "cHTTPX_queries.h"
#include "cHTTPX_serv.h"
#include "cHTTPX_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CHTTPX_PLATFORM_POSIX
#include <unistd.h>
#endif

#define MULTIPART_LINE_LIMIT 4096
#define MULTIPART_FORM_VALUE_LIMIT (1024 * 1024)
#define MULTIPART_MAX_PARTS 256

static int append_part_value(unsigned char** data, size_t* size, size_t* capacity, const unsigned char* bytes, size_t count, size_t limit)
{
    if (*size > limit || count > limit - *size || *size == SIZE_MAX || count > SIZE_MAX - *size - 1)
        return 0;
    size_t required = *size + count + 1;
    if (required > *capacity)
    {
        size_t next = *capacity ? *capacity : 4096;
        while (next < required)
        {
            if (next > (limit + 1) / 2)
            {
                next = limit + 1;
                break;
            }
            next *= 2;
        }
        if (next < required)
            return 0;
        unsigned char* resized = realloc(*data, next);
        if (!resized)
            return 0;
        *data = resized;
        *capacity = next;
    }
    memcpy(*data + *size, bytes, count);
    *size += count;
    (*data)[*size] = '\0';
    return 1;
}

static void remove_temporary_file(void* resource)
{
    char* path = resource;
    if (path)
    {
        remove(path);
        free(path);
    }
}

static FILE* create_temporary_file(char* path, size_t path_size)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    (void)path_size;
    char directory[MAX_PATH];
    if (!GetTempPathA(sizeof(directory), directory) || !GetTempFileNameA(directory, "chx", 0, path))
        return NULL;
    return fopen(path, "w+b");
#else
    if (path_size < 24)
        return NULL;
    snprintf(path, path_size, "/tmp/chttpx-upload-XXXXXX");
    int fd = mkstemp(path);
    if (fd < 0)
        return NULL;
    FILE* file = fdopen(fd, "w+b");
    if (!file)
    {
        close(fd);
        remove(path);
    }
    return file;
#endif
}

static int track_file(chttpx_request_t* req, const char* path, const char* field_name, const char* original_name, const char* content_type,
                      size_t size)
{
    chttpx_file_t* files = cHTTPX_Alloc(req, sizeof(*files) * (req->files_count + 1));
    if (!files)
        return 0;
    if (req->files)
    {
        memcpy(files, req->files, sizeof(*files) * req->files_count);
        free(cHTTPX_Detach(req, req->files));
    }
    req->files = files;

    char* owned_path = strdup(path);
    if (!owned_path || cHTTPX_Defer(req, owned_path, remove_temporary_file) != 0)
    {
        free(owned_path);
        return 0;
    }

    chttpx_file_t* file = &files[req->files_count++];
    memset(file, 0, sizeof(*file));
    file->path = owned_path;
    file->field_name = cHTTPX_Strdup(req, field_name ? field_name : "file");
    file->original_name = cHTTPX_Strdup(req, original_name ? original_name : "upload.bin");
    file->content_type = cHTTPX_Strdup(req, content_type ? content_type : cHTTPX_CTYPE_OCTET);
    file->size = size;
    file->temporary = true;
    if (req->files_count == 1)
        snprintf(req->filename, sizeof(req->filename), "%s", path);
    return file->field_name && file->original_name && file->content_type;
}

static int add_form_value(chttpx_request_t* req, const char* name, size_t name_size, const char* value, size_t value_size, bool decode)
{
    chttpx_query_t* values = cHTTPX_Alloc(req, sizeof(*values) * (req->form_values_count + 1));
    if (!values)
        return 0;
    if (req->form_values)
    {
        memcpy(values, req->form_values, sizeof(*values) * req->form_values_count);
        free(cHTTPX_Detach(req, req->form_values));
    }
    req->form_values = values;

    char* owned_name = cHTTPX_Alloc(req, name_size + 1);
    char* owned_value = cHTTPX_Alloc(req, value_size + 1);
    if (!owned_name || !owned_value)
        return 0;
    memcpy(owned_name, name, name_size);
    owned_name[name_size] = '\0';
    memcpy(owned_value, value, value_size);
    owned_value[value_size] = '\0';

    if (decode)
    {
        char* decoded_name = cHTTPX_Alloc(req, name_size + 1);
        char* decoded_value = cHTTPX_Alloc(req, value_size + 1);
        if (!decoded_name || !decoded_value || !cHTTPX_UrlDecode(decoded_name, name_size + 1, owned_name, true) ||
            !cHTTPX_UrlDecode(decoded_value, value_size + 1, owned_value, true))
            return 0;
        owned_name = decoded_name;
        owned_value = decoded_value;
    }

    values[req->form_values_count].name = owned_name;
    values[req->form_values_count].value = owned_value;
    req->form_values_count++;
    return 1;
}

static void parse_urlencoded(chttpx_request_t* req)
{
    const char* cursor = (const char*)req->body;
    const char* end = cursor + req->body_size;
    while (cursor < end)
    {
        const char* ampersand = memchr(cursor, '&', (size_t)(end - cursor));
        const char* pair_end = ampersand ? ampersand : end;
        const char* equals = memchr(cursor, '=', (size_t)(pair_end - cursor));
        if (equals && !add_form_value(req, cursor, (size_t)(equals - cursor), equals + 1, (size_t)(pair_end - equals - 1), true))
        {
            req->_parse_status = cHTTPX_StatusBadRequest;
            return;
        }
        cursor = ampersand ? ampersand + 1 : end;
    }
}

static int header_attribute(const char* headers, size_t headers_size, const char* attribute, char* output, size_t output_size)
{
    const char* found = memmem_case(headers, headers_size, attribute, strlen(attribute));
    if (!found)
        return 0;
    found += strlen(attribute);
    const char* end = memchr(found, '"', (size_t)((headers + headers_size) - found));
    if (!end || (size_t)(end - found) >= output_size)
        return 0;
    memcpy(output, found, (size_t)(end - found));
    output[end - found] = '\0';
    return 1;
}

static int multipart_boundary(const chttpx_request_t* req, char* boundary, size_t boundary_size)
{
    if (!req)
        return 0;

    const char* cursor = req->content_type;
    while ((cursor = strchr(cursor, ';')) != NULL)
    {
        cursor++;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;

        if (strncasecmp(cursor, "boundary", 8) != 0)
            continue;

        const char* value = cursor + 8;
        while (*value == ' ' || *value == '\t')
            value++;
        if (*value != '=')
            continue;
        value++;
        while (*value == ' ' || *value == '\t')
            value++;

        bool quoted = *value == '"';
        if (quoted)
            value++;

        size_t size = quoted ? strcspn(value, "\"\r\n") : strcspn(value, "; \t\r\n");
        if (size == 0 || size > 200 || size + 3 > boundary_size || (quoted && value[size] != '"'))
            return 0;

        boundary[0] = '-';
        boundary[1] = '-';
        memcpy(boundary + 2, value, size);
        boundary[size + 2] = '\0';
        return 1;
    }

    return 0;
}

static void parse_part_headers(const char* headers, size_t headers_size, char* name, size_t name_size, char* filename, size_t filename_size,
                               int* has_filename, char* content_type, size_t content_type_size)
{
    name[0] = '\0';
    filename[0] = '\0';
    snprintf(content_type, content_type_size, "%s", cHTTPX_CTYPE_OCTET);

    header_attribute(headers, headers_size, "name=\"", name, name_size);
    *has_filename = header_attribute(headers, headers_size, "filename=\"", filename, filename_size);

    const char* type = memmem_case(headers, headers_size, "Content-Type:", 13);
    if (!type)
        return;

    type += 13;
    while (type < headers + headers_size && (*type == ' ' || *type == '\t'))
        type++;

    const char* type_end = chttpx_memmem(type, (size_t)((headers + headers_size) - type), "\r\n", 2);
    size_t size = type_end ? (size_t)(type_end - type) : (size_t)((headers + headers_size) - type);
    if (size >= content_type_size)
        size = content_type_size - 1;
    memcpy(content_type, type, size);
    content_type[size] = '\0';
}

static void parse_multipart_buffered(chttpx_request_t* req)
{
    char boundary[204];
    if (!multipart_boundary(req, boundary, sizeof(boundary)))
    {
        req->_parse_status = cHTTPX_StatusBadRequest;
        return;
    }

    size_t boundary_size = strlen(boundary);
    const unsigned char* body = req->body;
    const unsigned char* end = body + req->body_size;
    const unsigned char* part = chttpx_memmem(body, req->body_size, boundary, boundary_size);

    while (part)
    {
        part += boundary_size;
        if (part + 2 <= end && part[0] == '-' && part[1] == '-')
            return;
        if (part + 2 > end || part[0] != '\r' || part[1] != '\n')
            goto bad_request;
        part += 2;

        const unsigned char* headers_end = chttpx_memmem(part, (size_t)(end - part), "\r\n\r\n", 4);
        if (!headers_end)
            goto bad_request;

        size_t headers_size = (size_t)(headers_end - part);
        const unsigned char* data = headers_end + 4;
        const unsigned char* next = chttpx_memmem(data, (size_t)(end - data), boundary, boundary_size);
        if (!next)
            goto bad_request;
        if (next < data + 2 || next[-2] != '\r' || next[-1] != '\n')
            goto bad_request;

        // cppcheck-suppress nullPointerArithmeticRedundantCheck
        size_t next_offset = (size_t)(next - data);
        if (next_offset < 2)
            goto bad_request;
        size_t data_size = next_offset - 2;
        char name[256], filename[512], content_type[512];
        int has_filename = 0;
        parse_part_headers((const char*)part, headers_size, name, sizeof(name), filename, sizeof(filename), &has_filename, content_type,
                           sizeof(content_type));
        if (!name[0])
            goto bad_request;

        if (has_filename)
        {
            char path[512];
            FILE* file = create_temporary_file(path, sizeof(path));
            if (!file || fwrite(data, 1, data_size, file) != data_size)
            {
                if (file)
                    fclose(file);
                req->_parse_status = cHTTPX_StatusInternalServerError;
                return;
            }
            fclose(file);
            if (!track_file(req, path, name, filename, content_type, data_size))
            {
                remove(path);
                req->_parse_status = cHTTPX_StatusInternalServerError;
                return;
            }
        }
        else if (!add_form_value(req, name, strlen(name), (const char*)data, data_size, false))
        {
            req->_parse_status = cHTTPX_StatusInternalServerError;
            return;
        }
        part = next;
    }
    return;

bad_request:
    req->_parse_status = cHTTPX_StatusBadRequest;
}

static int read_line(FILE* stream, char* line, size_t capacity)
{
    size_t size = 0;
    int ch;
    while ((ch = fgetc(stream)) != EOF)
    {
        if (size + 1 >= capacity)
            return 0;
        line[size++] = (char)ch;
        if (ch == '\n')
            break;
    }
    if (!size)
        return 0;
    line[size] = '\0';
    return 1;
}

static int read_stream_headers(FILE* stream, char* headers, size_t capacity, size_t* headers_size)
{
    *headers_size = 0;
    for (;;)
    {
        char line[MULTIPART_LINE_LIMIT];
        if (!read_line(stream, line, sizeof(line)))
            return 0;
        if (strcmp(line, "\r\n") == 0)
            return 1;

        size_t size = strlen(line);
        if (size > capacity - *headers_size - 1)
            return 0;
        memcpy(headers + *headers_size, line, size);
        *headers_size += size;
        headers[*headers_size] = '\0';
    }
}

static int multipart_copy_part(FILE* stream, const char* boundary, FILE* output, unsigned char** value, size_t* value_size, size_t value_limit,
                               int* final_boundary)
{
    char next_marker[256];
    char final_marker[256];
    int next_len = snprintf(next_marker, sizeof(next_marker), "\r\n%s\r\n", boundary);
    int final_len = snprintf(final_marker, sizeof(final_marker), "\r\n%s--", boundary);
    if (next_len <= 0 || final_len <= 0 || (size_t)next_len >= sizeof(next_marker) || (size_t)final_len >= sizeof(final_marker))
        return 0;

    size_t marker_keep = strlen(boundary) + 6;
    size_t capacity = FILE_BUFFER + marker_keep;
    unsigned char* buffer = malloc(capacity);
    if (!buffer)
        return 0;

    unsigned char* form = NULL;
    size_t form_size = 0;
    size_t form_capacity = 0;
    size_t used = 0;
    int ok = 0;

    for (;;)
    {
        if (used < capacity)
            used += fread(buffer + used, 1, capacity - used, stream);

        unsigned char* next = chttpx_memmem(buffer, used, next_marker, (size_t)next_len);
        unsigned char* final = chttpx_memmem(buffer, used, final_marker, (size_t)final_len);
        unsigned char* marker = NULL;
        size_t marker_size = 0;

        if (next && (!final || next < final))
        {
            marker = next;
            marker_size = (size_t)next_len;
            *final_boundary = 0;
        }
        else if (final)
        {
            marker = final;
            marker_size = (size_t)final_len;
            *final_boundary = 1;
        }

        if (marker)
        {
            size_t data_size = (size_t)(marker - buffer);
            if (output)
            {
                if (data_size && fwrite(buffer, 1, data_size, output) != data_size)
                    goto done;
            }
            else if (!append_part_value(&form, &form_size, &form_capacity, buffer, data_size, value_limit))
                goto done;

            size_t consumed = data_size + marker_size;
            size_t unread = used - consumed;
            if (unread && fseek(stream, -(long)unread, SEEK_CUR) != 0)
                goto done;
            ok = 1;
            break;
        }

        if (feof(stream))
            break;
        if (ferror(stream))
            break;

        if (used <= marker_keep)
            continue;

        size_t flush = used - marker_keep;
        if (output)
        {
            if (fwrite(buffer, 1, flush, output) != flush)
                goto done;
        }
        else if (!append_part_value(&form, &form_size, &form_capacity, buffer, flush, value_limit))
            goto done;

        memmove(buffer, buffer + flush, used - flush);
        used -= flush;
    }

done:
    free(buffer);
    if (ok && value)
    {
        if (!form)
        {
            form = malloc(1);
            if (!form)
                ok = 0;
            else
                form[0] = '\0';
        }
        *value = form;
        *value_size = form_size;
    }
    else
        free(form);
    return ok;
}

static void parse_multipart_stream(chttpx_request_t* req, FILE* stream)
{
    chttpx_serv_t* server = req ? req->_server : NULL;
    char boundary[204];
    if (!multipart_boundary(req, boundary, sizeof(boundary)) || fseek(stream, 0, SEEK_SET) != 0)
        goto bad_request;

    char line[MULTIPART_LINE_LIMIT];
    if (!read_line(stream, line, sizeof(line)))
        goto bad_request;

    char expected[208];
    if (snprintf(expected, sizeof(expected), "%s\r\n", boundary) >= (int)sizeof(expected) || strcmp(line, expected) != 0)
        goto bad_request;

    size_t part_count = 0;
    size_t form_bytes = 0;

    for (;;)
    {
        if (++part_count > MULTIPART_MAX_PARTS)
        {
            req->_parse_status = cHTTPX_StatusPayloadTooLarge;
            return;
        }
        char headers[MULTIPART_LINE_LIMIT];
        size_t headers_size = 0;
        if (!read_stream_headers(stream, headers, sizeof(headers), &headers_size))
            goto bad_request;

        char name[256], filename[512], content_type[512];
        int has_filename = 0;
        parse_part_headers(headers, headers_size, name, sizeof(name), filename, sizeof(filename), &has_filename, content_type,
                           sizeof(content_type));
        if (!name[0])
            goto bad_request;

        int final_boundary = 0;
        if (has_filename)
        {
            char path[512];
            FILE* file = create_temporary_file(path, sizeof(path));
            if (!file)
                goto internal_error;

            long start = ftell(file);
            if (start < 0 || !multipart_copy_part(stream, boundary, file, NULL, NULL, 0, &final_boundary))
            {
                fclose(file);
                remove(path);
                goto bad_request;
            }
            long end = ftell(file);
            if (end < start)
            {
                fclose(file);
                remove(path);
                goto internal_error;
            }
            fclose(file);

            if (!track_file(req, path, name, filename, content_type, (size_t)(end - start)))
            {
                remove(path);
                goto internal_error;
            }
        }
        else
        {
            size_t value_limit = MULTIPART_FORM_VALUE_LIMIT;
            if (server && server->max_body_size < value_limit)
                value_limit = server->max_body_size;
            if (form_bytes >= value_limit)
            {
                req->_parse_status = cHTTPX_StatusPayloadTooLarge;
                return;
            }
            value_limit -= form_bytes;

            unsigned char* value = NULL;
            size_t value_size = 0;
            if (!multipart_copy_part(stream, boundary, NULL, &value, &value_size, value_limit, &final_boundary))
                goto bad_request;
            int added = add_form_value(req, name, strlen(name), (const char*)value, value_size, false);
            free(value);
            if (!added)
                goto internal_error;
            form_bytes += value_size;
        }

        if (final_boundary)
            return;
    }

bad_request:
    req->_parse_status = cHTTPX_StatusBadRequest;
    return;

internal_error:
    req->_parse_status = cHTTPX_StatusInternalServerError;
}

static void save_raw_upload_stream(chttpx_request_t* req, FILE* stream)
{
    if (!req || !stream || fseek(stream, 0, SEEK_SET) != 0)
    {
        if (req)
            req->_parse_status = cHTTPX_StatusInternalServerError;
        return;
    }

    char path[512];
    FILE* file = create_temporary_file(path, sizeof(path));
    if (!file)
    {
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return;
    }

    size_t total = 0;
    unsigned char chunk[FILE_BUFFER];
    size_t received = 0;
    while ((received = fread(chunk, 1, sizeof(chunk), stream)) > 0)
    {
        if (fwrite(chunk, 1, received, file) != received)
            goto internal_error;
        if (total > SIZE_MAX - received)
            goto internal_error;
        total += received;
    }

    if (ferror(stream) || total != req->content_length)
        goto internal_error;

    fclose(file);
    if (!track_file(req, path, "file", "upload.bin", req->content_type, total))
    {
        remove(path);
        req->_parse_status = cHTTPX_StatusInternalServerError;
    }
    return;

internal_error:
    fclose(file);
    remove(path);
    req->_parse_status = cHTTPX_StatusInternalServerError;
}

static void save_raw_upload(chttpx_request_t* req, char* initial_buffer, size_t initial_len)
{
    char path[512];
    FILE* file = create_temporary_file(path, sizeof(path));
    if (!file)
    {
        req->_parse_status = cHTTPX_StatusInternalServerError;
        return;
    }

    const char* body = chttpx_memmem(initial_buffer, initial_len, "\r\n\r\n", 4);
    if (!body)
        goto bad_request;
    body += 4;

    size_t buffered = initial_len - (size_t)(body - initial_buffer);
    if (buffered > req->content_length)
        buffered = req->content_length;
    if (fwrite(body, 1, buffered, file) != buffered)
        goto bad_request;

    size_t total = buffered;
    unsigned char chunk[FILE_BUFFER];
    while (total < req->content_length)
    {
        size_t wanted = req->content_length - total;
        if (wanted > sizeof(chunk))
            wanted = sizeof(chunk);
        int received = recv(req->client_fd, (char*)chunk, wanted, 0);
        if (received <= 0 || fwrite(chunk, 1, (size_t)received, file) != (size_t)received)
            goto bad_request;
        total += (size_t)received;
    }

    fclose(file);
    if (!track_file(req, path, "file", "upload.bin", req->content_type, total))
    {
        remove(path);
        req->_parse_status = cHTTPX_StatusInternalServerError;
    }
    return;

bad_request:
    fclose(file);
    remove(path);
    req->_parse_status = cHTTPX_StatusBadRequest;
}

void _parse_media(chttpx_request_t* req, char* buffer, size_t buffer_len)
{
    if (!req || !req->content_type[0] || req->_parse_status)
        return;

    if (cHTTPX_MimeMatch(req->content_type, cHTTPX_CTYPE_MULTI))
    {
        if (req->_multipart_stream)
            parse_multipart_stream(req, (FILE*)req->_multipart_stream);
        else if (req->body)
            parse_multipart_buffered(req);
        else
            req->_parse_status = cHTTPX_StatusBadRequest;
    }
    else if (cHTTPX_MimeMatch(req->content_type, cHTTPX_CTYPE_FORM))
        parse_urlencoded(req);
    else if (req->content_length > 0 && !cHTTPX_MimeMatch(req->content_type, cHTTPX_CTYPE_JSON) &&
             !cHTTPX_MimeMatch(req->content_type, "text/*"))
    {
        if (req->_multipart_stream)
            save_raw_upload_stream(req, (FILE*)req->_multipart_stream);
        else if (req->body)
        {
            char path[512];
            FILE* file = create_temporary_file(path, sizeof(path));
            if (!file || fwrite(req->body, 1, req->body_size, file) != req->body_size)
            {
                if (file)
                    fclose(file);
                req->_parse_status = cHTTPX_StatusInternalServerError;
                return;
            }
            fclose(file);
            if (!track_file(req, path, "file", "upload.bin", req->content_type, req->body_size))
                req->_parse_status = cHTTPX_StatusInternalServerError;
        }
        else
            save_raw_upload(req, buffer, buffer_len);
    }
}

const chttpx_file_t* cHTTPX_RequestFile(chttpx_request_t* req)
{
    return req && req->files_count ? &req->files[0] : NULL;
}

const chttpx_file_t* cHTTPX_FormFile(chttpx_request_t* req, const char* name)
{
    if (!req || !name)
        return NULL;
    for (size_t i = 0; i < req->files_count; i++)
    {
        if (req->files[i].field_name && strcmp(req->files[i].field_name, name) == 0)
            return &req->files[i];
    }
    return NULL;
}

const char* cHTTPX_FormValue(chttpx_request_t* req, const char* name)
{
    if (!req || !name)
        return NULL;
    for (size_t i = 0; i < req->form_values_count; i++)
    {
        if (strcmp(req->form_values[i].name, name) == 0)
            return req->form_values[i].value;
    }
    return NULL;
}

int cHTTPX_FileDetach(chttpx_request_t* req, const chttpx_file_t* file)
{
    if (!req || !file || !file->path || !cHTTPX_Detach(req, (void*)file->path))
        return 0;

    /*
     * Keep ownership of the path string request-scoped after transferring
     * ownership of the file itself to the caller.
     */
    if (cHTTPX_Defer(req, (void*)file->path, free) != 0)
    {
        if (cHTTPX_Defer(req, (void*)file->path, remove_temporary_file) != 0)
        {
            remove(file->path);
            free((void*)file->path);
            ((chttpx_file_t*)file)->path = NULL;
        }
        return 0;
    }

    ((chttpx_file_t*)file)->temporary = false;
    return 1;
}

int cHTTPX_FileKeep(chttpx_request_t* req)
{
    const chttpx_file_t* file = cHTTPX_RequestFile(req);
    return file ? cHTTPX_FileDetach(req, file) : 0;
}

bool cHTTPX_MimeMatch(const char* mime, const char* pattern)
{
    if (!mime || !pattern)
        return false;
    const char* semicolon = strchr(mime, ';');
    size_t mime_size = semicolon ? (size_t)(semicolon - mime) : strlen(mime);
    size_t pattern_size = strlen(pattern);
    if (pattern_size >= 2 && pattern[pattern_size - 1] == '*' && pattern[pattern_size - 2] == '/')
        return mime_size >= pattern_size - 1 && strncasecmp(mime, pattern, pattern_size - 1) == 0;
    return mime_size == pattern_size && strncasecmp(mime, pattern, mime_size) == 0;
}

bool cHTTPX_MimeIsImage(const char* mime)
{
    return cHTTPX_MimeMatch(mime, "image/*");
}

bool cHTTPX_MimeIsVideo(const char* mime)
{
    return cHTTPX_MimeMatch(mime, "video/*");
}

bool cHTTPX_MimeIsAudio(const char* mime)
{
    return cHTTPX_MimeMatch(mime, "audio/*");
}
