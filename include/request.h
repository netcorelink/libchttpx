/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef REQUEST_H
#define REQUEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "crosspltm.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_BUFFER_BODY (1024ULL * 1024 * 1024) // 1GB
#define BUFFER_SIZE 16384

#define MAX_HEADERS 128
#define MAX_HEADER_NAME 128
#define MAX_HEADER_VALUE 4096

typedef struct {
    char name[MAX_HEADER_NAME];
    char value[MAX_HEADER_VALUE];
} chttpx_header_t;

typedef struct {
    char *name;
    char *value;
} chttpx_query_t;

#define MAX_PARAMS 64
#define MAX_PARAM_NAME 128
#define MAX_PARAM_VALUE 1024

typedef struct {
    char name[MAX_PARAM_NAME];
    char value[MAX_PARAM_VALUE];
} chttpx_param_t;

#define MAX_COOKIES 64
#define MAX_COOKIE_NAME 128
#define MAX_COOKIE_VALUE 1024

typedef struct {
    char name[MAX_COOKIE_NAME];
    char value[MAX_COOKIE_VALUE];
} chttpx_cookie_t;

/* Validation structs */
typedef enum {
    FIELD_STRING,
    FIELD_INT,
    FIELD_BOOL
} validation_t;

typedef enum {
    VALIDATOR_NONE,
    VALIDATOR_EMAIL,
    VALIDATOR_PHONE,
    VALIDATOR_URL,
} validator_type_t;

typedef struct {
    const char *name;

    /* Target value in struct */
    void *target;

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
} chttpx_validation_t;

// REQuest
typedef struct {
    char* method;
    char* path;

    /* Body */
    unsigned char* body;
    size_t body_size;

    /* Content len. request */
    size_t content_length;

    /* Client socket */
    chttpx_socket_t client_fd;

    /* User-Agent */
    char user_agent[512];

    /* HTTP/1.1 HTTP/2 ... */
    char protocol[16];

    /* Client IP request */
    char client_ip[46];

    /* Error request message */
    char error_msg[BUFFER_SIZE];

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

    /* Context REQuest */
    void* context;
} chttpx_request_t;

/**
 * Parse a JSON body and validate fields according to the provided definitions.
 * @param req Pointer to the HTTP request.
 * @param fields Array of field validation definitions (cHTTPX_FieldValidation).
 * @param field_count Number of fields in the array.
 * @return 1 if parsing and validation succeed, 0 if there is an error.
 * This function automatically checks required fields, string length, boolean types, etc.
 */
int cHTTPX_Parse(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count);

/*
 * Validates an array of cHTTPX_FieldValidation structures.
 * This function ensures that required fields are present, string lengths are within limits,
 * and basic validation for integers and boolean fields is performed.
 */
int cHTTPX_Validate(chttpx_request_t *req, chttpx_validation_t *fields, size_t field_count, const char* l);

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
#define chttpx_validation_string(name, ptr, required, min_length, max_length, validator) (chttpx_validation_t){name, ptr, required, min_length, max_length, FIELD_STRING, validator, 0}

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
#define chttpx_validation_integer(name, ptr, required) (chttpx_validation_t){name, ptr, required, 0, 0, FIELD_INT, VALIDATOR_NONE, 0}

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
#define chttpx_validation_boolean(name, ptr, required) (chttpx_validation_t){name, ptr, required, 0, 0, FIELD_BOOL, VALIDATOR_NONE, 0}


#ifdef __cplusplus
extern }
#endif

#endif
