#ifndef CHTTPX_TLS_H
#define CHTTPX_TLS_H

#include "cHTTPX_serv.h"

#ifdef __cplusplus
extern "C"
{
#endif

/** Return whether TLS support was compiled in. */
int _chttpx_tls_available(void);

/**
 * Log a TLS failure through the server logger.
 *
 * @param server Server providing the logger callback.
 * @param request_id Correlation id for log output.
 * @param message Short failure prefix.
 */
void _chttpx_tls_log_error(chttpx_serv_t* server, const char* request_id, const char* message);

/**
 * Load server certificate material and prepare the TLS context.
 *
 * @param server Target server.
 * @param config TLS configuration from server setup.
 * @return Zero on success or a negative error code.
 */
int _chttpx_tls_server_init(chttpx_serv_t* server, const chttpx_tls_config_t* config);

/** Release server TLS context and owned path strings. @param server Target server. */
void _chttpx_tls_server_cleanup(chttpx_serv_t* server);

/**
 * Perform a blocking TLS accept and require HTTP/2 ALPN.
 *
 * @param server Owning server.
 * @param client_fd Accepted client socket.
 * @param session Out pointer receiving the accepted OpenSSL session.
 * @return Zero on success or a negative error code.
 */
int _chttpx_tls_accept(chttpx_serv_t* server, chttpx_socket_t client_fd, void** session);

#define CHTTPX_IO_WANT_READ -1001
#define CHTTPX_IO_WANT_WRITE -1002

/**
 * Allocate a TLS session and start a non-blocking accept handshake.
 *
 * @param server Owning server.
 * @param client_fd Accepted client socket.
 * @param session Out pointer receiving the new OpenSSL session.
 * @return Zero on success, WANT_READ/WANT_WRITE while pending, or an error code.
 */
int _chttpx_tls_accept_begin(chttpx_serv_t* server, chttpx_socket_t client_fd, void** session);

/**
 * Advance a non-blocking TLS accept handshake.
 *
 * @param session OpenSSL session from accept_begin.
 * @return Zero when complete, WANT_READ/WANT_WRITE while pending, or an error code.
 */
int _chttpx_tls_accept_step(void* session);

/** Shut down and free one TLS session. @param session OpenSSL session to release. */
void _chttpx_tls_session_close(void* session);

/**
 * Connect as a TLS client with HTTP/2 ALPN and optional mutual TLS.
 *
 * @param socket_fd Connected TCP socket.
 * @param host SNI host name or IP literal.
 * @param config Client trust and certificate settings.
 * @param context Out pointer receiving the client OpenSSL context.
 * @param session Out pointer receiving the connected OpenSSL session.
 * @return Zero on success or a negative error code.
 */
int _chttpx_tls_client_connect(chttpx_socket_t socket_fd, const char* host, const chttpx_tls_client_config_t* config, void** context, void** session);

/** Release client TLS session and context. */
void _chttpx_tls_client_close(void* context, void* session);

/** Blocking receive through TLS or plain socket. */
int _chttpx_io_recv(chttpx_socket_t fd, void* tls_session, void* buffer, size_t size);

/** Non-blocking receive with WANT_READ/WANT_WRITE semantics. */
int _chttpx_io_recv_nonblocking(chttpx_socket_t fd, void* tls_session, void* buffer, size_t size);

/** Non-blocking send with WANT_READ/WANT_WRITE semantics. */
int _chttpx_io_send_nonblocking(chttpx_socket_t fd, void* tls_session, const void* data, size_t size);

/** Send an entire buffer, retrying through TLS or plain socket. */
int _chttpx_io_send_all(chttpx_socket_t fd, void* tls_session, const void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
