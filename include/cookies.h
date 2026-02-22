#ifndef COOKIES_H
#define COOKIES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "request.h"

/* Parse cookie in request */
void _parse_req_cookies(chttpx_request_t *req);

/* Free cookies before response */
void chttpx_free_req_cookie(chttpx_request_t* req);

/**
 * Get cookie value by name.
 *
 * Searches for a cookie in the request by its name (case-insensitive).
 *
 * @param req   Pointer to the HTTP request structure.
 * @param name  Cookie name to search for.
 *
 * @return Pointer to the cookie value string if found,
 *         or NULL if the cookie does not exist or input is invalid.
 */
const chttpx_cookie_t* cHTTPX_CookieGet(chttpx_request_t *req, const char *name);

/**
 * Set an HTTP cookie.
 *
 * This function formats a Set-Cookie header according to RFC 6265
 * and appends it to the header list using HeaderAdd.
 *
 * Supported attributes:
 *  - Path
 *  - Domain
 *  - Expires (GMT format)
 *  - SameSite (Lax, Strict, None)
 *  - Secure
 *  - HttpOnly
 *
 * @param req     Pointer to HTTP request/response structure.
 * @param cookie  Pointer to cookie structure.
 */
int cHTTPX_CookieSet(chttpx_request_t* req, const chttpx_cookie_t* cookie);

#ifdef __cplusplus
}
#endif

#endif