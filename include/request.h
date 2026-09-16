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

#include "crosspltm.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define CHTTPX_ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

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

        /* Internal request lifecycle state. */
        void* _cleanup_entries;
        void* _contexts;
        chttpx_body_chunk_fn _body_chunk_fn;
        void* _body_chunk_data;
        int _parse_status;
    } chttpx_request_t;

    void* cHTTPX_Alloc(chttpx_request_t* req, size_t size);
    char* cHTTPX_Strdup(chttpx_request_t* req, const char* str);
    int cHTTPX_Defer(chttpx_request_t* req, void* resource, chttpx_cleanup_fn cleanup_fn);
    void* cHTTPX_Detach(chttpx_request_t* req, void* resource);
    void cHTTPX_RequestCleanup(chttpx_request_t* req);

    int cHTTPX_ContextSet(chttpx_request_t* req, const char* name, void* value, chttpx_context_free_fn cleanup_fn);
    void* cHTTPX_ContextGet(chttpx_request_t* req, const char* name);
    void* cHTTPX_ContextDetach(chttpx_request_t* req, const char* name);

    const char* cHTTPX_BearerToken(chttpx_request_t* req);
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
