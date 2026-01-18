/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef RESPONSE_H
#define RESPONSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "request.h"

// RESponse
typedef struct {
    /* Response status code */
    int status;

    /* Response content type */
    const char *content_type;

    /* Response body */
    const unsigned char *body;
    /* Response body size */
    size_t body_size;
} chttpx_response_t;

typedef chttpx_response_t (*chttpx_handler_t)(chttpx_request_t *req);

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
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
chttpx_response_t cHTTPX_ResJson(uint16_t status, const char *fmt, ...);

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
chttpx_response_t cHTTPX_ResBinary(uint16_t status, const char *content_type, const unsigned char *body, size_t body_size);

/**
 * Create a binary HTTP response from FILE.
 *
 * @param status HTTP status code (e.g. 200, 400, 404)
 * @param content_type MIME type of the response (e.g. "image/png")
 * @param path Path from return file
 * @return Initialized chttpx_response_t
 */
chttpx_response_t cHTTPX_ResFile(uint16_t status, const char *content_type, const char *path);

#ifdef __cplusplus
extern }
#endif

#endif