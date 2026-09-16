#ifndef JSON_H
#define JSON_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "request.h"
#include "response.h"

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
