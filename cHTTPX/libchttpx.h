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

#define BUFFER_SIZE 2048

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

#define MAX_HEADERS 64
#define MAX_HEADER_NAME 64
#define MAX_HEADER_VALUE 2048

typedef struct {
    char name[MAX_HEADER_NAME];
    char value[MAX_HEADER_VALUE];
} chttpx_header_t;

typedef struct {
    char name[MAX_PARAM_NAME];
    char value[MAX_PARAM_VALUE];
} chttpx_param_t;

typedef struct {
    char *name;
    char *value;
} chttpx_query_t;

// REQuest
typedef struct {
    char *method;
    char *path;
    char *body;

    /* Error request message */
    char error_msg[BUFFER_SIZE];

    /* Headers in REQuest */
    chttpx_header_t headers[MAX_HEADERS];
    size_t headers_count;
    
    /* Query params in URL
     * exmaple: ?name=netcorelink
     */
    chttpx_query_t *query;
    size_t query_count; 

    /* Params in URL 
     * exmaple: /{uuid}
     */
    chttpx_param_t params[MAX_ROUTE_PARAMS];
    size_t param_count;
} chttpx_request_t;

// RESponse
typedef struct {
    int status;
    const char *content_type;
    const char *body;
} chttpx_response_t;

typedef chttpx_response_t (*chttpx_handler_t)(chttpx_request_t *req);

typedef struct {
    const char *method;
    const char *path;
    chttpx_handler_t handler;
} route_t;

typedef enum {
    FIELD_STRING,
    FIELD_INT,
    FIELD_BOOL
} validation_t;

typedef struct {
    int enabled;
    const char **origins;
    size_t origins_count;
    const char *methods;
    const char *headers;
} chttpx_cors_t;

typedef struct {
    int port;
    int server_fd;

    size_t max_clients;

    /* Server timeout params */
    int read_timeout_sec;
    int write_timeout_sec;
    int idle_timeout_sec;

    /* Routes params */
    route_t *routes;
    size_t routes_count;
    size_t routes_capacity;

    /* Cors */
    chttpx_cors_t cors;
} chttpx_server_t;

/**
 * Initialize the HTTP server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * @param max_routes Maximum number of routes that can be registered.
 * This function must be called before registering routes or starting the server.
 */
int cHTTPX_Init(chttpx_server_t *serv_p, int port);

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
void cHTTPX_Listen();

typedef struct {
    const char *name;

    /* Type field str/int/bool */
    validation_t type;

    /* Required field */
    int required;

    /* Min/Max value string */
    size_t min_length;
    size_t max_length;

    /* Target value in struct */
    void *target;
} chttpx_validation_t;

/**
 * Parse a JSON body and validate fields according to the provided definitions.
 * @param body JSON string to parse.
 * @param fields Array of field validation definitions (cHTTPX_FieldValidation).
 * @param field_count Number of fields in the array.
 * @param error_msg Output pointer to a string describing the first validation error, if any.
 * @return 1 if parsing and validation succeed, 0 if there is an error.
 * This function automatically checks required fields, string length, boolean types, etc.
 */
int cHTTPX_Parse(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count);

/*
 * Validates an array of cHTTPX_FieldValidation structures.
 * This function ensures that required fields are present, string lengths are within limits,
 * and basic validation for integers and boolean fields is performed.
 */
int cHTTPX_Validate(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count);

/**
 * Get a request header by name.
 * @param req Pointer to the HTTP request.
 * @param name Header name (case-insensitive).
 * @return Pointer to header value if found, otherwise NULL.
 */
const char* cHTTPX_Header(chttpx_request_t *req, const char *name);

/**
 * Get a route parameter value by its name.
 * @param req  Pointer to the current HTTP request structure.
 * @param name Name of the route parameter (e.g., "uuid").
 *
 * @return Pointer to the parameter value string if found, or NULL if the parameter does not exist.
 */
const char* cHTTPX_Param(chttpx_request_t *req, const char *name);

/**
 * Get a query parameter value by name.
 *
 * Searches the parsed URL query parameters (e.g. ?name=value&age=10)
 * and returns the value associated with the given parameter name.
 * 
 * @param req   Pointer to the current HTTP request.
 * @param name  Name of the query parameter.
 * @return Pointer to the parameter value string if found, or NULL if not present.
 */
const char* cHTTPX_Query(chttpx_request_t *req, const char *name);

void cHTTPX_Cors(const char **origins, size_t origins_count, const char *methods, const char *headers);

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
chttpx_response_t cHTTPX_JsonResponse(int status, const char *fmt, ...);

#define chttpx_validation_str(name, required, min_length, max_length, ptr) (chttpx_validation_t){name, FIELD_STRING, required, min_length, max_length, ptr}
#define chttpx_validation_int(name, required, ptr) (chttpx_validation_t){name, FIELD_INT required, 0, 0, ptr}
#define chttpx_validation_bool(name, required, ptr) (chttpx_validation_t){name, FIELD_BOOL, required, 0, 0, ptr}

/* HTTP methods */
#define cHTTPX_MethodGet     "GET"
#define cHTTPX_MethodPost    "POST"
#define cHTTPX_MethodPut     "PUT"
#define cHTTPX_MethodPatch   "PATCH"
#define cHTTPX_MethodDelete  "DELETE"
#define cHTTPX_MethodOptions "OPTIONS"

/* HTTP statuses */
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
