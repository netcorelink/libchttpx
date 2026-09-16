/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef QUERIES_H
#define QUERIES_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "request.h"

    /**
     * Get a query parameter value by name.
     *
     * Searches the parsed URL query parameters (e.g. ?name=value&age=10)
     * and returns the value associated with the given parameter name.
     *
     * @param req   Pointer to the current HTTP request.
     * @param name  Name of the query parameter.
     * @return Pointer to the parameter value string if found, or NULL if not present.
     */
    const char* cHTTPX_Query(chttpx_request_t* req, const char* name);

    /** Parse a query parameter as an integer. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryInt(chttpx_request_t* req, const char* name, int* value);

    /** Parse a query parameter as an unsigned 64-bit integer. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryU64(chttpx_request_t* req, const char* name, uint64_t* value);

    /** Parse a query parameter as a boolean. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryBool(chttpx_request_t* req, const char* name, bool* value);

    /** Parse a query parameter as a double. Returns 1 on success, otherwise 0. */
    int cHTTPX_QueryDouble(chttpx_request_t* req, const char* name, double* value);

    /** Parse a query parameter or write default_value when it is absent or invalid. */
    int cHTTPX_QueryU64Default(chttpx_request_t* req, const char* name, uint64_t* value, uint64_t default_value);

    /**
     * Decode percent-encoded URL data into a caller-provided buffer.
     *
     * @param destination      Destination buffer.
     * @param
     * destination_size Destination size including the terminator.
     * @param source           Encoded source string.
     * @param plus_as_space
     * Decode '+' as a space when true.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_UrlDecode(char* destination, size_t destination_size, const char* source, bool plus_as_space);

    /* Parse queries in request */
    void _parse_req_query(chttpx_request_t* req, char* query);

#ifdef __cplusplus
    extern
}
#endif

#endif
