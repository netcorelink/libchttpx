/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libuhttpx.c` for details.
 */

#ifndef LIBUHTTPX_H
#define LIBUHTTPX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define BUFFER_SIZE 1024

// HTTP Content Types
// HTML document. Use this for web pages, responses that browsers render as HTML.
#define uHTTPX_CTYPE_HTML  "text/html"
// JSON data. Use this for API responses or requests containing structured data.
#define uHTTPX_CTYPE_JSON  "application/json"
// PNG image. Use this when sending images in PNG format.
#define uHTTPX_CTYPE_PNG   "image/png"
// JPEG image. Use this when sending images in JPEG format.
#define uHTTPX_CTYPE_JPEG  "image/jpeg"
// Plain text. Use for simple text responses, logs, or messages without formatting.
#define uHTTPX_CTYPE_TEXT  "text/plain"
// CSS stylesheet. Use when returning CSS files for web pages.
#define uHTTPX_CTYPE_CSS   "text/css"
// JavaScript script. Use when returning JS files for web pages or dynamic scripts.
#define uHTTPX_CTYPE_JS    "application/javascript"

// REQuest
typedef struct {
    const char *method;
    const char *path;
    const char *query;
    const char *body;
} httpx_request_t;

// RESponse
typedef struct {
    int status;
    const char *content_type;
    const char *body;
} httpx_response_t;

typedef httpx_response_t (*httpx_handler_t)(httpx_request_t *req);

// Route
typedef struct {
    const char *method;
    const char *path;
    httpx_handler_t handler;
} route_t;

typedef enum {
    FIELD_STRING,
    FIELD_INT,
    FIELD_BOOL
} FieldType;

typedef struct {
    const char *name;
    FieldType type;
    int required;
    size_t min_length;
    size_t max_length;
    void *target;
} uHTTPX_FieldValidation;

/**
 * Initialize the HTTP server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * @param max_routes Maximum number of routes that can be registered.
 * This function must be called before registering routes or starting the server.
 */
void uHTTPX_Init(int port, int max_routes);

/**
 * Register a route handler for a specific HTTP method and path.
 * @param method HTTP method string, e.g., "GET", "POST".
 * @param path URL path to match, e.g., "/users".
 * @param handler Function pointer to handle the request. The handler should return httpx_response_t.
 * This allows the server to call the appropriate function when a matching request is received.
 */
void uHTTPX_Route(const char *method, const char *path, httpx_handler_t handler);

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
 * This function reads the request, parses it, calls the matching route handler,
 * and sends the response back to the client.
 */
void uHTTPX_Handle(int client_fd);

/**
 * Start the server loop to listen for incoming connections.
 * This function blocks indefinitely, accepting new client connections
 * and dispatching them to uHTTPX_Handle.
 */
void uHTTPX_Listen(void);

/**
 * Parse a JSON body and validate fields according to the provided definitions.
 * @param body JSON string to parse.
 * @param fields Array of field validation definitions (uHTTPX_FieldValidation).
 * @param field_count Number of fields in the array.
 * @param error_msg Output pointer to a string describing the first validation error, if any.
 * @return 1 if parsing and validation succeed, 0 if there is an error.
 * This function automatically checks required fields, string length, boolean types, etc.
 */
int uHTTPX_Parse(httpx_request_t *req, uHTTPX_FieldValidation *fields, size_t field_count, char **error_msg);

#define uHTTPX_FIELD_STRING(name, required, min_length, max_length, ptr) (uHTTPX_FieldValidation){name, FIELD_STRING, required, min_length, max_length, ptr}
#define uHTTPX_FIELD_INT(name, required, ptr) (uHTTPX_FieldValidation){name, FIELD_INT required, 0, 0, ptr}
#define uHTTPX_FIELD_BOOL(name, required, ptr) (uHTTPX_FieldValidation){name, FIELD_BOOL, required, 0, 0, ptr}

// HTTP statuses
// 1xx
#define uHTTPX_StatusContinue 100
#define uHTTPX_StatusSwitchingProtocols 101
#define uHTTPX_StatusProcessing 102
// 2xx
#define uHTTPX_StatusOK 200
#define uHTTPX_StatusCreated 201
#define uHTTPX_StatusAccepted 202
#define uHTTPX_StatusNonAuthoritativeInformation 203
#define uHTTPX_StatusNoContent 204
#define uHTTPX_StatusResetContent 205
// 3xx
#define uHTTPX_StatusNotModified 304
#define uHTTPX_StatusUseProxy 305
#define uHTTPX_StatusTemporaryRedirect 307
// 4xx
#define uHTTPX_StatusBadRequest 400
#define uHTTPX_StatusUnauthorized 401
#define uHTTPX_StatusForbidden 403
#define uHTTPX_StatusNotFound 404
#define uHTTPX_StatusProxyAuthenticationRequired 407
#define uHTTPX_StatusRequestTimeout 408
#define uHTTPX_StatusConflict 409
#define uHTTPX_StatusPayloadTooLarge 413
#define uHTTPX_StatusURITooLong 414
#define uHTTPX_StatusTooManyRequests 429
// 5xx
#define uHTTPX_StatusInternalServerError 500
#define uHTTPX_StatusBadGateway 502
#define uHTTPX_StatusGatewayTimeout 504

#ifdef __cplusplus
}
#endif

#endif
