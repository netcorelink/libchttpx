/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef BODY_H
#define BODY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "request.h"

void _parse_req_body(chttpx_request_t *req, char *buffer, size_t buffer_len);

#ifdef __cplusplus
extern }
#endif

#endif