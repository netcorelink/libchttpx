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

#include "cHTTPX_crosspltm.h"

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

    /** One HTTP request header name/value pair (fixed-size storage). */
    typedef struct
    {
        char name[MAX_HEADER_NAME];
        char value[MAX_HEADER_VALUE];
    } chttpx_header_t;

    /** Parsed query-string name/value pair (heap strings). */
    typedef struct
    {
        char* name;
        char* value;
    } chttpx_query_t;

#define MAX_PARAMS 64
#define MAX_PARAM_NAME 128
#define MAX_PARAM_VALUE 1024

    /** Route template parameter extracted from the request path. */
    typedef struct
    {
        char name[MAX_PARAM_NAME];
        char value[MAX_PARAM_VALUE];
    } chttpx_param_t;

#define MAX_COOKIES 64

    /** Parsed Cookie header entry. */
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

    /** JSON field type used by cHTTPX_Parse / cHTTPX_Validate. */
    typedef enum
    {
        FIELD_STRING,
        FIELD_NUMBER,
        FIELD_BOOL,
        FIELD_STRING_ARRAY,
        FIELD_NUMBER_ARRAY
    } validation_t;

    /** Parsed JSON array of strings (request-owned items). */
    typedef struct
    {
        char** items;
        size_t count;
    } chttpx_string_array_t;

    /** Parsed JSON array of numbers (request-owned items). */
    typedef struct
    {
        int* items;
        size_t count;
    } chttpx_number_array_t;

    /** Built-in string validator kind for chttpx_validation_t. */
    typedef enum
    {
        VALIDATOR_NONE,
        VALIDATOR_EMAIL,
        VALIDATOR_PHONE,
        VALIDATOR_URL,
    } validator_type_t;

    /** Optional application validator invoked during cHTTPX_Validate. */
    typedef bool (*chttpx_custom_validator_t)(const void* value, char* error, size_t error_size);

    /** Bit flags for normalizing parsed string fields. */
    typedef enum
    {
        cHTTPX_NORMALIZE_NONE = 0,
        cHTTPX_TRIM = 1 << 0,
        cHTTPX_LOWERCASE = 1 << 1,
        cHTTPX_UPPERCASE = 1 << 2
    } chttpx_normalizer_t;

