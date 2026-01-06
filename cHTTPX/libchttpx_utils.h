/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef LIBCHTTPX_UTILS_H
#define LIBCHTTPX_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#define cHTTPX_ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

char *cHTTPX_strdup(const char *s);

#ifdef __cplusplus
}
#endif

#endif