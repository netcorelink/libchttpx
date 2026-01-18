/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef PARAMS_H
#define PARAMS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "request.h"

/**
 * Get a route parameter value by its name.
 * @param req  Pointer to the current HTTP request structure.
 * @param name Name of the route parameter (e.g., "uuid").
 *
 * @return Pointer to the parameter value string if found, or NULL if the parameter does not exist.
 */
const char* cHTTPX_Param(chttpx_request_t *req, const char *name);

#ifdef __cplusplus
extern }
#endif

#endif