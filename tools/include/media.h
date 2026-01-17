/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef MEDIA_H
#define MEDIA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "include/request.h"

void _parse_media(chttpx_request_t *req);

#ifdef __cplusplus
}
#endif

#endif