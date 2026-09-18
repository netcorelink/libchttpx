/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef MEDIA_H
#define MEDIA_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cHTTPX_http.h"
#include "cHTTPX_request.h"

#define FILE_BUFFER 65536

    typedef struct
    {
        const char* ctype;
        const char* ext;
    } content_type_map_t;

    /* Parse media in request */
    void _parse_media(chttpx_request_t* req, char* buffer, size_t buffer_len);

    /** Return the first uploaded file, or NULL when the request has no file. */
    const chttpx_file_t* cHTTPX_RequestFile(chttpx_request_t* req);

    /** Return an uploaded multipart file by field name, or NULL when not found. */
    const chttpx_file_t* cHTTPX_FormFile(chttpx_request_t* req, const char* name);

    /** Return a multipart text field by name, or NULL when not found. */
    const char* cHTTPX_FormValue(chttpx_request_t* req, const char* name);

    /** Keep all uploaded temporary files after request cleanup. Returns 1 on success. */
    int cHTTPX_FileKeep(chttpx_request_t* req);

    /** Detach one uploaded file from automatic cleanup. Returns 1 on success. */
    int cHTTPX_FileDetach(chttpx_request_t* req, const chttpx_file_t* file);

    /** Match a MIME type against an exact type or a family wildcard pattern. */
    bool cHTTPX_MimeMatch(const char* mime, const char* pattern);

    /** Return true when the MIME type belongs to the image family. */
    bool cHTTPX_MimeIsImage(const char* mime);

    /** Return true when the MIME type belongs to the video family. */
    bool cHTTPX_MimeIsVideo(const char* mime);

    /** Return true when the MIME type belongs to the audio family. */
    bool cHTTPX_MimeIsAudio(const char* mime);

#ifdef __cplusplus
}
#endif

#endif
