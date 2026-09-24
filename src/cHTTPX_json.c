/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "cHTTPX_json.h"

#include "cHTTPX_http.h"

#if defined(_WIN32) || defined(_WIN64)
#include "../lib/cjson/cJSON.h"
#else
#include <cjson/cJSON.h>
#endif

#include <stdio.h>
#include <string.h>

/** JSON builder backing store (cJSON node plus request arena ownership). */
struct chttpx_json
{
    chttpx_request_t* req;
    cJSON* value;
    bool array;
};

/**
 * Escape a string for JSON output.
 *
 * @param req Request arena that owns the returned string.
 * @param value Raw string to escape.
 * @return Escaped string, or NULL on error.
 */
char* cHTTPX_JsonEscape(chttpx_request_t* req, const char* value)
{
    if (!req || !value)
        return NULL;

    size_t size = 1;
    for (const unsigned char* p = (const unsigned char*)value; *p; p++)
        size += (*p < 0x20) ? 6 : ((*p == '"' || *p == '\\') ? 2 : 1);

    char* escaped = cHTTPX_Alloc(req, size);
    if (!escaped)
        return NULL;

    char* output = escaped;
    for (const unsigned char* p = (const unsigned char*)value; *p; p++)
    {
        if (*p == '"' || *p == '\\')
        {
            *output++ = '\\';
            *output++ = (char)*p;
        }
        else if (*p == '\b' || *p == '\f' || *p == '\n' || *p == '\r' || *p == '\t')
        {
            *output++ = '\\';
            const char escaped_char[] = {'b', 'f', 'n', 'r', 't'};
            const char raw_char[] = {'\b', '\f', '\n', '\r', '\t'};
            size_t i = 0;
            while (raw_char[i] != *p)
                i++;
            *output++ = escaped_char[i];
        }
        else if (*p < 0x20)
        {
            snprintf(output, 7, "\\u%04x", *p);
            output += 6;
        }
        else
        {
            *output++ = (char)*p;
        }
    }
    *output = '\0';
    return escaped;
}

/**
 * Allocate a JSON object or array builder on the request arena.
 *
 * @param req Request arena for allocation and cleanup.
 * @param array True to create an array, false for an object.
 * @return JSON builder, or NULL on error.
 */
static chttpx_json_t* json_create(chttpx_request_t* req, bool array)
{
    if (!req)
        return NULL;
    chttpx_json_t* json = cHTTPX_Alloc(req, sizeof(*json));
    if (!json)
        return NULL;
    json->req = req;
    json->array = array;
    json->value = array ? cJSON_CreateArray() : cJSON_CreateObject();
    if (!json->value || cHTTPX_Defer(req, json->value, (chttpx_cleanup_fn)cJSON_Delete) != 0)
    {
        if (json->value)
            cJSON_Delete(json->value);
        return NULL;
    }
    return json;
}

/** @copydoc cHTTPX_JsonObject */
chttpx_json_t* cHTTPX_JsonObject(chttpx_request_t* req)
{
    return json_create(req, false);
}

/** @copydoc cHTTPX_JsonArray */
chttpx_json_t* cHTTPX_JsonArray(chttpx_request_t* req)
{
    return json_create(req, true);
}

/**
 * Attach a cJSON node to a JSON object builder.
 *
 * @param json Target object builder.
 * @param key Property name.
 * @param value cJSON node (freed on error).
 * @return 0 on success, -1 on error.
 */
static int object_add(chttpx_json_t* json, const char* key, cJSON* value)
{
    if (!json || json->array || !key || !value)
    {
        cJSON_Delete(value);
        return -1;
    }
    cJSON_AddItemToObject(json->value, key, value);
    return 0;
}

/**
 * Append a cJSON node to a JSON array builder.
 *
 * @param json Target array builder.
 * @param value cJSON node (freed on error).
 * @return 0 on success, -1 on error.
 */
static int array_add(chttpx_json_t* json, cJSON* value)
{
    if (!json || !json->array || !value)
    {
        cJSON_Delete(value);
        return -1;
    }
    cJSON_AddItemToArray(json->value, value);
    return 0;
}

