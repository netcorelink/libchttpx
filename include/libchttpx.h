/**
 * Copyright (c) 2026 netcorelink
 *
 * Public API for libchttpx.
 *
 * This is the only public header installed by the library. Implementation
 * details and internal module headers live under src/.
 */

#ifndef LIBCHTTPX_H
#define LIBCHTTPX_H


/* ========================================================================== */
/* cHTTPX_crosspltm.h */
/* ========================================================================== */
#ifndef CROSSPLTM_H
#define CROSSPLTM_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#define CHTTPX_PLATFORM_WINDOWS
#else
#define CHTTPX_PLATFORM_POSIX
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
#define strdup _strdup
#else
#define strdup strdup
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
#define chttpx_close(s) closesocket(s)
#else
#define chttpx_close(s) close(s)
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <time.h>
#endif

#ifdef _WIN32
    typedef SOCKET chttpx_socket_t;
#else
typedef int chttpx_socket_t;
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
    static inline struct tm* localtime_r(const time_t* timep, struct tm* result)
    {
        memset(result, 0, sizeof(*result));
        localtime_s(result, timep);
        return result;
    }

    static inline struct tm* gmtime_r(const time_t* timep, struct tm* result)
    {
        memset(result, 0, sizeof(*result));
        gmtime_s(result, timep);
        return result;
    }

    static inline int chttpx_clock_gettime(int clock_id, struct timespec* value)
    {
        if (!value)
            return -1;
        if (clock_id == CLOCK_MONOTONIC)
        {
            LARGE_INTEGER frequency;
            LARGE_INTEGER counter;
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&counter);
            value->tv_sec = (time_t)(counter.QuadPart / frequency.QuadPart);
            value->tv_nsec = (long)(((counter.QuadPart % frequency.QuadPart) * 1000000000LL) / frequency.QuadPart);
            return 0;
        }
        FILETIME file_time;
        ULARGE_INTEGER ticks;
        GetSystemTimeAsFileTime(&file_time);
        ticks.LowPart = file_time.dwLowDateTime;
        ticks.HighPart = file_time.dwHighDateTime;
        unsigned long long unix_ticks = ticks.QuadPart - 116444736000000000ULL;
        value->tv_sec = (time_t)(unix_ticks / 10000000ULL);
        value->tv_nsec = (long)((unix_ticks % 10000000ULL) * 100ULL);
        return 0;
    }

#define clock_gettime chttpx_clock_gettime
#endif

#ifdef CHTTPX_PLATFORM_POSIX
#include <unistd.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

#ifdef CHTTPX_PLATFORM_WINDOWS
#define strcasecmp _stricmp
#endif

    static inline void* chttpx_memmem(const void* haystack, size_t haystacklen, const void* needle, size_t needlelen)
    {
        if (!needlelen)
            return (void*)haystack;
        if (needlelen > haystacklen)
            return NULL;

        const unsigned char* h = haystack;
        const unsigned char* n = needle;

        for (size_t i = 0; i <= haystacklen - needlelen; i++)
        {
            if (h[i] == n[0] && memcmp(h + i, n, needlelen) == 0)
                return (void*)(h + i);
        }

        return NULL;
    }

#ifdef __cplusplus
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_http.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef HTTP_H
#define HTTP_H