#ifndef CHTTPX_DISABLE_LEGACY_NORMALIZER_NAMES
#define CHTTPX_NORMALIZE_NONE cHTTPX_NORMALIZE_NONE
#define CHTTPX_TRIM cHTTPX_TRIM
#define CHTTPX_LOWERCASE cHTTPX_LOWERCASE
#define CHTTPX_UPPERCASE cHTTPX_UPPERCASE
#endif

    /** One field binding for JSON parse and validate helpers. */
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

    /** Callback that releases a named request context value. */
    typedef void (*chttpx_context_free_fn)(void*);

    /** Callback registered with cHTTPX_Defer for automatic cleanup. */
    typedef void (*chttpx_cleanup_fn)(void*);

    /** Uploaded file metadata tracked on the request. */
    typedef struct
    {
        const char* path;
        const char* original_name;
        const char* content_type;
        size_t size;
        bool temporary;
        const char* field_name;
    } chttpx_file_t;

    /** Chunk callback used by cHTTPX_OnBodyChunk. */
    typedef int (*chttpx_body_chunk_fn)(const unsigned char* data, size_t size, void* user_data);

    struct chttpx_response;

    /** Internal streaming transport used by long-lived response helpers such as SSE. */
    typedef struct
    {
        void* context;
        int (*open)(void* context, const struct chttpx_response* response);
        int (*write)(void* context, const void* data, size_t size);
        int (*close)(void* context);
        bool (*connected)(void* context);
    } chttpx_stream_transport_t;

    /** Per-request HTTP state populated by the server parser. */
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

        /* HTTP protocol negotiated for this request. */
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
        chttpx_stream_transport_t _stream_transport;

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
     * The allocation is released automatically during request cleanup unless
     * it is detached with cHTTPX_Detach().
     *
     * @param req Current HTTP request.
     * @param size Number of bytes to allocate.
     * @return Request-owned memory or NULL on invalid input/allocation failure.
     */
    void* cHTTPX_Alloc(chttpx_request_t* req, size_t size);

    /**
     * Duplicate a string into request-owned memory.
     *
     * @param req Current HTTP request.
     * @param str Null-terminated source string.
     * @return Request-owned copy or NULL on failure.
     */
    char* cHTTPX_Strdup(chttpx_request_t* req, const char* str);

    /**
     * Register an arbitrary resource for automatic request cleanup.
     *
     * @param req Current HTTP request.
     * @param resource Resource passed to cleanup_fn.
     * @param cleanup_fn Callback that releases resource.
     * @return 0 on success or -1 on invalid input/allocation failure.
     */
    int cHTTPX_Defer(chttpx_request_t* req, void* resource, chttpx_cleanup_fn cleanup_fn);

    /**
     * Remove a resource from automatic request cleanup.
     *
     * @param req Current HTTP request.
     * @param resource Previously deferred resource.
     * @return Detached resource owned by the caller, or NULL when not found.
     */
    void* cHTTPX_Detach(chttpx_request_t* req, void* resource);

    /**
     * Run request cleanup callbacks and release internal request-owned state.
     *
     * This is normally invoked by the server lifecycle rather than application
     * code.
     *
     * @param req Request whose scoped resources should be released.
     */
    void cHTTPX_RequestCleanup(chttpx_request_t* req);

    /**
     * Store or replace a named request context.
     *
     * Named contexts use a request-local hash table. Replacing a context invokes
     * the previous cleanup callback when the previous value differs.
     *
     * @param req Current HTTP request.
     * @param name Context key.
     * @param value Application value; may be NULL.
     * @param cleanup_fn Optional callback used to release value.
     * @return 0 on success or -1 on invalid input/allocation failure.
     */
    int cHTTPX_ContextSet(chttpx_request_t* req, const char* name, void* value, chttpx_context_free_fn cleanup_fn);

    /**
     * Look up a named request context.
     *
     * @param req Current HTTP request.
     * @param name Context key.
     * @return Borrowed context value or NULL when absent.
     */
    void* cHTTPX_ContextGet(chttpx_request_t* req, const char* name);

    /**
     * Detach a named context without running its cleanup callback.
     *
     * @param req Current HTTP request.
     * @param name Context key.
     * @return Detached value owned by the caller, or NULL when absent.
     */
    void* cHTTPX_ContextDetach(chttpx_request_t* req, const char* name);

    /**
     * Extract a Bearer token from the Authorization request header.
     *
     * @param req Current HTTP request.
     * @return Borrowed token pointer or NULL for a missing/invalid header.
     */
    const char* cHTTPX_BearerToken(chttpx_request_t* req);

    /**
     * Replay the request body through a bounded chunk callback.
     *
     * @param req Current HTTP request.
     * @param callback Function invoked for each body chunk.
     * @param user_data Caller value forwarded to callback.
     * @return 0 on success or -1 on invalid input, I/O error, or callback failure.
     */
    int cHTTPX_OnBodyChunk(chttpx_request_t* req, chttpx_body_chunk_fn callback, void* user_data);

    /**
     * Parse a JSON body into validation targets.
     *
     * @param req Current HTTP request.
     * @param fields Field definitions and output targets.
     * @param field_count Number of entries in fields.
     * @return 1 on success or 0 on parse/type/allocation failure.
     */
    int cHTTPX_Parse(chttpx_request_t* req, chttpx_validation_t* fields, size_t field_count);

    /**
     * Validate already parsed field values.
     *
     * @param req Current HTTP request.
     * @param fields Field definitions and parsed targets.
     * @param field_count Number of entries in fields.
     * @param l Language code used for validation messages.
     * @return 1 when all values pass validation, otherwise 0.
     */
    int cHTTPX_Validate(chttpx_request_t* req, chttpx_validation_t* fields, size_t field_count, const char* l);

    struct chttpx_response;
    /**
     * Parse and validate a JSON request body.
     *
     * Parsed strings and arrays are request-owned. On failure this function
     * creates a safe JSON 400 response in res.
     *
     * @param req Current HTTP request.
     * @param res Response populated when binding fails.
     * @param fields Field definitions and output targets.
     * @param field_count Number of field definitions.
     * @return 1 on success, 0 on parsing or validation failure.
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
        name, ptr, required, min_length, max_length, FIELD_STRING, validator, 0, cHTTPX_NORMALIZE_NONE, NULL                                         \
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
        name, ptr, required, 0, 0, FIELD_NUMBER, VALIDATOR_NONE, 0, cHTTPX_NORMALIZE_NONE, NULL                                                      \
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
        name, ptr, required, 0, 0, FIELD_BOOL, VALIDATOR_NONE, 0, cHTTPX_NORMALIZE_NONE, NULL                                                        \
    }

#define cHTTPX_StringField(name, ptr, required, min_length, max_length, normalizers, validator)                                                      \
    (chttpx_validation_t)                                                                                                                            \
    {                                                                                                                                                \
        name, ptr, required, min_length, max_length, FIELD_STRING, VALIDATOR_NONE, 0, normalizers, validator                                         \
    }

#ifdef __cplusplus
}
#endif

#endif