/** @copydoc cHTTPX_JsonString */
int cHTTPX_JsonString(chttpx_json_t* json, const char* key, const char* value)
{
    return object_add(json, key, cJSON_CreateString(value ? value : ""));
}

/** @copydoc cHTTPX_JsonNumber */
int cHTTPX_JsonNumber(chttpx_json_t* json, const char* key, double value)
{
    return object_add(json, key, cJSON_CreateNumber(value));
}

/** @copydoc cHTTPX_JsonBool */
int cHTTPX_JsonBool(chttpx_json_t* json, const char* key, bool value)
{
    return object_add(json, key, cJSON_CreateBool(value));
}

/** @copydoc cHTTPX_JsonNull */
int cHTTPX_JsonNull(chttpx_json_t* json, const char* key)
{
    return object_add(json, key, cJSON_CreateNull());
}

/** @copydoc cHTTPX_JsonChild */
int cHTTPX_JsonChild(chttpx_json_t* json, const char* key, chttpx_json_t* child)
{
    if (!json || !child || json->req != child->req || !cHTTPX_Detach(child->req, child->value))
        return -1;
    return object_add(json, key, child->value);
}

/** @copydoc cHTTPX_JsonArrayString */
int cHTTPX_JsonArrayString(chttpx_json_t* json, const char* value)
{
    return array_add(json, cJSON_CreateString(value ? value : ""));
}

/** @copydoc cHTTPX_JsonArrayNumber */
int cHTTPX_JsonArrayNumber(chttpx_json_t* json, double value)
{
    return array_add(json, cJSON_CreateNumber(value));
}

/** @copydoc cHTTPX_JsonArrayBool */
int cHTTPX_JsonArrayBool(chttpx_json_t* json, bool value)
{
    return array_add(json, cJSON_CreateBool(value));
}

/** @copydoc cHTTPX_JsonArrayNull */
int cHTTPX_JsonArrayNull(chttpx_json_t* json)
{
    return array_add(json, cJSON_CreateNull());
}

/** @copydoc cHTTPX_JsonArrayChild */
int cHTTPX_JsonArrayChild(chttpx_json_t* json, chttpx_json_t* child)
{
    if (!json || !child || json->req != child->req || !cHTTPX_Detach(child->req, child->value))
        return -1;
    return array_add(json, child->value);
}

/** @copydoc cHTTPX_ResJsonObject */
chttpx_response_t cHTTPX_ResJsonObject(uint16_t status, chttpx_json_t* json)
{
    if (!json || !json->value)
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, "failed to serialize JSON");
    char* text = cJSON_PrintUnformatted(json->value);
    if (!text)
        return cHTTPX_ResError(cHTTPX_StatusInternalServerError, "failed to serialize JSON");
    chttpx_response_t response = cHTTPX_ResBinary(status, cHTTPX_CTYPE_JSON, (const unsigned char*)text, strlen(text));
    cJSON_free(text);
    return response;
}

/**
 * Build a JSON object response with one string field.
 *
 * @param status HTTP status code.
 * @param name JSON property name.
 * @param message Property value.
 * @return JSON HTTP response.
 */
static chttpx_response_t json_named_response(uint16_t status, const char* name, const char* message)
{
    cJSON* object = cJSON_CreateObject();
    if (!object)
        return cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\":\"internal server error\"}");
    cJSON_AddStringToObject(object, name, message ? message : "");
    char* text = cJSON_PrintUnformatted(object);
    cJSON_Delete(object);
    if (!text)
        return cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\":\"internal server error\"}");
    chttpx_response_t response = cHTTPX_ResBinary(status, cHTTPX_CTYPE_JSON, (const unsigned char*)text, strlen(text));
    cJSON_free(text);
    return response;
}

/**
 * JSON error response with an "error" string field.
 *
 * @param status HTTP status code.
 * @param message Error message text.
 * @return JSON HTTP response.
 */
chttpx_response_t cHTTPX_ResError(uint16_t status, const char* message)
{
    return json_named_response(status, "error", message);
}

/**
 * JSON response with a "message" string field.
 *
 * @param status HTTP status code.
 * @param message Message text.
 * @return JSON HTTP response.
 */
chttpx_response_t cHTTPX_ResMessage(uint16_t status, const char* message)
{
    return json_named_response(status, "message", message);
}