#ifdef __cplusplus
extern "C"
{
#endif

/* HTTP Content Types */
/* HTML document. Use this for web pages rendered by browsers. */
#define cHTTPX_CTYPE_HTML "text/html"
/* Plain text. Use for simple text responses or logs. */
#define cHTTPX_CTYPE_TEXT "text/plain"
/* XML document. Use for XML-based APIs or configurations. */
#define cHTTPX_CTYPE_XML "application/xml"
/* CSS stylesheet. Use when returning CSS files for web pages. */
#define cHTTPX_CTYPE_CSS "text/css"
/* CSV file. Use for spreadsheet-style data exports. */
#define cHTTPX_CTYPE_CSV "text/csv"
/* JSON data. Use for REST API responses and requests. */
#define cHTTPX_CTYPE_JSON "application/json"
/* URL-encoded form data. Typical for HTML form submissions. */
#define cHTTPX_CTYPE_FORM "application/x-www-form-urlencoded"
/* Multipart form data. Used for file uploads via forms. */
#define cHTTPX_CTYPE_MULTI "multipart/form-data"
/* Raw binary stream. Use when content type is unknown. */
#define cHTTPX_CTYPE_OCTET "application/octet-stream"
/* JavaScript script file. Used for web applications. */
#define cHTTPX_CTYPE_JS "application/javascript"
/* PNG image format. Lossless compressed image. */
#define cHTTPX_CTYPE_PNG "image/png"
/* JPEG image format. Common for photos. */
#define cHTTPX_CTYPE_JPEG "image/jpeg"
/* GIF image format. Supports simple animations. */
#define cHTTPX_CTYPE_GIF "image/gif"
/* WebP image format. Modern compressed image format. */
#define cHTTPX_CTYPE_WEBP "image/webp"
/* SVG vector image format. */
#define cHTTPX_CTYPE_SVG "image/svg+xml"
/* BMP bitmap image format. Rarely used on the web. */
#define cHTTPX_CTYPE_BMP "image/bmp"
/* MP3 audio format. Common compressed audio. */
#define cHTTPX_CTYPE_MP3 "audio/mpeg"
/* WAV audio format. Uncompressed audio. */
#define cHTTPX_CTYPE_WAV "audio/wav"
/* OGG audio format. Open-source audio container. */
#define cHTTPX_CTYPE_OGG "audio/ogg"
/* MP4 video format. Most common video container. */
#define cHTTPX_CTYPE_MP4 "video/mp4"
/* WebM video format. Open-source video format. */
#define cHTTPX_CTYPE_WEBM "video/webm"
/* AVI video format. Older Microsoft video format. */
#define cHTTPX_CTYPE_AVI "video/x-msvideo"
/* ZIP archive file. */
#define cHTTPX_CTYPE_ZIP "application/zip"
/* RAR archive file. */
#define cHTTPX_CTYPE_RAR "application/vnd.rar"
/* 7-Zip archive file. */
#define cHTTPX_CTYPE_7Z "application/x-7z-compressed"
/* PDF document file. */
#define cHTTPX_CTYPE_PDF "application/pdf"
/* Microsoft Word DOC document. */
#define cHTTPX_CTYPE_DOC "application/msword"
/* Microsoft Word DOCX document. */
#define cHTTPX_CTYPE_DOCX "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
/* Microsoft Excel XLS spreadsheet. */
#define cHTTPX_CTYPE_XLS "application/vnd.ms-excel"
/* Microsoft Excel XLSX spreadsheet. */
#define cHTTPX_CTYPE_XLSX "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"
/* Web Open Font Format. */
#define cHTTPX_CTYPE_WOFF "font/woff"
/* Web Open Font Format 2. */
#define cHTTPX_CTYPE_WOFF2 "font/woff2"
/* TrueType font. */
#define cHTTPX_CTYPE_TTF "font/ttf"
/* OpenType font. */
#define cHTTPX_CTYPE_OTF "font/otf"

/* HTTP methods */
#define cHTTPX_MethodGet "GET"
#define cHTTPX_MethodPost "POST"
#define cHTTPX_MethodPut "PUT"
#define cHTTPX_MethodPatch "PATCH"
#define cHTTPX_MethodDelete "DELETE"
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
    extern
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_request.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef REQUEST_H
#define REQUEST_H

#ifdef __cplusplus
extern "C"
{
#endif


#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define CHTTPX_ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

    struct chttpx_serv;

#define MAX_BUFFER_BODY (1024ULL * 1024 * 1024) // 1GB
#define BUFFER_SIZE 16384

#define MAX_HEADERS 128
#define MAX_HEADER_NAME 128
#define MAX_HEADER_VALUE 4096

    /* Header structure */
    typedef struct
    {
        char name[MAX_HEADER_NAME];
        char value[MAX_HEADER_VALUE];
    } chttpx_header_t;

    /* Query structure */
    typedef struct
    {
        char* name;
        char* value;
    } chttpx_query_t;

#define MAX_PARAMS 64
#define MAX_PARAM_NAME 128
#define MAX_PARAM_VALUE 1024

    /* Param structure */
    typedef struct
    {
        char name[MAX_PARAM_NAME];
        char value[MAX_PARAM_VALUE];
    } chttpx_param_t;

#define MAX_COOKIES 64

    /* Cookie structure */
    typedef struct
    {
        char* name;
        char* value;

        char* path;
        char* domain;

        time_t expires;

        bool http_only;
        bool secure;

        /* 0 - None; 1 - Lax; 2 - Strict; 3 - None */
        int same_site;
    } chttpx_cookie_t;

    /* Validation structs */
    typedef enum
    {
        FIELD_STRING,
        FIELD_NUMBER,
        FIELD_BOOL,
        FIELD_STRING_ARRAY,
        FIELD_NUMBER_ARRAY
    } validation_t;

    typedef struct
    {
        char** items;
        size_t count;
    } chttpx_string_array_t;

    typedef struct
    {
        int* items;
        size_t count;
    } chttpx_number_array_t;

    typedef enum
    {
        VALIDATOR_NONE,
        VALIDATOR_EMAIL,
        VALIDATOR_PHONE,
        VALIDATOR_URL,
    } validator_type_t;

    typedef bool (*chttpx_custom_validator_t)(const void* value, char* error, size_t error_size);

    typedef enum
    {
        CHTTPX_NORMALIZE_NONE = 0,
        CHTTPX_TRIM = 1 << 0,
        CHTTPX_LOWERCASE = 1 << 1,
        CHTTPX_UPPERCASE = 1 << 2
    } chttpx_normalizer_t;

    typedef struct
    {
        const char* name;

        /* Target value in struct */
        void* target;

        /* Required field */
        bool required;

        /* Min/Max value string */
        size_t min_length;
        size_t max_length;

        /* Type field str/int/bool */
        validation_t type;

        /* Custom validator */
        validator_type_t validator;

        /* Present type for boolean required */
        uint8_t present;

        /* Optional transformations and application validator */
        unsigned int normalizers;
        chttpx_custom_validator_t custom_validator;
    } chttpx_validation_t;

    /* Function for free REQuest context */
    typedef void (*chttpx_context_free_fn)(void*);

    typedef void (*chttpx_cleanup_fn)(void*);

    typedef struct
    {
        const char* path;
        const char* original_name;
        const char* content_type;
        size_t size;
        bool temporary;
        const char* field_name;
    } chttpx_file_t;

    typedef int (*chttpx_body_chunk_fn)(const unsigned char* data, size_t size, void* user_data);

    // REQuest
    typedef struct
    {
        char* method;
        char* path;

        /* Body */
        unsigned char* body;
        size_t body_size;

        /* Content len. REQuest */
        size_t content_length;

        /* Content type REQuest */
        char content_type[512];

        /* Client socket */
        chttpx_socket_t client_fd;

        /* User-Agent */
        char user_agent[512];

        /* HTTP/1.1 HTTP/2 ... */
        char protocol[16];

        /* Client IP REQuest */
        char client_ip[46];

        /* Error REQuest message */
        char error_msg[BUFFER_SIZE];

        /* Request metadata */
        char request_id[65];
        char language[16];

        /* Headers in REQuest */
        chttpx_header_t headers[MAX_HEADERS];
        size_t headers_count;

        /* Query params in URL
         * exmaple: ?name=netcorelink
         */
        chttpx_query_t* query;
        size_t query_count;

        /* Params in URL
         * exmaple: /{uuid}
         */
        chttpx_param_t params[MAX_PARAMS];
        size_t params_count;

        /* Cookies */
        chttpx_cookie_t cookies[MAX_COOKIES];
        size_t cookies_count;

        /* Media
         * @filename - File name
         */
        char filename[384];

        chttpx_file_t* files;
        size_t files_count;

        chttpx_query_t* form_values;
        size_t form_values_count;

        /* Context REQuest */
        void* context;
        chttpx_context_free_fn context_free;

        /* App-managed server/microservice handling this request. */
        struct chttpx_serv* _server;

        /* Internal transport state. NULL for plain HTTP. */
        void* _tls_session;

        /* Internal request lifecycle state. */
        void* _cleanup_entries;
        void* _contexts;
        chttpx_body_chunk_fn _body_chunk_fn;
        void* _body_chunk_data;
        /* Disk-backed upload body awaiting media parsing (multipart or raw). */
        void* _multipart_stream;
        int _parse_status;
    } chttpx_request_t;

    /**
     * Allocate zero-initialized memory owned by the current request.
     *
     * The library automatically releases the allocation after
     * the request.
     *
     * @param req Current HTTP request.
     * @param size Number of bytes to allocate.
     * @return Allocated memory, or
     * NULL on invalid input or allocation failure.
     */
    void* cHTTPX_Alloc(chttpx_request_t* req, size_t size);

    /**
     * Duplicate a string into request-owned memory.
     *
     * @param req Current HTTP request.
     * @param str Null-terminated string
     * to duplicate.
     * @return Request-owned string, or NULL on failure. The caller must not free it.
     */
    char* cHTTPX_Strdup(chttpx_request_t* req, const char* str);

    /**
     * Register an arbitrary resource for cleanup after the request.
     *
     * @param req Current HTTP request.
     * @param resource
     * Resource passed to cleanup_fn during cleanup.
     * @param cleanup_fn Function that releases the resource.
     * @return 0 on success, -1 on
     * invalid input or allocation failure.
     */
    int cHTTPX_Defer(chttpx_request_t* req, void* resource, chttpx_cleanup_fn cleanup_fn);

    /**
     * Remove a resource from automatic request cleanup.
     *
     * Ownership is transferred to the caller after a successful detach.
 *

     * * @param req Current HTTP request.
     * @param resource Previously registered resource.
     * @return The detached resource, or NULL when it
     * was not registered.
     */
    void* cHTTPX_Detach(chttpx_request_t* req, void* resource);

    /**
     * Run all registered request cleanup callbacks.
     *
     * This is an internal lifecycle function normally called by the server.
 *

     * * @param req Request whose resources must be released.
     */
    void cHTTPX_RequestCleanup(chttpx_request_t* req);

    /**
     * Store or replace a named request context.
     *
     * The cleanup callback is invoked automatically after the request. Replacing

     * * an existing value also cleans up the previous value.
     *
     * @param req Current HTTP request.
     * @param name Context name.
     *
     * @param value Application value; may be NULL.
     * @param cleanup_fn Optional value cleanup callback.
     * @return 0 on success, -1 on
     * invalid input or allocation failure.
     */
    int cHTTPX_ContextSet(chttpx_request_t* req, const char* name, void* value, chttpx_context_free_fn cleanup_fn);

    /**
     * Get a named request context.
     *
     * @param req Current HTTP request.
     * @param name Context name.
     * @return Borrowed
     * context value, or NULL when it does not exist.
     */
    void* cHTTPX_ContextGet(chttpx_request_t* req, const char* name);

    /**
     * Detach a named context from automatic cleanup.
     *
     * @param req Current HTTP request.
     * @param name Context name.
     *
     * @return Detached value owned by the caller, or NULL when not found.
     */
    void* cHTTPX_ContextDetach(chttpx_request_t* req, const char* name);

    /**
     * Extract a Bearer token from the Authorization header.
     *
     * The Bearer prefix is matched case-insensitively.
     *
     *
     * @param req Current HTTP request.
     * @return Borrowed token pointer, or NULL for a missing or invalid header.
     */
    const char* cHTTPX_BearerToken(chttpx_request_t* req);

    /**
     * Consume the request body through a chunk callback.
     *
     * Buffered bodies and temporary uploads are replayed in bounded chunks.

     * *
     * @param req Current HTTP request.
     * @param callback Function invoked for each body chunk.
     * @param user_data Application
     * value passed to callback.
     * @return 0 on success, -1 on invalid input, I/O error, or callback failure.
     */
    int cHTTPX_OnBodyChunk(chttpx_request_t* req, chttpx_body_chunk_fn callback, void* user_data);

    /**
     * Parse a JSON body and validate fields according to the provided definitions.
     * @param req Pointer to the HTTP request.
     * @param fields Array of field validation definitions (cHTTPX_FieldValidation).
     * @param field_count Number of fields in the array.
     * @return 1 if parsing and validation succeed, 0 if there is an error.
     * This function automatically checks required fields, string length, boolean types, etc.
     */
    int cHTTPX_Parse(chttpx_request_t* req, chttpx_validation_t* fields, size_t field_count);

    /*
     * Validates an array of cHTTPX_FieldValidation structures.
     * This function ensures that required fields are present, string lengths are within limits,
     * and basic validation for integers and boolean fields is performed.
     */
    int cHTTPX_Validate(chttpx_request_t* req, chttpx_validation_t* fields, size_t field_count, const char* l);

    struct chttpx_response;
    /**
     * Parse and validate a JSON request body.
     *
     * Parsed strings and arrays are request-owned. On failure this function
     *
     * creates a safe JSON 400 response in res.
     *
     * @param req Current HTTP request.
     * @param res Response populated when binding
     * fails.
     * @param fields Field definitions and output targets.
     * @param field_count Number of field definitions.
     * @return 1 on
     * success, 0 on parsing or validation failure.
     */
    int cHTTPX_BindJSON(chttpx_request_t* req, struct chttpx_response* res, chttpx_validation_t* fields, size_t field_count);

/**
 * Macro to define a string field for JSON request validation.
 *
 * Creates a chttpx_validation_t structure for a string field, including
 * whether it is required and optional minimum/maximum length constraints.
 *
 * @param name       Name of the field in the JSON body.
 * @param required   Non-zero if the field is required, 0 if optional.
 * @param min_length Minimum allowed string length (0 for no minimum).
 * @param max_length Maximum allowed string length (0 for no maximum).
 * @param ptr        Pointer to the target string variable where the value will be stored.
 *
 * @return A chttpx_validation_t structure initialized for a string field.
 */
#define chttpx_validation_string(name, ptr, required, min_length, max_length, validator)                                                             \
    (chttpx_validation_t)                                                                                                                            \
    {                                                                                                                                                \
        name, ptr, required, min_length, max_length, FIELD_STRING, validator, 0, CHTTPX_NORMALIZE_NONE, NULL                                         \
    }

/**
 * Macro to define an integer field for JSON request validation.
 * Creates a chttpx_validation_t structure for an integer field.
 *
 * @param name     Name of the field in the JSON body.
 * @param required Non-zero if the field is required, 0 if optional.
 * @param ptr      Pointer to the target int variable where the value will be stored.
 *
 * @return A chttpx_validation_t structure initialized for an integer field.
 */
#define chttpx_validation_integer(name, ptr, required)                                                                                               \
    (chttpx_validation_t)                                                                                                                            \
    {                                                                                                                                                \
        name, ptr, required, 0, 0, FIELD_NUMBER, VALIDATOR_NONE, 0, CHTTPX_NORMALIZE_NONE, NULL                                                      \
    }

/**
 * Macro to define a boolean field for JSON request validation.
 * Creates a chttpx_validation_t structure for a boolean field.
 *
 * @param name     Name of the field in the JSON body.
 * @param required Non-zero if the field is required, 0 if optional.
 * @param ptr      Pointer to the target int variable where the boolean value (0/1) will be stored.
 *
 * @return A chttpx_validation_t structure initialized for a boolean field.
 */
#define chttpx_validation_boolean(name, ptr, required)                                                                                               \
    (chttpx_validation_t)                                                                                                                            \
    {                                                                                                                                                \
        name, ptr, required, 0, 0, FIELD_BOOL, VALIDATOR_NONE, 0, CHTTPX_NORMALIZE_NONE, NULL                                                        \
    }

#define cHTTPX_StringField(name, ptr, required, min_length, max_length, normalizers, validator)                                                      \
    (chttpx_validation_t)                                                                                                                            \
    {                                                                                                                                                \
        name, ptr, required, min_length, max_length, FIELD_STRING, VALIDATOR_NONE, 0, normalizers, validator                                         \
    }

#ifdef __cplusplus
    extern
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_response.h */
/* ========================================================================== */
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


#include <time.h>

    struct chttpx_serv;

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

    /* Internal route dispatcher shared by socket requests and cHTTPX_Call(). */
    int _chttpx_dispatch(struct chttpx_serv* server, chttpx_request_t* req, chttpx_response_t* res);

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


/* ========================================================================== */
/* cHTTPX_middlewares.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef MIDDLEWARES_H
#define MIDDLEWARES_H

#ifdef __cplusplus
extern "C"
{
#endif



#include <stdio.h>
#include <pthread.h>

#define MAX_MIDDLEWARES 128

    struct chttpx_serv;

    /* Enum for result all middlewares */
    typedef enum
    {
        out = 0,
        next = 1,
    } chttpx_middleware_result_t;

    typedef chttpx_middleware_result_t (*chttpx_middleware_t)(chttpx_request_t* req, chttpx_response_t* res);

    /* Struct for base middlewares */
    typedef struct
    {
        chttpx_middleware_t middlewares[MAX_MIDDLEWARES];
        size_t middleware_count;
        chttpx_middleware_t after_middlewares[MAX_MIDDLEWARES];
        size_t after_middleware_count;
    } chttpx_middleware_stack_t;

    /**
     * Register a global middleware function.
     *
     * Middleware functions are executed in the order they are registered,
     * before the route handler is called.
     *
     * If a middleware returns 0(out), the middleware chain is aborted and the
     * response provided by the middleware is sent to the client.
     *
     * If a middleware returns 1(next), processing continues to the next middleware
     * or to the route handler.
     *
     * @param mw Middleware function pointer.
     */
    void cHTTPX_MiddlewareUse(struct chttpx_serv* server, chttpx_middleware_t mw);

    /** Register a global middleware that runs after the route handler. */
    void cHTTPX_MiddlewareUseAfter(struct chttpx_serv* server, chttpx_middleware_t mw);

#define MAX_MIDDLEWARE_RATE_LIMIT_TABLE_SIZE 4096

    /* Struct for middleware [RATE LIMITER]*/
    typedef struct
    {
        /* Start limit window time*/
        time_t window_start;
        /* How many requests have already been received in this window */
        uint32_t requests;
    } rate_limiter_entry_t;

    /**
     * Configure the rate limiter and register the middleware.
     *
     * Example:
     * cHTTPX_MiddlewareRateLimiter(10, 1); // 10 requests per second
     *
     * @param max_requests maximum number of requests
     * @param window_sec time window in seconds
     */
    void cHTTPX_MiddlewareRateLimiter(struct chttpx_serv* server, uint32_t max_requests, uint32_t window_sec);

    /**
     * Compatibility hook. Fatal signal recovery is deliberately disabled:
     * continuing after memory-corrupting faults is not safe.
     */
    void _recovery_init(void);

    /**
     * Register the compatibility recovery middleware. It is a no-op; use a
     * process supervisor to restart after fatal signals.
     */
    void cHTTPX_MiddlewareRecovery(struct chttpx_serv* server);

    /**
     * Writes the HTTP request and response log to a file.
     *
     * This function is called after a request has been processed and a response
     * has been generated. It collects information about the client, HTTP method,
     * request path, protocol, response status, response size, and processing time,
     * then writes it to a log file.
     *
     * @param req Pointer to the chttpx_request_t request structure.
     * @param res Pointer to the chttpx_response_t response structure.
     */
    void postmiddleware_logging_write(chttpx_request_t* req, chttpx_response_t* res);

    /**
     * Registers a middleware for logging HTTP requests.
     *
     * This middleware is executed after every request to log request and response
     * information into a log file. If logging has not been initialized via
     * cHTTPX_LoggingInit, this middleware does not perform any logging.
     */
    void cHTTPX_MiddlewareLogging(struct chttpx_serv* server);

    /* Internal cleanup for per-server middleware state. */
    void _chttpx_middleware_server_cleanup(struct chttpx_serv* server);

#ifdef __cplusplus
    extern
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_cors.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef CORS_H
#define CORS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdio.h>
#include <stdint.h>

    struct chttpx_serv;

    typedef struct
    {
        uint8_t enabled;
        /* Allowed urls */
        const char** origins;
        /* Origins count*/
        size_t origins_count;
        /* Allowed http methods */
        const char* methods;
        /* Allowed http headers */
        const char* headers;
    } chttpx_cors_t;

    /**
     * Enable and configure CORS (Cross-Origin Resource Sharing).
     *
     * This function enables CORS support for the HTTP server and configures
     * which origins, HTTP methods, and request headers are allowed.
     *
     * The CORS configuration is applied globally and is typically used together
     * with the built-in CORS middleware.
     *
     * @param origins        Array of allowed origin strings (e.g. "https://example.com").
     *                       Each origin must match exactly the value of the "Origin" header.
     * @param origins_count Number of elements in the origins array.
     * @param methods       Comma-separated list of allowed HTTP methods.
     *                       If NULL, defaults to:
     *                       "GET, POST, PUT, DELETE, OPTIONS"
     * @param headers       Comma-separated list of allowed request headers.
     *                       If NULL, defaults to:
     *                       "Content-Type"
     */
    void cHTTPX_Cors(struct chttpx_serv* server, const char** origins, size_t origins_count, const char* methods, const char* headers);

#ifdef __cplusplus
    extern
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_serv.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef SERV_H
#define SERV_H

#ifdef __cplusplus
extern "C"
{
#endif




#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define CHTTPX_MAX_PATH 4096
#define MAX_CLIENTS_DEFAULT 255

    struct chttpx_app;

    typedef enum
    {
        CHTTPX_OK = 0,
        CHTTPX_ERR_MEMORY = -1,
        CHTTPX_ERR_SOCKET = -2,
        CHTTPX_ERR_BIND = -3,
        CHTTPX_ERR_LISTEN = -4,
        CHTTPX_ERR_INVALID_ARGUMENT = -5,
        CHTTPX_ERR_LIMIT = -6,
        CHTTPX_ERR_IO = -7,
        CHTTPX_ERR_NOT_FOUND = -8,
        CHTTPX_ERR_PROTOCOL = -9,
        CHTTPX_ERR_STATE = -10,
        CHTTPX_ERR_UNAVAILABLE = -11,
        CHTTPX_ERR_TIMEOUT = -12,
        CHTTPX_ERR_TLS = -13
    } chttpx_error_t;

    typedef enum
    {
        CHTTPX_LOG_DEBUG,
        CHTTPX_LOG_INFO,
        CHTTPX_LOG_WARN,
        CHTTPX_LOG_ERROR,
        CHTTPX_LOG_OFF
    } chttpx_log_level_t;

    typedef void (*chttpx_logger_fn)(chttpx_log_level_t level, const char* request_id, const char* message, void* user_data);

    typedef enum
    {
        CHTTPX_NETWORK_IPV4 = 0,
        CHTTPX_NETWORK_IPV6,
        CHTTPX_NETWORK_DUAL
    } chttpx_network_mode_t;

    typedef struct
    {
        bool enabled;
        const char* cert_file;
        const char* key_file;
        const char* client_ca_file;
        bool require_client_cert;
    } chttpx_tls_config_t;

    typedef struct
    {
        bool verify_peer;
        const char* ca_file;
        const char* client_cert_file;
        const char* client_key_file;
    } chttpx_tls_client_config_t;

    typedef struct
    {
        uint16_t port;
        chttpx_network_mode_t network_mode;
        chttpx_tls_config_t tls;
        size_t max_clients;
        uint16_t read_timeout_sec;
        uint16_t write_timeout_sec;
        uint16_t idle_timeout_sec;
        size_t max_body_size;
        size_t max_upload_size;
        size_t max_header_size;
        bool request_id_enabled;
        const char** languages;
        size_t languages_count;
        const char* default_language;
        chttpx_log_level_t log_level;
        chttpx_logger_fn logger;
        void* logger_data;
    } chttpx_config_t;

    typedef struct
    {
        size_t max_size;
        const char** allowed_types;
        size_t allowed_types_count;
    } chttpx_upload_policy_t;

    typedef struct
    {
        const char* method;
        const char* path;
        chttpx_handler_t handler;
        chttpx_middleware_t middlewares[MAX_MIDDLEWARES];
        size_t middleware_count;
        chttpx_middleware_t after_middlewares[MAX_MIDDLEWARES];
        size_t after_middleware_count;
        chttpx_upload_policy_t upload_policy;
        bool has_upload_policy;
    } chttpx_route_t;

    typedef struct chttpx_serv
    {
        struct chttpx_app* app;
        char* name;
        bool initialized;

        uint16_t port;
        chttpx_network_mode_t network_mode;
        chttpx_socket_t server_fd;

        chttpx_tls_config_t tls;
        void* _tls_ctx;

        size_t max_clients;
        size_t current_clients;
        volatile bool shutdown_requested;
        volatile bool listening;

        uint16_t read_timeout_sec;
        uint16_t write_timeout_sec;
        uint16_t idle_timeout_sec;
        size_t max_body_size;
        size_t max_upload_size;
        size_t max_header_size;

        bool request_id_enabled;
        const char** languages;
        size_t languages_count;
        const char* default_language;

        chttpx_log_level_t log_level;
        chttpx_logger_fn logger;
        void* logger_data;

        chttpx_route_t** routes;
        size_t routes_count;
        size_t routes_capacity;

        chttpx_middleware_stack_t middleware;
        bool logging_enabled;
        void* rate_limiter_state;

        chttpx_cors_t cors;
    } chttpx_serv_t;

    typedef struct
    {
        chttpx_serv_t* server;
        chttpx_socket_t client_fd;
    } chttpx_client_ctx_t;

    typedef struct
    {
        chttpx_serv_t* serv;
        char prefix[CHTTPX_MAX_PATH];
        chttpx_middleware_t middlewares[MAX_MIDDLEWARES];
        size_t middleware_count;
        chttpx_middleware_t after_middlewares[MAX_MIDDLEWARES];
        size_t after_middleware_count;
    } chttpx_router_t;

    /** Return a server configuration initialized with library defaults. */
    chttpx_config_t cHTTPX_DefaultConfig(void);

    /** Create a router bound to one App-managed server. */
    chttpx_router_t cHTTPX_RoutePathPrefix(chttpx_serv_t* server, const char* prefix);

    void cHTTPX_RegisterRoute(chttpx_router_t* r, const char* method, const char* path, chttpx_handler_t handler);

    chttpx_route_t* cHTTPX_Get(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Post(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Put(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Patch(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Delete(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Options(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    chttpx_router_t cHTTPX_RouteGroup(const chttpx_router_t* parent, const char* prefix);

    int cHTTPX_RouterUse(chttpx_router_t* router, chttpx_middleware_t middleware);
    int cHTTPX_RouterUseAfter(chttpx_router_t* router, chttpx_middleware_t middleware);
    int cHTTPX_RouteUse(chttpx_route_t* route, chttpx_middleware_t middleware);
    int cHTTPX_RouteUseAfter(chttpx_route_t* route, chttpx_middleware_t middleware);
    int cHTTPX_RouteUploadPolicy(chttpx_route_t* route, const chttpx_upload_policy_t* policy);
    void cHTTPX_RouterFree(chttpx_router_t* router);

    /** Configure logging for one App-managed component. */
    void cHTTPX_SetLogger(chttpx_serv_t* server, chttpx_logger_fn logger, void* user_data, chttpx_log_level_t level);

    /* Internal App/runtime helpers. */
    int _chttpx_server_set_languages(chttpx_serv_t* server, const char** languages, size_t count, const char* fallback);
    int _chttpx_server_init(chttpx_serv_t* server, struct chttpx_app* app, const char* name, const chttpx_config_t* config);
    void _chttpx_server_listen(chttpx_serv_t* server);
    void _chttpx_server_shutdown(chttpx_serv_t* server);

#ifdef __cplusplus
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_app.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * Application runtime for managing multiple independent HTTP servers.
 */

#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C"
{
#endif


#include <stdbool.h>
#include <stddef.h>

    typedef struct chttpx_app
    {
        void* _servers;
        size_t _servers_count;
        size_t _servers_capacity;

        void* _remotes;
        size_t _remotes_count;
        size_t _remotes_capacity;

        bool _initialized;
        bool _started;
        bool _network_initialized;
    } chttpx_app_t;

    /** Initialize an application runtime. */
    int cHTTPX_AppInit(chttpx_app_t* app);

    /** Create an App-managed HTTP server using the supplied configuration. */
    chttpx_serv_t* cHTTPX_AppServer(chttpx_app_t* app, const char* name, const chttpx_config_t* config);

    /** Return client TLS defaults (peer verification enabled, system trust store). */
    chttpx_tls_client_config_t cHTTPX_DefaultTLSClientConfig(void);

    /**
     * Register a server that lives in another process/container.
     *
     * Both http:// and https:// are accepted. HTTPS uses certificate
     * verification by default.
     */
    int cHTTPX_AppRemote(chttpx_app_t* app, const char* name, const char* base_url);

    /**
     * Register a remote server with explicit TLS client settings.
     *
     * ca_file == NULL uses the system trust store. Client certificate/key
     * fields are optional and enable mutual TLS when both are provided.
     */
    int cHTTPX_AppRemoteEx(chttpx_app_t* app, const char* name, const char* base_url,
                           const chttpx_tls_client_config_t* tls_config);

    /** Start every local server in the App in its own listener thread. */
    int cHTTPX_AppStart(chttpx_app_t* app);

    /** Wait until all started server listener threads exit. */
    int cHTTPX_AppWait(chttpx_app_t* app);

    /** Convenience helper equivalent to AppStart followed by AppWait. */
    int cHTTPX_AppRun(chttpx_app_t* app);

    /** Stop every server and release all App-owned resources. */
    void cHTTPX_AppShutdown(chttpx_app_t* app);

    /**
     * Call another server by name.
     *
     * Local AppServer targets are dispatched directly without opening another
     * TCP connection. AppRemote targets are called over HTTP with a fixed
     * 30-second connect/send/receive timeout.
     *
     * The current body, content type, request id, language and request headers
     * are inherited from req.
     */
    int cHTTPX_Call(chttpx_request_t* req, const char* server_name, const char* method, const char* path, chttpx_response_t* res);

    typedef struct
    {
        const void* body;
        size_t body_size;
        const char* content_type;
    } chttpx_call_options_t;

    /**
     * Extended call variant.
     *
     * Use options to override the body/content type inherited by cHTTPX_Call().
     * More call-level customization can be added to this structure later
     * without introducing separate call functions.
     */
    int cHTTPX_CallEx(chttpx_request_t* req, const char* server_name, const char* method, const char* path,
                      const chttpx_call_options_t* options, chttpx_response_t* res);

#ifdef __cplusplus
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_inet.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef INET_H
#define INET_H

#ifdef __cplusplus
extern "C"
{
#endif


    /**
     * Get client IP address from the underlying socket connection.
     *
     * This function retrieves the real network-level IP address of the client
     * using the TCP socket (`getpeername`). It supports both IPv4 and IPv6.
     *
     * The returned value is a pointer to a static buffer, so it will be
     * overwritten on subsequent calls and is NOT thread-safe.
     *
     * This IP cannot be spoofed by HTTP headers, but if the server is behind
     * a reverse proxy (Nginx, CDN, load balancer), the returned address will
     * be the proxy’s IP instead of the original client.
     *
     * @param client_fd Connected client socket file descriptor.
     * @return Pointer to a string with the client IP address,
     *         or "-" if the address cannot be determined.
     */
    const char* cHTTPX_ClientInetIP(chttpx_socket_t client_fd);

#ifdef __cplusplus
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_params.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef PARAMS_H
#define PARAMS_H

#ifdef __cplusplus
extern "C"
{
#endif


    /**
     * Get a route parameter value by its name.
     * @param req  Pointer to the current HTTP request structure.
     * @param name Name of the route parameter (e.g., "uuid").
     *
     * @return Pointer to the parameter value string if found, or NULL if the parameter does not exist.
     */
    const char* cHTTPX_Param(chttpx_request_t* req, const char* name);

    /** Parse a route parameter as an integer. Returns 1 on success, otherwise 0. */
    int cHTTPX_ParamInt(chttpx_request_t* req, const char* name, int* value);

    /** Parse a route parameter as an unsigned 64-bit integer. Returns 1 on success, otherwise 0. */
    int cHTTPX_ParamU64(chttpx_request_t* req, const char* name, uint64_t* value);

    /** Parse a route parameter as a boolean. Returns 1 on success, otherwise 0. */
    int cHTTPX_ParamBool(chttpx_request_t* req, const char* name, bool* value);

#ifdef __cplusplus
    extern
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_queries.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef QUERIES_H
#define QUERIES_H

#ifdef __cplusplus
extern "C"
{
#endif


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
    const char* cHTTPX_Query(chttpx_request_t* req, const char* name);

    /** Parse a query parameter as an integer. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryInt(chttpx_request_t* req, const char* name, int* value);

    /** Parse a query parameter as an unsigned 64-bit integer. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryU64(chttpx_request_t* req, const char* name, uint64_t* value);

    /** Parse a query parameter as a boolean. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryBool(chttpx_request_t* req, const char* name, bool* value);

    /** Parse a query parameter as a double. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryDouble(chttpx_request_t* req, const char* name, double* value);

    /** Parse a query parameter or write default_value when it is absent or invalid. */
    int cHTTPX_QueryU64Default(chttpx_request_t* req, const char* name, uint64_t* value, uint64_t default_value);

    /**
     * Decode percent-encoded URL data into a caller-provided buffer.
     *
     * @param destination      Destination buffer.
     * @param
     * destination_size Destination size including the terminator.
     * @param source           Encoded source string.
     * @param plus_as_space
     * Decode '+' as a space when true.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_UrlDecode(char* destination, size_t destination_size, const char* source, bool plus_as_space);

    /* Parse queries in request */
    void _parse_req_query(chttpx_request_t* req, char* query);

#ifdef __cplusplus
    extern
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_headers.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef HEADERS_H
#define HEADERS_H

#ifdef __cplusplus
extern "C"
{
#endif



    /**
     * Get a request header by name.
     * @param req Pointer to the HTTP request.
     * @param name Header name (case-insensitive).
     * @return Pointer to header value if found, otherwise NULL.
     */
    const char* cHTTPX_HeaderGet(chttpx_request_t* req, const char* name);

    /**
     * Add a new HTTP header.
     *
     * This function appends a header to the request/response header list.
     * Unlike HeaderSet, it does NOT replace existing headers with the same name.
     * This is required for headers like "Set-Cookie" that may appear multiple times.
     *
     * @param res   Pointer to HTTP request/response structure.
     * @param name  Header name.
     * @param value Header value.
     */
    int cHTTPX_HeaderAdd(chttpx_response_t* res, const char* name, const char* value);

    /**
     * Set or add a request header.
     * If header exists (case-insensitive), its value will be replaced.
     * Otherwise a new header will be added.
     *
     * @param req Pointer to the HTTP request.
     * @param name Header name.
     * @param value Header value.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_HeaderSet(chttpx_request_t* req, const char* name, const char* value);

    /**
     * Get the client's IP from the HEADER request.
     *
     * @param req a pointer to the query structure
     * @return const char* Client's IP
     */
    const char* cHTTPX_ClientIP(chttpx_request_t* req);

    /* Parse headers in request */
    void _parse_req_headers(chttpx_request_t* req, char* buffer, size_t buffer_len);

#ifdef __cplusplus
    extern
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_cookies.h */
/* ========================================================================== */
#ifndef COOKIES_H
#define COOKIES_H

#ifdef __cplusplus
extern "C"
{
#endif



    /* Parse cookie in request */
    void _parse_req_cookies(chttpx_request_t* req);

    /* Free cookies before response */
    void chttpx_free_req_cookie(chttpx_request_t* req);

    /**
     * Get cookie value by name.
     *
     * Searches for a cookie in the request by its name (case-insensitive).
     *
     * @param req   Pointer to the HTTP request structure.
     * @param name  Cookie name to search for.
     *
     * @return Pointer to the cookie value string if found,
     *         or NULL if the cookie does not exist or input is invalid.
     */
    const chttpx_cookie_t* cHTTPX_CookieGet(chttpx_request_t* req, const char* name);

    /**
     * Set an HTTP cookie.
     *
     * This function formats a Set-Cookie header according to RFC 6265
     * and appends it to the header list using HeaderAdd.
     *
     * Supported attributes:
     *  - Path
     *  - Domain
     *  - Expires (GMT format)
     *  - SameSite (Lax, Strict, None)
     *  - Secure
     *  - HttpOnly
     *
     * @param req     Pointer to HTTP request/response structure.
     * @param cookie  Pointer to cookie structure.
     */
    int cHTTPX_CookieSet(chttpx_response_t* res, const chttpx_cookie_t* cookie);

#ifdef __cplusplus
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_i18n.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef I18N_H
#define I18N_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <stddef.h>
#include <string.h>

#define MAX_LOCALES 64

    struct chttpx_serv;

    typedef struct
    {
        char* key;
        char* value;
    } i18n_entry_t;

    typedef struct
    {
        /* Language en, ru, es */
        char locale[8];

        /* Entries i18n */
        i18n_entry_t* entries;
        size_t count;
    } i18n_locale_t;

    typedef struct
    {
        i18n_locale_t locales[MAX_LOCALES];
        size_t count;
        i18n_locale_t* default_locale;
    } i18n_manager_t;

    typedef enum
    {
        LANG_EN,
        LANG_RU,
        LANG_ES,
        LANG_FR,
        LANG_COUNT
    } i18n_language_t;

    i18n_language_t i18n_lang_from_string(const char* code);

    /**
     * Initializes the global i18n manager.
     *
     * Loads all locale JSON files from the specified directory.
     * The file name determines the locale language:
     * en.json -> "en"
     * ru.json -> "ru"
     * fr.json -> "fr"
     *
     * All translations are stored globally in memory and are used by the cHTTPX_i18n_t() function.
     *
     * The memory is automatically freed when the program ends.
     *
     * @param directory The path to the directory with locale JSON files.
     *
     * Example:
     *   cHTTPX_i18n("public");
     */
    void cHTTPX_i18n(const char* directory);

    /**
     * Returns a translation by key and language.
     *
     * Searches for a translation by key in the specified locale.
     * If the language is not found, the default locale is used.
     * If the key is not found, the key itself is returned.
     *
     * The function does not allocate memory — the returned string
     * belongs to the i18n manager.
     *
     * @param key  Translation key (for example: "welcome").
     * @param lang Language code ("en", "ru", NULL for default).
     *
     * @return The translation string or key if the translation is not found.
     *
     * Example:
     *   const char* text = cHTTPX_i18n_t("welcome", "ru");
     */
    const char* cHTTPX_i18n_t(const char* key, const char* lang);

    /**
     * Configure the language preference list used for request negotiation.
     *
     * @param languages Ordered array of supported language
     * codes.
     * @param count     Number of elements in languages.
     * @param fallback  Fallback language code.
     * @return CHTTPX_OK on
     * success, otherwise a negative error code.
     */
    int cHTTPX_i18n_languages(struct chttpx_serv* server, const char** languages, size_t count, const char* fallback);

#ifdef __cplusplus
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_media.h */
/* ========================================================================== */
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


/* ========================================================================== */
/* cHTTPX_json.h */
/* ========================================================================== */
#ifndef JSON_H
#define JSON_H

#ifdef __cplusplus
extern "C"
{
#endif



    typedef struct chttpx_json chttpx_json_t;

    /** Escape a string for JSON. The returned string is owned by the request arena. */
    char* cHTTPX_JsonEscape(chttpx_request_t* req, const char* value);

    /** Create an empty JSON object owned by the request arena. */
    chttpx_json_t* cHTTPX_JsonObject(chttpx_request_t* req);

    /** Create an empty JSON array owned by the request arena. */
    chttpx_json_t* cHTTPX_JsonArray(chttpx_request_t* req);

    /** Add a string property to a JSON object. */
    int cHTTPX_JsonString(chttpx_json_t* json, const char* key, const char* value);

    /** Add a numeric property to a JSON object. */
    int cHTTPX_JsonNumber(chttpx_json_t* json, const char* key, double value);

    /** Add a boolean property to a JSON object. */
    int cHTTPX_JsonBool(chttpx_json_t* json, const char* key, bool value);

    /** Add a null property to a JSON object. */
    int cHTTPX_JsonNull(chttpx_json_t* json, const char* key);

    /** Add a nested object or array to a JSON object. */
    int cHTTPX_JsonChild(chttpx_json_t* json, const char* key, chttpx_json_t* child);

    /** Append a string value to a JSON array. */
    int cHTTPX_JsonArrayString(chttpx_json_t* json, const char* value);

    /** Append a numeric value to a JSON array. */
    int cHTTPX_JsonArrayNumber(chttpx_json_t* json, double value);

    /** Append a boolean value to a JSON array. */
    int cHTTPX_JsonArrayBool(chttpx_json_t* json, bool value);

    /** Append a null value to a JSON array. */
    int cHTTPX_JsonArrayNull(chttpx_json_t* json);

    /** Append a nested object or array to a JSON array. */
    int cHTTPX_JsonArrayChild(chttpx_json_t* json, chttpx_json_t* child);

    /** Serialize a JSON builder into an application/json HTTP response. */
    chttpx_response_t cHTTPX_ResJsonObject(uint16_t status, chttpx_json_t* json);

#ifdef __cplusplus
}
#endif

#endif


/* ========================================================================== */
/* cHTTPX_websocket.h */
/* ========================================================================== */
/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */


#include <stdlib.h>

#define CHTTPX_WSOCKET_OPCODE_CONTINUATION 0x0
#define CHTTPX_WSOCKET_OPCODE_TEXT 0x1
#define CHTTPX_WSOCKET_OPCODE_BINARY 0x2
#define CHTTPX_WSOCKET_OPCODE_CLOSE 0x8
#define CHTTPX_WSOCKET_OPCODE_PING 0x9
#define CHTTPX_WSOCKET_OPCODE_PONG 0xA

typedef struct
{
    /* FIN - final fragment
     * 1 eq. this is the last frame of the message
     * 0 eq. the message is divided into several parts
     */
    int fin;
    /* Check define CHTTPX_WSOCKET_OPCODE */
    int opcode;
    int masked;
    /* Length data in payload */
    uint64_t payload_len;
    /* Mask for XOR payload */
    unsigned char mask[4];
    /* Data in socket */
    unsigned char* payload;
} wsocket_frame_t;

typedef struct
{
    int socket;
    int connected;
} chttpx_wsocket_t;

typedef void (*chttpx_wsocket_handler_t)(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

typedef void (*chttpx_wsocket_route_t)(chttpx_wsocket_t* wsocket);

void cHTTPX_WSocketRegisterRoute(chttpx_router_t* r, const char* path, chttpx_wsocket_route_t handler);

int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key);

int cHTTPX_WSocketSend(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len);

int cHTTPX_WSocketRecv(chttpx_wsocket_t* wsocket, unsigned char* buffer, size_t len);


#endif /* LIBCHTTPX_H */
