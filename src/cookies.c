#include "cookies.h"

#include "headers.h"
#include "crosspltm.h"

#include <string.h>

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