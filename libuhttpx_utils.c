/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libuhttpx.c` for details.
 */

#include "libuhttpx_utils.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

char *uHTTPX_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}