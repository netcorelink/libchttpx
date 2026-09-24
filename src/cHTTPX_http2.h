#ifndef CHTTPX_HTTP2_H
#define CHTTPX_HTTP2_H

#include "cHTTPX_serv.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Serve one HTTP/2 connection until shutdown or I/O failure.
 *
 * @param server Owning server instance.
 * @param client_fd Connected client socket.
 * @param tls_session OpenSSL session, or NULL for cleartext h2c.
 * @return Zero on success or a negative error code.
 */
int _chttpx_http2_serve(chttpx_serv_t* server, chttpx_socket_t client_fd, void* tls_session);

/**
 * Issue an outbound HTTP/2 request and fill a response object.
 *
 * @param source Originating request used for inherited metadata.
 * @param base_url Remote http(s) base URL.
 * @param tls_config Client TLS settings for https URLs.
 * @param method HTTP method.
 * @param path Request path.
 * @param body Optional request body.
 * @param body_size Body length in bytes.
 * @param content_type Optional Content-Type override.
 * @param res Output response object.
 * @return Zero on success or a negative error code.
 */
int _chttpx_http2_call(chttpx_request_t* source, const char* base_url, const chttpx_tls_client_config_t* tls_config, const char* method, const char* path, const void* body, size_t body_size, const char* content_type, chttpx_response_t* res);

#ifdef __cplusplus
}
#endif

#endif
