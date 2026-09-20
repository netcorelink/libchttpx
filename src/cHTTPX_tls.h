#ifndef CHTTPX_TLS_H
#define CHTTPX_TLS_H

#include "cHTTPX_serv.h"

#ifdef __cplusplus
extern "C"
{
#endif

int _chttpx_tls_available(void);
void _chttpx_tls_log_error(chttpx_serv_t* server, const char* request_id, const char* message);
int _chttpx_tls_server_init(chttpx_serv_t* server, const chttpx_tls_config_t* config);
void _chttpx_tls_server_cleanup(chttpx_serv_t* server);
int _chttpx_tls_accept(chttpx_serv_t* server, chttpx_socket_t client_fd, void** session);
void _chttpx_tls_session_close(void* session);

int _chttpx_tls_client_connect(chttpx_socket_t socket_fd, const char* host,
                               const chttpx_tls_client_config_t* config,
                               void** context, void** session);
void _chttpx_tls_client_close(void* context, void* session);

int _chttpx_io_recv(chttpx_socket_t fd, void* tls_session, void* buffer, size_t size);
int _chttpx_io_send_all(chttpx_socket_t fd, void* tls_session, const void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
