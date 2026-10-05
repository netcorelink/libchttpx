/*
 * Copyright (c) 2026 netcorelink
 *
 * Distributed under the BSD 3-Clause License. See LICENSE for details.
 */

#include "cHTTPX_params.h"

#include "cHTTPX_crosspltm.h"

#include <errno.h>
#include <limits.h>

/**
 * Get a route parameter value by its name.
 * @param req Pointer to the current HTTP request structure.
 * @param name Name of the route parameter (e.g., "uuid").
 * @return Pointer to the parameter value string if found, or NULL if absent.
 */
const char* cHTTPX_Param(chttpx_request_t* req, const char* name)
{
    if (!req || !name || req->params_count == 0)
        return NULL;

    for (size_t i = 0; i < req->params_count; i++)
    {
        if (strcmp(req->params[i].name, name) == 0)
        {
            return req->params[i].value;
        }
    }

    return NULL;
}

/**
 * Parse a decimal string into a bounded signed 64-bit integer.
 *
 * @param text Input string.
 * @param min_value Minimum allowed value (inclusive).
 * @param max_value Maximum allowed value (inclusive).
 * @param value Output parsed value.
 * @return 1 on success, otherwise 0.
 */
static int parse_i64(const char* text, long long min_value, long long max_value, long long* value)
{
    if (!text || !*text || !value)
        return 0;
    errno = 0;
    char* end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if (errno == ERANGE || !end || *end || parsed < min_value || parsed > max_value)
        return 0;
    *value = parsed;
    return 1;
}

/**
 * Parse a route parameter as a signed integer.
 *
 * @param req Current HTTP request.
 * @param name Route parameter name.
 * @param value Output integer on success.
 * @return 1 on success, otherwise 0.
 */
int cHTTPX_ParamInt(chttpx_request_t* req, const char* name, int* value)
{
    long long parsed;
    if (!value || !parse_i64(cHTTPX_Param(req, name), INT_MIN, INT_MAX, &parsed))
        return 0;
    *value = (int)parsed;
    return 1;
}

/**
 * Parse a route parameter as an unsigned 64-bit integer.
 *
 * @param req Current HTTP request.
 * @param name Route parameter name.
 * @param value Output value on success.
 * @return 1 on success, otherwise 0.
 */
int cHTTPX_ParamU64(chttpx_request_t* req, const char* name, uint64_t* value)
{
    const char* text = cHTTPX_Param(req, name);
    if (!text || !*text || *text == '-' || !value)
        return 0;
    errno = 0;
    char* end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (errno == ERANGE || !end || *end)
        return 0;
    *value = (uint64_t)parsed;
    return 1;
}

/**
 * Parse a route parameter as a boolean (true/false/1/0).
 *
 * @param req Current HTTP request.
 * @param name Route parameter name.
 * @param value Output boolean on success.
 * @return 1 on success, otherwise 0.
 */
int cHTTPX_ParamBool(chttpx_request_t* req, const char* name, bool* value)
{
    const char* text = cHTTPX_Param(req, name);
    if (!text || !value)
        return 0;
    if (strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0)
        *value = true;
    else if (strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0)
        *value = false;
    else
        return 0;
    return 1;
}
