#include "cookies.h"

#include "headers.h"
#include "crosspltm.h"

#include <string.h>

/* Parse cookie in request */
void _parse_req_cookies(chttpx_request_t* req)
{
    const char* cookie_header = cHTTPX_Header(req, "Cookie");
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

            strncpy(req->cookies[req->cookies_count].name, pair, MAX_COOKIE_NAME - 1);
            strncpy(req->cookies[req->cookies_count].value, eq + 1, MAX_COOKIE_VALUE - 1);

            req->cookies_count++;
        }

        pair = strtok(NULL, ";");
    }
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
const char* cHTTPX_Cookie(chttpx_request_t* req, const char* name)
{
    if (!req || req->cookies_count == 0 || !name)
        return NULL;

    for (size_t i = 0; i < req->cookies_count; i++)
    {
        if (strcasecmp(req->cookies[i].name, name) == 0)
        {
            return req->cookies[i].value;
        }
    }

    return NULL;
}