#ifndef CHTTPX_HTTP2_H
#define CHTTPX_HTTP2_H

#include "cHTTPX_serv.h"

#ifdef __cplusplus
extern "C"
{
#endif

int _chttpx_http2_serve(chttpx_serv_t* server, chttpx_socket_t client_fd, void* tls_session);

int _chttpx_http2_call(chttpx_request_t* source,
                       const char* base_url,
                       const chttpx_tls_client_config_t* tls_config,
                       const char* method,
                       const char* path,
                       const void* body,
                       size_t body_size,
                       const char* content_type,
                       chttpx_response_t* res);

#ifdef __cplusplus
}
#endif

#endif
