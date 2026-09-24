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

#include "cHTTPX_request.h"

    /**
     * Get a query parameter value by name.
     *
     * Searches parsed URL query parameters (e.g. ?name=value&age=10).
     *
     * @param req Pointer to the current HTTP request.
     * @param name Name of the query parameter.
     * @return Pointer to the parameter value string if found, or NULL if not present.
     */
    const char* cHTTPX_Query(chttpx_request_t* req, const char* name);

    /**
     * Parse a query parameter as a signed integer.
     *
     * @param req Current HTTP request.
     * @param name Query parameter name.
     * @param value Output integer on success.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_QueryInt(chttpx_request_t* req, const char* name, int* value);

    /**
     * Parse a query parameter as an unsigned 64-bit integer.
     *
     * @param req Current HTTP request.
     * @param name Query parameter name.
     * @param value Output value on success.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_QueryU64(chttpx_request_t* req, const char* name, uint64_t* value);

    /**
     * Parse a query parameter as a boolean (true/false/1/0).
     *
     * @param req Current HTTP request.
     * @param name Query parameter name.
     * @param value Output boolean on success.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_QueryBool(chttpx_request_t* req, const char* name, bool* value);

    /**
     * Parse a query parameter as a double.
     *
     * @param req Current HTTP request.
     * @param name Query parameter name.
     * @param value Output double on success.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_QueryDouble(chttpx_request_t* req, const char* name, double* value);

    /**
     * Parse a query parameter as uint64_t, or write default_value when absent.
     *
     * @param req Current HTTP request.
     * @param name Query parameter name.
     * @param value Output value.
     * @param default_value Value used when the parameter is missing.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_QueryU64Default(chttpx_request_t* req, const char* name, uint64_t* value, uint64_t default_value);

    /**
     * Decode percent-encoded URL data into a caller-provided buffer.
     *
     * @param destination Destination buffer.
     * @param destination_size Destination size including the terminator.
     * @param source Encoded source string.
     * @param plus_as_space Decode '+' as a space when true.
     * @return 1 on success, otherwise 0.
     */
    int cHTTPX_UrlDecode(char* destination, size_t destination_size, const char* source, bool plus_as_space);

    /**
     * Parse a URL query string into the request query table.
     *
     * @param req Request to populate.
     * @param query Mutable query substring (ampersand-separated pairs).
     */
    void _parse_req_query(chttpx_request_t* req, char* query);

#ifdef __cplusplus
}
#endif

#endif
