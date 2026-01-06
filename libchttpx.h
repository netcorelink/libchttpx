/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef LIBCHTTPX_H
#define LIBCHTTPX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define BUFFER_SIZE 1024

// HTTP Content Types
// HTML document. Use this for web pages, responses that browsers render as HTML.
#define cHTTPX_CTYPE_HTML  "text/html"
// JSON data. Use this for API responses or requests containing structured data.
#define cHTTPX_CTYPE_JSON  "application/json"
// PNG image. Use this when sending images in PNG format.
#define cHTTPX_CTYPE_PNG   "image/png"
// JPEG image. Use this when sending images in JPEG format.
#define cHTTPX_CTYPE_JPEG  "image/jpeg"
// Plain text. Use for simple text responses, logs, or messages without formatting.
#define cHTTPX_CTYPE_TEXT  "text/plain"
// CSS stylesheet. Use when returning CSS files for web pages.
#define cHTTPX_CTYPE_CSS   "text/css"
// JavaScript script. Use when returning JS files for web pages or dynamic scripts.
#define cHTTPX_CTYPE_JS    "application/javascript"

#define MAX_ROUTE_PARAMS 8
#define MAX_PARAM_NAME 128
#define MAX_PARAM_VALUE 255

typedef struct {
    char name[MAX_PARAM_NAME];
    char value[MAX_PARAM_VALUE];
} route_param_t;

// REQuest
typedef struct {
    const char *method;
    const char *path;
    const char *body;
    const char *query;

    route_param_t params[MAX_ROUTE_PARAMS];
    int param_count;
} chttpx_request_t;

// RESponse
typedef struct {
    int status;
    const char *content_type;
    const char *body;
} chttpx_response_t;

typedef chttpx_response_t (*chttpx_handler_t)(chttpx_request_t *req);

// Route
typedef struct {
    const char *method;
    const char *path;
    chttpx_handler_t handler;
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
} cHTTPX_FieldValidation;

/**
 * Initialize the HTTP server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * @param max_routes Maximum number of routes that can be registered.
 * This function must be called before registering routes or starting the server.
 */
void cHTTPX_Init(int port, int max_routes);

/**
 * Register a route handler for a specific HTTP method and path.
 * @param method HTTP method string, e.g., "GET", "POST".
 * @param path URL path to match, e.g., "/users".
 * @param handler Function pointer to handle the request. The handler should return httpx_response_t.
 * This allows the server to call the appropriate function when a matching request is received.
 */
void cHTTPX_Route(const char *method, const char *path, chttpx_handler_t handler);

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
 * This function reads the request, parses it, calls the matching route handler,
 * and sends the response back to the client.
 */
void cHTTPX_Handle(int client_fd);

/**
 * Start the server loop to listen for incoming connections.
 * This function blocks indefinitely, accepting new client connections
 * and dispatching them to cHTTPX_Handle.
 */
void cHTTPX_Listen(void);

/**
 * Parse a JSON body and validate fields according to the provided definitions.
 * @param body JSON string to parse.
 * @param fields Array of field validation definitions (cHTTPX_FieldValidation).
 * @param field_count Number of fields in the array.
 * @param error_msg Output pointer to a string describing the first validation error, if any.
 * @return 1 if parsing and validation succeed, 0 if there is an error.
 * This function automatically checks required fields, string length, boolean types, etc.
 */
int cHTTPX_Parse(chttpx_request_t *req, cHTTPX_FieldValidation *fields, size_t field_count, char **error_msg);

/**
 * Get a route parameter value by its name.
 * @param req  Pointer to the current HTTP request structure.
 * @param name Name of the route parameter (e.g., "uuid").
 *
 * @return Pointer to the parameter value string if found, or NULL if the parameter does not exist.
 */
const char* cHTTPX_Param(chttpx_request_t *req, const char *name);

#define cHTTPX_FIELD_STRING(name, required, min_length, max_length, ptr) (cHTTPX_FieldValidation){name, FIELD_STRING, required, min_length, max_length, ptr}
#define cHTTPX_FIELD_INT(name, required, ptr) (cHTTPX_FieldValidation){name, FIELD_INT required, 0, 0, ptr}
#define cHTTPX_FIELD_BOOL(name, required, ptr) (cHTTPX_FieldValidation){name, FIELD_BOOL, required, 0, 0, ptr}

