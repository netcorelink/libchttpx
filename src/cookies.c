#include "cookies.h"

#include "headers.h"
#include "crosspltm.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHTTPX_SAMESITE_NONE 0
#define CHTTPX_SAMESITE_LAX 1
#define CHTTPX_SAMESITE_STRICT 2
#define CHTTPX_SAMESITE_NONE_MODE 3

/* Parse cookie in request */
void _parse_req_cookies(chttpx_request_t* req)
{
    const char* cookie_header = cHTTPX_HeaderGet(req, "Cookie");
    if (!cookie_header)
        return;

    char buffer[strlen(cookie_header) + 1];
    strcpy(buffer, cookie_header);

    char* pair = strtok(buffer, ";");

    while (pair && req->cookies_count < MAX_COOKIES)
    {
        while (*pair == ' ')
            pair++;

        char* eq = strchr(pair, '=');
        if (eq)
        {
            *eq = '\0';

            req->cookies[req->cookies_count].name = strdup(pair);
            req->cookies[req->cookies_count].value = strdup(eq + 1);

            req->cookies[req->cookies_count].path = strdup("/");
            req->cookies[req->cookies_count].domain = NULL;

            req->cookies[req->cookies_count].expires = 0;
            req->cookies[req->cookies_count].http_only = false;
            req->cookies[req->cookies_count].secure = false;
            req->cookies[req->cookies_count].same_site = 0;

            req->cookies_count++;
        }

        pair = strtok(NULL, ";");
    }
}

void chttpx_free_req_cookie(chttpx_request_t* req)
{
    if (!req)
        return;

    for (size_t i = 0; i < req->cookies_count; i++)
    {
        free(req->cookies[i].name);
        free(req->cookies[i].value);
        free(req->cookies[i].path);
        free(req->cookies[i].domain);
    }

    req->cookies_count = 0;
}

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
const chttpx_cookie_t* cHTTPX_CookieGet(chttpx_request_t* req, const char* name)
{
    if (!req || req->cookies_count == 0 || !name)
        return NULL;

    for (size_t i = 0; i < req->cookies_count; i++)
    {
        if (strcasecmp(req->cookies[i].name, name) == 0)
        {
            return &req->cookies[i];
        }
    }

    return NULL;
}

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
int cHTTPX_CookieSet(chttpx_request_t* req, const chttpx_cookie_t* cookie)
{
    if (!req || !cookie)
        return -1;

    char buffer[1024];
    size_t offset = 0;

    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "%s=%s", cookie->name, cookie->value);

    if (cookie->path[0])
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; Path=%s", cookie->path);
    }

    if (cookie->domain[0])
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; Domain=%s", cookie->domain);
    }

    if (cookie->expires > 0)
    {
        struct tm gm;
        gmtime_r(&cookie->expires, &gm);

        char timebuf[128];
        strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", &gm);

        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; Expires=%s", timebuf);
    }

    if (cookie->same_site == CHTTPX_SAMESITE_LAX)
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; SameSite=Lax");
    }
    else if (cookie->same_site == CHTTPX_SAMESITE_STRICT)
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; SameSite=Strict");
    }
    else if (cookie->same_site == CHTTPX_SAMESITE_NONE_MODE)
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; SameSite=None");
    }

    if (cookie->secure)
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; Secure");
    }

    if (cookie->http_only)
    {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "; HttpOnly");
    }

    return cHTTPX_HeaderAdd(req, "Set-Cookie", buffer);
}