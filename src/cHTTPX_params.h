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

#include "cHTTPX_request.h"

    /**
     * Get a route parameter value by its name.
     *
     * @param req Pointer to the current HTTP request structure.
     * @param name Name of the route parameter (e.g., "uuid").
     * @return Pointer to the parameter value string if found, or NULL if absent.
     */
    const char* cHTTPX_Param(chttpx_request_t* req, const char* name);

    /**
     * Parse a route parameter as a signed integer.
     *
     * @param req Current HTTP request.
     * @param name Route parameter name.
     * @param value Output integer on success.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_ParamInt(chttpx_request_t* req, const char* name, int* value);

    /**
     * Parse a route parameter as an unsigned 64-bit integer.
     *
     * @param req Current HTTP request.
     * @param name Route parameter name.
     * @param value Output value on success.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_ParamU64(chttpx_request_t* req, const char* name, uint64_t* value);

    /**
     * Parse a route parameter as a boolean (true/false/1/0).
     *
     * @param req Current HTTP request.
     * @param name Route parameter name.
     * @param value Output boolean on success.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_ParamBool(chttpx_request_t* req, const char* name, bool* value);

#ifdef __cplusplus
}
#endif

#endif
