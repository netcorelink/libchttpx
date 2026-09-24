/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef JSON_H
#define JSON_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cHTTPX_request.h"
#include "cHTTPX_response.h"

    /** Opaque JSON builder (object or array) tied to a request arena. */
    typedef struct chttpx_json chttpx_json_t;

    /**
     * Escape a string for JSON output.
     *
     * @param req Request arena that owns the returned string.
     * @param value Raw string to escape.
     * @return Escaped string, or NULL on error.
     */
    char* cHTTPX_JsonEscape(chttpx_request_t* req, const char* value);

    /**
     * Create an empty JSON object owned by the request arena.
     *
     * @param req Request arena for allocation and cleanup.
     * @return JSON builder, or NULL on error.
     */
    chttpx_json_t* cHTTPX_JsonObject(chttpx_request_t* req);

    /**
     * Create an empty JSON array owned by the request arena.
     *
     * @param req Request arena for allocation and cleanup.
     * @return JSON builder, or NULL on error.
     */
    chttpx_json_t* cHTTPX_JsonArray(chttpx_request_t* req);

    /**
     * Add a string property to a JSON object.
     *
     * @param json Target object builder.
     * @param key Property name.
     * @param value String value (NULL becomes empty string).
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonString(chttpx_json_t* json, const char* key, const char* value);

    /**
     * Add a numeric property to a JSON object.
     *
     * @param json Target object builder.
     * @param key Property name.
     * @param value Numeric value.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonNumber(chttpx_json_t* json, const char* key, double value);

    /**
     * Add a boolean property to a JSON object.
     *
     * @param json Target object builder.
     * @param key Property name.
     * @param value Boolean value.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonBool(chttpx_json_t* json, const char* key, bool value);

    /**
     * Add a null property to a JSON object.
     *
     * @param json Target object builder.
     * @param key Property name.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonNull(chttpx_json_t* json, const char* key);

    /**
     * Add a nested object or array to a JSON object.
     *
     * @param json Target object builder.
     * @param key Property name.
     * @param child Nested builder (ownership transfers to parent).
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonChild(chttpx_json_t* json, const char* key, chttpx_json_t* child);

    /**
     * Append a string value to a JSON array.
     *
     * @param json Target array builder.
     * @param value String value (NULL becomes empty string).
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonArrayString(chttpx_json_t* json, const char* value);

    /**
     * Append a numeric value to a JSON array.
     *
     * @param json Target array builder.
     * @param value Numeric value.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonArrayNumber(chttpx_json_t* json, double value);

    /**
     * Append a boolean value to a JSON array.
     *
     * @param json Target array builder.
     * @param value Boolean value.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonArrayBool(chttpx_json_t* json, bool value);

    /**
     * Append a null value to a JSON array.
     *
     * @param json Target array builder.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonArrayNull(chttpx_json_t* json);

    /**
     * Append a nested object or array to a JSON array.
     *
     * @param json Target array builder.
     * @param child Nested builder (ownership transfers to parent).
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_JsonArrayChild(chttpx_json_t* json, chttpx_json_t* child);

    /**
     * Serialize a JSON builder into an application/json HTTP response.
     *
     * @param status HTTP status code.
     * @param json Builder to serialize.
     * @return Response with JSON body.
     */
    chttpx_response_t cHTTPX_ResJsonObject(uint16_t status, chttpx_json_t* json);

#ifdef __cplusplus
}
#endif

#endif