// HTTP statuses
// 1xx
#define cHTTPX_StatusContinue 100
#define cHTTPX_StatusSwitchingProtocols 101
#define cHTTPX_StatusProcessing 102
#define cHTTPX_StatusEarlyHints 103
// 2xx
#define cHTTPX_StatusOK 200
#define cHTTPX_StatusCreated 201
#define cHTTPX_StatusAccepted 202
#define cHTTPX_StatusNonAuthoritativeInformation 203
#define cHTTPX_StatusNoContent 204
#define cHTTPX_StatusResetContent 205
#define cHTTPX_StatusPartialContent 206
#define cHTTPX_StatusMultiStatus 207
#define cHTTPX_StatusAlreadyReported 208
#define cHTTPX_StatusIMUsed 226
// 3xx
#define cHTTPX_StatusMultipleChoices 300
#define cHTTPX_StatusMovedPermanently 301
#define cHTTPX_StatusFound 302
#define cHTTPX_StatusSeeOther 303
#define cHTTPX_StatusNotModified 304
#define cHTTPX_StatusUseProxy 305
#define cHTTPX_StatusNone 306
#define cHTTPX_StatusTemporaryRedirect 307
#define cHTTPX_StatusPermanentRedirect 308
// 4xx
#define cHTTPX_StatusBadRequest 400
#define cHTTPX_StatusUnauthorized 401
#define cHTTPX_StatusPaymentRequired 402
#define cHTTPX_StatusForbidden 403
#define cHTTPX_StatusNotFound 404
#define cHTTPX_StatusMethodNotAllowed 405
#define cHTTPX_StatusNotAcceptable 406
#define cHTTPX_StatusProxyAuthenticationRequired 407
#define cHTTPX_StatusRequestTimeout 408
#define cHTTPX_StatusConflict 409
#define cHTTPX_StatusGone 410
#define cHTTPX_StatusLengthRequired 411
#define cHTTPX_StatusPreconditionFailed 412
#define cHTTPX_StatusPayloadTooLarge 413
#define cHTTPX_StatusURITooLong 414
#define cHTTPX_StatusUnsupportedMediaType 415
#define cHTTPX_StatusRangeNotSatisfiable 416
#define cHTTPX_StatusExpectationFailed 417
#define cHTTPX_StatusImATeapot 418
#define cHTTPX_StatusAuthenticationTimeout 419
#define cHTTPX_StatusMisdirectedRequest 421
#define cHTTPX_StatusUnprocessableEntity 422
#define cHTTPX_StatusLocked 423
#define cHTTPX_StatusFailedDependency 424
#define cHTTPX_StatusTooEarly 425
#define cHTTPX_StatusUpgradeRequired 426
#define cHTTPX_StatusPreconditionRequired 428
#define cHTTPX_StatusTooManyRequests 429
#define cHTTPX_StatusRequestHeaderFieldsTooLarge 431
#define cHTTPX_StatusRetryWith 449
#define cHTTPX_StatusUnavailableForLegalReasons 451
#define cHTTPX_StatusClientClosedRequest 499
// 5xx
#define cHTTPX_StatusInternalServerError 500
#define cHTTPX_StatusNotImplemented 501
#define cHTTPX_StatusBadGateway 502
#define cHTTPX_StatusServiceUnavailable 503
#define cHTTPX_StatusGatewayTimeout 504
#define cHTTPX_StatusHTTPVersionNotSupported 505
#define cHTTPX_StatusVariantAlsoNegotiates 506
#define cHTTPX_StatusInsufficientStorage 507
#define cHTTPX_StatusLoopDetected 508
#define cHTTPX_StatusBandwidthLimitExceeded 509
#define cHTTPX_StatusNotExtended 510
#define cHTTPX_StatusNetworkAuthenticationRequired 511
#define cHTTPX_StatusUnknownError 520
#define cHTTPX_StatusWebServerIsDown 521
#define cHTTPX_StatusConnectionTimedOut 522
#define cHTTPX_StatusOriginIsUnreachable 523
#define cHTTPX_StatusATimeoutOccurred 524
#define cHTTPX_StatusSSLHandshakeFailed 525
#define cHTTPX_StatusInvalidSSLCertificate 526

#ifdef __cplusplus
}
#endif

#endif
