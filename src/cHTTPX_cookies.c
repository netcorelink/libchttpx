/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "cHTTPX_cookies.h"

#include "cHTTPX_headers.h"
#include "cHTTPX_crosspltm.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define CHTTPX_SAMESITE_NONE 0
#define CHTTPX_SAMESITE_LAX 1
#define CHTTPX_SAMESITE_STRICT 2
#define CHTTPX_SAMESITE_NONE_MODE 3

/**
 * Append formatted text to a fixed cookie header buffer.
 *
 * @param buffer Destination buffer.
 * @param capacity Total size of buffer.
 * @param offset In/out write offset.
 * @param format printf-style format string.
 * @return 1 on success, 0 if the buffer would overflow.
 */
static int cookie_append(char* buffer, size_t capacity, size_t* offset, const char* format, ...)
{
    if (*offset >= capacity)
        return 0;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer + *offset, capacity - *offset, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= capacity - *offset)
        return 0;
    *offset += (size_t)written;
    return 1;
}

/**
 * Parse Cookie header into the request cookie table.
 *
 * @param req Request whose Cookie header should be parsed.
 */
void _parse_req_cookies(chttpx_request_t* req)
{
    const char* cookie_header = cHTTPX_HeaderGet(req, "Cookie");
    if (!cookie_header)
        return;

    char buffer[strlen(cookie_header) + 1];
    strcpy(buffer, cookie_header);

    char* pair = buffer;
    while (pair && *pair && req->cookies_count < MAX_COOKIES)
    {
        char* next_pair = strchr(pair, ';');
        if (next_pair)
            *next_pair++ = '\0';
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

            if (!req->cookies[req->cookies_count].name || !req->cookies[req->cookies_count].value || !req->cookies[req->cookies_count].path)
            {
                free(req->cookies[req->cookies_count].name);
                free(req->cookies[req->cookies_count].value);
                free(req->cookies[req->cookies_count].path);
                memset(&req->cookies[req->cookies_count], 0, sizeof(req->cookies[req->cookies_count]));
                req->_parse_status = 500;
                return;
            }

            req->cookies[req->cookies_count].expires = 0;
            req->cookies[req->cookies_count].http_only = false;
            req->cookies[req->cookies_count].secure = false;
            req->cookies[req->cookies_count].same_site = 0;

            req->cookies_count++;
        }

        pair = next_pair;
    }
}

/**
 * Free heap-allocated cookies attached to a request.
 *
 * @param req Request whose cookies should be released.
 */
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
 * @param req Pointer to the HTTP request structure.
 * @param name Cookie name to search for.
 * @return Pointer to the cookie structure if found, or NULL if missing or invalid input.
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
 * @param res Pointer to HTTP response structure.
 * @param cookie Pointer to cookie structure.
 * @return 0 on success, -1 on error.
 */
int cHTTPX_CookieSet(chttpx_response_t* res, const chttpx_cookie_t* cookie)
{
    if (!res || !cookie || !cookie->name || !cookie->value)
        return -1;

    char buffer[1024];
    size_t offset = 0;

    if (!cookie_append(buffer, sizeof(buffer), &offset, "%s=%s", cookie->name, cookie->value))
        return -1;

    if (cookie->path && cookie->path[0])
    {
        if (!cookie_append(buffer, sizeof(buffer), &offset, "; Path=%s", cookie->path))
            return -1;
    }

    if (cookie->domain && cookie->domain[0])
    {
        if (!cookie_append(buffer, sizeof(buffer), &offset, "; Domain=%s", cookie->domain))
            return -1;
    }

    if (cookie->expires > 0)
    {
        struct tm gm;
        gmtime_r(&cookie->expires, &gm);

        char timebuf[128];
        strftime(timebuf, sizeof(timebuf), "%a, %d %b %Y %H:%M:%S GMT", &gm);

        if (!cookie_append(buffer, sizeof(buffer), &offset, "; Expires=%s", timebuf))
            return -1;
    }

    if (cookie->same_site == CHTTPX_SAMESITE_LAX)
    {
        if (!cookie_append(buffer, sizeof(buffer), &offset, "; SameSite=Lax"))
            return -1;
    }
    else if (cookie->same_site == CHTTPX_SAMESITE_STRICT)
    {
        if (!cookie_append(buffer, sizeof(buffer), &offset, "; SameSite=Strict"))
            return -1;
    }
    else if (cookie->same_site == CHTTPX_SAMESITE_NONE_MODE)
    {
        if (!cookie_append(buffer, sizeof(buffer), &offset, "; SameSite=None"))
            return -1;
    }

    if (cookie->secure)
    {
        if (!cookie_append(buffer, sizeof(buffer), &offset, "; Secure"))
            return -1;
    }

    if (cookie->http_only)
    {
        if (!cookie_append(buffer, sizeof(buffer), &offset, "; HttpOnly"))
            return -1;
    }

    return cHTTPX_HeaderAdd(res, "Set-Cookie", buffer);
}
