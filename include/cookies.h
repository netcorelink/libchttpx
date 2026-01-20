#ifndef COOKIES_H
#define COOKIES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "request.h"

void _parse_req_cookies(chttpx_request_t *req);

const char* cHTTPX_Cookie(chttpx_request_t *req, const char *name);

#ifdef __cplusplus
}
#endif

#endif