/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef COOKIES_H
#define COOKIES_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cHTTPX_request.h"
#include "cHTTPX_response.h"

    /**
     * Parse Cookie header into the request cookie table.
     *
     * @param req Request whose Cookie header should be parsed.
     */
    void _parse_req_cookies(chttpx_request_t* req);

    /**
     * Free heap-allocated cookies attached to a request.
     *
     * @param req Request whose cookies should be released.
     */
    void chttpx_free_req_cookie(chttpx_request_t* req);

    /**
     * Get cookie value by name.
     *
     * Searches for a cookie in the request by its name (case-insensitive).
     *
     * @param req Pointer to the HTTP request structure.
     * @param name Cookie name to search for.
     * @return Pointer to the cookie structure if found, or NULL if missing or invalid input.
     */
    const chttpx_cookie_t* cHTTPX_CookieGet(chttpx_request_t* req, const char* name);

    /**
     * Set an HTTP cookie via Set-Cookie (RFC 6265).
     *
     * Formats a Set-Cookie header and appends it with HeaderAdd. Supports Path, Domain,
     * Expires, SameSite, Secure, and HttpOnly.
     *
     * @param res Pointer to HTTP response structure.
     * @param cookie Pointer to cookie structure.
     * @return 0 on success, -1 on error.
     */
    int cHTTPX_CookieSet(chttpx_response_t* res, const chttpx_cookie_t* cookie);

#ifdef __cplusplus
}
#endif

#endif
