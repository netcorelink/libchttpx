#ifndef JSON_H
#define JSON_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "request.h"
#include "response.h"

    typedef struct chttpx_json chttpx_json_t;

    char* cHTTPX_JsonEscape(chttpx_request_t* req, const char* value);
    chttpx_json_t* cHTTPX_JsonObject(chttpx_request_t* req);
    chttpx_json_t* cHTTPX_JsonArray(chttpx_request_t* req);
    int cHTTPX_JsonString(chttpx_json_t* json, const char* key, const char* value);
    int cHTTPX_JsonNumber(chttpx_json_t* json, const char* key, double value);
    int cHTTPX_JsonBool(chttpx_json_t* json, const char* key, bool value);
    int cHTTPX_JsonNull(chttpx_json_t* json, const char* key);
    int cHTTPX_JsonChild(chttpx_json_t* json, const char* key, chttpx_json_t* child);
    int cHTTPX_JsonArrayString(chttpx_json_t* json, const char* value);
    int cHTTPX_JsonArrayNumber(chttpx_json_t* json, double value);
    int cHTTPX_JsonArrayBool(chttpx_json_t* json, bool value);
    int cHTTPX_JsonArrayNull(chttpx_json_t* json);
    int cHTTPX_JsonArrayChild(chttpx_json_t* json, chttpx_json_t* child);
    chttpx_response_t cHTTPX_ResJsonObject(uint16_t status, chttpx_json_t* json);

#ifdef __cplusplus
}
#endif

#endif
