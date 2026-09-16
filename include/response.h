/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef RESPONSE_H
#define RESPONSE_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "request.h"

#include <time.h>

    // RESponse
    typedef enum
    {
        CHTTPX_BODY_BORROWED = 0,
        CHTTPX_BODY_OWNED = 1
    } chttpx_body_ownership_t;

    typedef struct chttpx_response
    {
        /* Response status code */
        int status;

        /* Response content type */
        const char* content_type;

        /* Headers in REQuest */
        chttpx_header_t headers[MAX_HEADERS];
        size_t headers_count;

        /* Response body */
        const unsigned char* body;
        /* Response body size */
        size_t body_size;

        chttpx_body_ownership_t body_ownership;

        /* Times for logging */
        struct timespec start_ts;
        struct timespec end_ts;
    } chttpx_response_t;

    /* handler */
    typedef void (*chttpx_handler_t)(chttpx_request_t* req, chttpx_response_t* res);

    /**
     * Handle a single client connection.
     * @param arg The file descriptor of the accepted client socket.
     * This function reads the request, parses it, calls the matching route handler,
     * and sends the response back to the client.
     */
    void* chttpx_handle(void* arg);

    /**
     * Create a JSON HTTP response with formatted content.
     *
     * Formats a JSON response body using printf-style arguments,
     * allocates memory for the response body, and returns a
     * fully initialized chttpx_response_t structure.
     *
     * @param status HTTP status code (e.g. 200, 400, 404).
     * @param fmt    printf-style format string for the JSON body.
     * @param ...    Format arguments.
     */
    chttpx_response_t cHTTPX_ResJson(uint16_t status, const char* fmt, ...);

    /**
     * Creates an HTTP response with HTML content.
     *
     * This function generates a chttpx_response_t structure with the specified
     * HTTP status code and HTML body. The body is created using a printf-style
     * format string (fmt) and additional arguments. Memory for the body is
     * dynamically allocated and must be freed after sending the response.
     *
     * @param status HTTP status code (e.g., 200, 404, 500).
     * @param fmt Format string containing the HTML content (like printf).
     * @param ... Arguments corresponding to the format string.
     */
    chttpx_response_t cHTTPX_ResHtml(uint16_t status, const char* fmt, ...);

    /**
     * Create a binary HTTP response (file, media, etc.).
     *
     * Allocates memory for the response body and returns a fully initialized
     * chttpx_response_t structure.
     *
     * @param status HTTP status code (e.g. 200, 400, 404)
     * @param content_type MIME type of the response (e.g. "image/png")
     * @param body Pointer to the data buffer
     * @param body_size Size of the data buffer in bytes
     * @return Initialized chttpx_response_t
     */
    chttpx_response_t cHTTPX_ResBinary(uint16_t status, const char* content_type, const unsigned char* body, size_t body_size);

    /**
     * Create a binary HTTP response from FILE.
     *
     * @param status HTTP status code (e.g. 200, 400, 404)
     * @param content_type MIME type of the response (e.g. "image/png")
     * @param path Path from return file
     * @return Initialized chttpx_response_t
     */
    chttpx_response_t cHTTPX_ResFile(uint16_t status, const char* content_type, const char* path);

    /** Create an error response with the given HTTP status and message. */
    chttpx_response_t cHTTPX_ResError(uint16_t status, const char* message);

    /** Create a plain-text response with the given HTTP status and message. */
    chttpx_response_t cHTTPX_ResMessage(uint16_t status, const char* message);

    /** Create an HTTP 204 No Content response. */
    chttpx_response_t cHTTPX_ResNoContent(void);

    /** Release all resources owned by a response. The pointer may be NULL. */
    void cHTTPX_ResponseCleanup(chttpx_response_t* res);

    /** Return the standard reason phrase for an HTTP status code. */
    const char* cHTTPX_StatusReason(uint16_t status);

    /**
     * Send an entire buffer, retrying partial socket writes.
     *
     * @param fd   Connected socket descriptor.
     * @param data Buffer
     * to send.
     * @param size Buffer size in bytes.
     * @return CHTTPX_OK on success, otherwise a negative error code.
     */
    int cHTTPX_SendAll(chttpx_socket_t fd, const void* data, size_t size);

#ifdef __cplusplus
    extern
}
#endif

#endif
