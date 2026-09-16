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

#include "headers.h"
#include "queries.h"
#include "serv.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CHTTPX_PLATFORM_POSIX
#include <unistd.h>
#endif

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
    const char* found = chttpx_memmem(headers, headers_size, attribute, strlen(attribute));
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

static void parse_multipart(chttpx_request_t* req)
{
    const char* boundary_value = strstr(req->content_type, "boundary=");
    if (!boundary_value)
    {
        req->_parse_status = cHTTPX_StatusBadRequest;
        return;
    }
    boundary_value += 9;
    if (*boundary_value == '"')
        boundary_value++;
    size_t boundary_value_size = strcspn(boundary_value, "\";\r\n");
    if (boundary_value_size == 0 || boundary_value_size > 200)
    {
        req->_parse_status = cHTTPX_StatusBadRequest;
        return;
    }
    char boundary[204];
    boundary[0] = '-';
    boundary[1] = '-';
    memcpy(boundary + 2, boundary_value, boundary_value_size);
    size_t boundary_size = boundary_value_size + 2;

    const unsigned char* body = req->body;
    const unsigned char* end = body + req->body_size;
    const unsigned char* part = chttpx_memmem(body, req->body_size, boundary, boundary_size);
    while (part)
    {
        part += boundary_size;
        if (part + 2 <= end && part[0] == '-' && part[1] == '-')
            break;
        if (part + 2 > end || part[0] != '\r' || part[1] != '\n')
        {
            req->_parse_status = cHTTPX_StatusBadRequest;
            return;
        }
        part += 2;
        const unsigned char* headers_end = chttpx_memmem(part, (size_t)(end - part), "\r\n\r\n", 4);
        if (!headers_end)
        {
            req->_parse_status = cHTTPX_StatusBadRequest;
            return;
        }
        size_t headers_size = (size_t)(headers_end - part);
        const unsigned char* data = headers_end + 4;
        const unsigned char* next = chttpx_memmem(data, (size_t)(end - data), boundary, boundary_size);
        if (!next || next < data + 2 || next[-2] != '\r' || next[-1] != '\n')
        {
            req->_parse_status = cHTTPX_StatusBadRequest;
            return;
        }
        size_t data_size = (size_t)((next - 2) - data);
        char name[256] = {0};
        char filename[512] = {0};
        char content_type[512] = cHTTPX_CTYPE_OCTET;
        header_attribute((const char*)part, headers_size, "name=\"", name, sizeof(name));
        int has_filename = header_attribute((const char*)part, headers_size, "filename=\"", filename, sizeof(filename));
        const char* type = memmem_case(part, headers_size, "Content-Type:", 13);
        if (type)
        {
            type += 13;
            while (*type == ' ')
                type++;
            const char* type_end = chttpx_memmem(type, (size_t)((const char*)headers_end - type), "\r\n", 2);
            size_t type_size = type_end ? (size_t)(type_end - type) : (size_t)((const char*)headers_end - type);
            if (type_size >= sizeof(content_type))
                type_size = sizeof(content_type) - 1;
            memcpy(content_type, type, type_size);
            content_type[type_size] = '\0';
        }

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
    if (strstr(req->content_type, cHTTPX_CTYPE_MULTI))
        parse_multipart(req);
    else if (strstr(req->content_type, cHTTPX_CTYPE_FORM))
        parse_urlencoded(req);
    else if (req->content_length > 0 && !strstr(req->content_type, cHTTPX_CTYPE_JSON) && !strstr(req->content_type, "text/"))
    {
        if (req->body)
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
    cHTTPX_Defer(req, (void*)file->path, free);
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
