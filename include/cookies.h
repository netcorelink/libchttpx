#ifndef COOKIES_H
#define COOKIES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "request.h"

/* Parse cookie in request */
void _parse_req_cookies(chttpx_request_t *req);

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
const char* cHTTPX_Cookie(chttpx_request_t *req, const char *name);

#ifdef __cplusplus
}
#endif

#endif