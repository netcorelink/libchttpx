/*
 * Optional TLS transport backend for libchttpx.
 *
 * Define CHTTPX_ENABLE_TLS and link OpenSSL (ssl + crypto) to enable it.
 * Cleartext HTTP/2 builds compile this file without any OpenSSL dependency.
 */

#include "cHTTPX_tls.h"
#include "cHTTPX_crosspltm.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CHTTPX_ENABLE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

/**
 * Select HTTP/2 during TLS ALPN negotiation.
 *
 * @param ssl OpenSSL session participating in ALPN selection.
 * @param out Output pointer to the selected ALPN protocol bytes.
 * @param outlen Output length of the selected ALPN protocol.
 * @param in Client ALPN extension bytes offered during handshake.
 * @param inlen Length of the client ALPN extension.
 * @param arg Unused ALPN callback user argument.
 * @return SSL_TLSEXT_ERR_OK when HTTP/2 is selected, otherwise a fatal alert code.
 */
static int chttpx_h2_alpn_select(SSL* ssl, const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen, void* arg)
{
    (void)ssl;
    (void)arg;
    const unsigned char* cursor = in;
    const unsigned char* end = in + inlen;

    while (cursor < end)
    {
        unsigned int length = *cursor++;
        if ((size_t)(end - cursor) < length)
            break;
        if (length == 2 && cursor[0] == 'h' && cursor[1] == '2')
        {
            *out = cursor;
            *outlen = 2;
            return SSL_TLSEXT_ERR_OK;
        }
        cursor += length;
    }

    return SSL_TLSEXT_ERR_ALERT_FATAL;
}

/**
 * Return whether the negotiated ALPN protocol is HTTP/2.
 *
 * @param ssl OpenSSL session after handshake completion.
 * @return Non-zero when the negotiated ALPN protocol is HTTP/2.
 */
static int chttpx_tls_selected_h2(SSL* ssl)
{
    const unsigned char* protocol = NULL;
    unsigned int length = 0;
    SSL_get0_alpn_selected(ssl, &protocol, &length);
    return length == 2 && protocol && protocol[0] == 'h' && protocol[1] == '2';
}
#endif

/**
 * Log a TLS failure through the server logger.
 *
 * @param server Server instance.
 * @param request_id Request correlation id for logging.
 * @param prefix Short failure prefix prepended to OpenSSL detail.
 */
void _chttpx_tls_log_error(chttpx_serv_t* server, const char* request_id, const char* prefix)
{
    if (!server || !server->logger || server->log_level > cHTTPX_LOG_ERROR)
        return;

#ifdef CHTTPX_ENABLE_TLS
    unsigned long code = ERR_peek_last_error();
    char detail[256] = {0};
    if (code)
        ERR_error_string_n(code, detail, sizeof(detail));

    char message[384];
    if (detail[0])
        snprintf(message, sizeof(message), "%s: %s", prefix, detail);
    else
        snprintf(message, sizeof(message), "%s", prefix);
    server->logger(cHTTPX_LOG_ERROR, request_id && *request_id ? request_id : "-", message, server->logger_data);
#else
    server->logger(cHTTPX_LOG_ERROR, request_id && *request_id ? request_id : "-", prefix, server->logger_data);
#endif
}

/**
 * Return whether TLS support was compiled in.
 *
 * @return Non-zero when OpenSSL support is enabled at build time.
 */
int _chttpx_tls_available(void)
{
#ifdef CHTTPX_ENABLE_TLS
    return 1;
#else
    return 0;
#endif
}

/**
 * Free owned TLS path strings on a server.
 *
 * @param server Server instance.
 */
static void free_server_tls_strings(chttpx_serv_t* server)
{
    if (!server)
        return;

    free((void*)server->tls.cert_file);
    free((void*)server->tls.key_file);
    free((void*)server->tls.client_ca_file);
    memset(&server->tls, 0, sizeof(server->tls));
}

/**
 * Load server certificate material and prepare the TLS context.
 *
 * @param server Server instance.
 * @param config TLS configuration supplied by the caller.
 * @return Zero on success or a negative error code.
 */
int _chttpx_tls_server_init(chttpx_serv_t* server, const chttpx_tls_config_t* config)
{
    if (!server || !config)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (!config->enabled)
        return cHTTPX_OK;

    if (!config->cert_file || !*config->cert_file || !config->key_file || !*config->key_file)
        return cHTTPX_ERR_INVALID_ARGUMENT;

#ifndef CHTTPX_ENABLE_TLS
    return cHTTPX_ERR_UNAVAILABLE;
#else
    if (OPENSSL_init_ssl(0, NULL) != 1)
        return cHTTPX_ERR_TLS;

    server->tls.enabled = true;
    server->tls.require_client_cert = config->require_client_cert;
    server->tls.cert_file = strdup(config->cert_file);
    server->tls.key_file = strdup(config->key_file);
    server->tls.client_ca_file = config->client_ca_file ? strdup(config->client_ca_file) : NULL;

    if (!server->tls.cert_file || !server->tls.key_file ||
        (config->client_ca_file && !server->tls.client_ca_file))
    {
        free_server_tls_strings(server);
        return cHTTPX_ERR_MEMORY;
    }

    SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx)
        goto tls_error;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);
    SSL_CTX_set_alpn_select_cb(ctx, chttpx_h2_alpn_select, NULL);

    if (SSL_CTX_use_certificate_chain_file(ctx, server->tls.cert_file) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx, server->tls.key_file, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx) != 1)
    {
        SSL_CTX_free(ctx);
        goto tls_error;
    }

    if (server->tls.client_ca_file)
    {
        if (SSL_CTX_load_verify_locations(ctx, server->tls.client_ca_file, NULL) != 1)
        {
            SSL_CTX_free(ctx);
            goto tls_error;
        }

        STACK_OF(X509_NAME)* names = SSL_load_client_CA_file(server->tls.client_ca_file);
        if (names)
            SSL_CTX_set_client_CA_list(ctx, names);

       SSL_CTX_set_verify(ctx, server->tls.require_client_cert, ? (SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
                               : SSL_VERIFY_PEER,
                           NULL);
    }
    else if (server->tls.require_client_cert)
    {
        if (SSL_CTX_set_default_verify_paths(ctx) != 1)
        {
            SSL_CTX_free(ctx);
            goto tls_error;
        }
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    }

    server->_tls_ctx = ctx;
    return cHTTPX_OK;

tls_error:
    _chttpx_tls_log_error(server, "-", "TLS server initialization failed");
    free_server_tls_strings(server);
    return cHTTPX_ERR_TLS;
#endif
}

/**
 * Release server TLS context and owned path strings.
 *
 * @param server Server instance.
 */
void _chttpx_tls_server_cleanup(chttpx_serv_t* server)
{
    if (!server)
        return;

#ifdef CHTTPX_ENABLE_TLS
    if (server->_tls_ctx)
        SSL_CTX_free((SSL_CTX*)server->_tls_ctx);
#endif
    server->_tls_ctx = NULL;
    free_server_tls_strings(server);
}

/**
 * Perform a blocking TLS accept and require HTTP/2 ALPN.
 *
 * @param server Server instance.
 * @param client_fd Connected client socket.
 * @param session Out pointer receiving the accepted OpenSSL session.
 * @return Zero on success or a negative error code.
 */
int _chttpx_tls_accept(chttpx_serv_t* server, chttpx_socket_t client_fd, void** session)
{
    if (!server || !session)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    *session = NULL;
    if (!server->tls.enabled)
        return cHTTPX_OK;

#ifndef CHTTPX_ENABLE_TLS
    (void)client_fd;
    return cHTTPX_ERR_UNAVAILABLE;
#else
    SSL* ssl = SSL_new((SSL_CTX*)server->_tls_ctx);
    if (!ssl)
    {
        _chttpx_tls_log_error(server, "-", "TLS session allocation failed");
        return cHTTPX_ERR_TLS;
    }

    if (SSL_set_fd(ssl, (int)client_fd) != 1 || SSL_accept(ssl) != 1 || !chttpx_tls_selected_h2(ssl))
    {
        _chttpx_tls_log_error(server, "-", "TLS handshake failed");
        SSL_free(ssl);
        return cHTTPX_ERR_TLS;
    }

    *session = ssl;
    return cHTTPX_OK;
#endif
}

/**
 * Allocate a TLS session and start a non-blocking accept handshake.
 *
 * @param server Server instance.
 * @param client_fd Connected client socket.
 * @param session Out pointer receiving the new OpenSSL session.
 * @return Zero on success, WANT_READ/WANT_WRITE while pending, or an error code.
 */
int _chttpx_tls_accept_begin(chttpx_serv_t* server, chttpx_socket_t client_fd, void** session)
{
    if (!server || !session)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    *session = NULL;
    if (!server->tls.enabled)
        return cHTTPX_OK;
#ifndef CHTTPX_ENABLE_TLS
    (void)client_fd;
    return cHTTPX_ERR_UNAVAILABLE;
#else
    SSL* ssl = SSL_new((SSL_CTX*)server->_tls_ctx);
    if (!ssl)
        return cHTTPX_ERR_TLS;
    if (SSL_set_fd(ssl, (int)client_fd) != 1)
    {
        SSL_free(ssl);
        return cHTTPX_ERR_TLS;
    }
    SSL_set_accept_state(ssl);
    *session = ssl;
    return cHTTPX_OK;
#endif
}

/**
 * Advance a non-blocking TLS accept handshake.
 *
 * @param session OpenSSL session started by accept_begin.
 * @return Zero when complete, WANT_READ/WANT_WRITE while pending, or an error code.
 */
int _chttpx_tls_accept_step(void* session)
{
    if (!session)
        return cHTTPX_OK;
#ifndef CHTTPX_ENABLE_TLS
    return cHTTPX_ERR_UNAVAILABLE;
#else
    int result = SSL_accept((SSL*)session);
    if (result == 1)
        return chttpx_tls_selected_h2((SSL*)session) ? cHTTPX_OK : cHTTPX_ERR_TLS;
    int error = SSL_get_error((SSL*)session, result);
    if (error == SSL_ERROR_WANT_READ)
        return CHTTPX_IO_WANT_READ;
    if (error == SSL_ERROR_WANT_WRITE)
        return CHTTPX_IO_WANT_WRITE;
    return cHTTPX_ERR_TLS;
#endif
}

/**
 * Shut down and free one TLS session.
 *
 * @param session OpenSSL session to shut down and free.
 */
void _chttpx_tls_session_close(void* session)
{
#ifdef CHTTPX_ENABLE_TLS
    if (session)
    {
        SSL_shutdown((SSL*)session);
        SSL_free((SSL*)session);
    }
#else
    (void)session;
#endif
}

/**
 * Connect as a TLS client with HTTP/2 ALPN and optional mTLS.
 *
 * @param socket_fd Connected TCP socket.
 * @param host Remote host name or IP literal.
 * @param config TLS configuration supplied by the caller.
 * @param context Out pointer receiving the client OpenSSL context.
 * @param session Out pointer receiving the connected OpenSSL session.
 * @return Zero on success or a negative error code.
 */
int _chttpx_tls_client_connect(chttpx_socket_t socket_fd, const char* host, const chttpx_tls_client_config_t* config, void** context, void** session)
{
    if (!host || !*host || !context || !session)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    *context = NULL;
    *session = NULL;

#ifndef CHTTPX_ENABLE_TLS
    (void)socket_fd;
    (void)config;
    return cHTTPX_ERR_UNAVAILABLE;
#else
    chttpx_tls_client_config_t selected = config ? *config : (chttpx_tls_client_config_t){.verify_peer = true};
    if ((selected.client_cert_file && !selected.client_key_file) ||
        (!selected.client_cert_file && selected.client_key_file))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx)
        return cHTTPX_ERR_TLS;

    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ctx, SSL_OP_NO_COMPRESSION);

    if (selected.verify_peer)
    {
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
        int trust_ok = selected.ca_file ? SSL_CTX_load_verify_locations(ctx, selected.ca_file, NULL)
                           : SSL_CTX_set_default_verify_paths(ctx);
        if (trust_ok != 1)
        {
            SSL_CTX_free(ctx);
            return cHTTPX_ERR_TLS;
        }
    }
    else
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    if (selected.client_cert_file)
    {
        if (SSL_CTX_use_certificate_chain_file(ctx, selected.client_cert_file) != 1 ||
            SSL_CTX_use_PrivateKey_file(ctx, selected.client_key_file, SSL_FILETYPE_PEM) != 1 ||
            SSL_CTX_check_private_key(ctx) != 1)
        {
            SSL_CTX_free(ctx);
            return cHTTPX_ERR_TLS;
        }
    }

    SSL* ssl = SSL_new(ctx);
    if (!ssl)
    {
        SSL_CTX_free(ctx);
        return cHTTPX_ERR_TLS;
    }

    unsigned char ip_buffer[16];
    bool is_ip = inet_pton(AF_INET, host, ip_buffer) == 1 || inet_pton(AF_INET6, host, ip_buffer) == 1;

    if (!is_ip && SSL_set_tlsext_host_name(ssl, host) != 1)
        goto error;

    if (selected.verify_peer)
    {
        X509_VERIFY_PARAM* param = SSL_get0_param(ssl);
        if (!param)
            goto error;

        if (is_ip)
        {
            if (X509_VERIFY_PARAM_set1_ip_asc(param, host) != 1)
                goto error;
        }
        else if (SSL_set1_host(ssl, host) != 1)
            goto error;
    }

    static const unsigned char h2_alpn[] = {2, 'h', '2'};
    if (SSL_set_alpn_protos(ssl, h2_alpn, sizeof(h2_alpn)) != 0)
        goto error;

    if (SSL_set_fd(ssl, (int)socket_fd) != 1 || SSL_connect(ssl) != 1 || !chttpx_tls_selected_h2(ssl))
        goto error;

    if (selected.verify_peer && SSL_get_verify_result(ssl) != X509_V_OK)
        goto error;

    *context = ctx;
    *session = ssl;
    return cHTTPX_OK;

error:
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return cHTTPX_ERR_TLS;
#endif
}

/**
 * Release client TLS session and context.
 *
 * @param context Client OpenSSL context returned from client_connect.
 * @param session Client OpenSSL session returned from client_connect.
 */
void _chttpx_tls_client_close(void* context, void* session)
{
#ifdef CHTTPX_ENABLE_TLS
    if (session)
    {
        SSL_shutdown((SSL*)session);
        SSL_free((SSL*)session);
    }
    if (context)
        SSL_CTX_free((SSL_CTX*)context);
#else
    (void)context;
    (void)session;
#endif
}

/**
 * Blocking receive through TLS or plain socket.
 *
 * @param fd Socket descriptor.
 * @param tls_session OpenSSL session for TLS I/O, or NULL for cleartext.
 * @param buffer Memory buffer for socket receive or send.
 * @param size Buffer size in bytes for the I/O operation.
 * @return Zero on success or a negative error code.
 */
int _chttpx_io_recv(chttpx_socket_t fd, void* tls_session, void* buffer, size_t size)
{
    if (!buffer || size == 0)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    size_t wanted = size > INT_MAX ? INT_MAX : size;

#ifdef CHTTPX_ENABLE_TLS
    if (tls_session)
    {
        for (;;)
        {
            int result = SSL_read((SSL*)tls_session, buffer, (int)wanted);
            if (result > 0)
                return result;

            int error = SSL_get_error((SSL*)tls_session, result);
            if (error == SSL_ERROR_ZERO_RETURN)
                return 0;
#ifdef CHTTPX_PLATFORM_POSIX
            if (error == SSL_ERROR_SYSCALL && errno == EINTR)
                continue;
#endif
            return cHTTPX_ERR_TLS;
        }
    }
#else
    (void)tls_session;
#endif

    for (;;)
    {
        int result = recv(fd, (char*)buffer, (int)wanted, 0);
        if (result >= 0)
            return result;
#ifdef CHTTPX_PLATFORM_POSIX
        if (errno == EINTR)
            continue;
#endif
        return -1;
    }
}

/**
 * Non-blocking receive with WANT_READ/WANT_WRITE semantics.
 *
 * @param fd Socket descriptor.
 * @param tls_session OpenSSL session for TLS I/O, or NULL for cleartext.
 * @param buffer Memory buffer for socket receive or send.
 * @param size Buffer size in bytes for the I/O operation.
 * @return Zero on success or a negative error code.
 */
int _chttpx_io_recv_nonblocking(chttpx_socket_t fd, void* tls_session, void* buffer, size_t size)
{
    if (!buffer || size == 0)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    size_t wanted = size > INT_MAX ? INT_MAX : size;
#ifdef CHTTPX_ENABLE_TLS
    if (tls_session)
    {
        int result = SSL_read((SSL*)tls_session, buffer, (int)wanted);
        if (result > 0)
            return result;
        int error = SSL_get_error((SSL*)tls_session, result);
        if (error == SSL_ERROR_WANT_READ)
            return CHTTPX_IO_WANT_READ;
        if (error == SSL_ERROR_WANT_WRITE)
            return CHTTPX_IO_WANT_WRITE;
        if (error == SSL_ERROR_ZERO_RETURN)
            return 0;
        return cHTTPX_ERR_TLS;
    }
#else
    (void)tls_session;
#endif
    int result = recv(fd, (char*)buffer, (int)wanted, 0);
    if (result >= 0)
        return result;
#ifdef CHTTPX_PLATFORM_WINDOWS
    int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK)
        return CHTTPX_IO_WANT_READ;
    if (error == WSAEINTR)
        return CHTTPX_IO_WANT_READ;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return CHTTPX_IO_WANT_READ;
    if (errno == EINTR)
        return CHTTPX_IO_WANT_READ;
#endif
    return cHTTPX_ERR_IO;
}

/**
 * Non-blocking send with WANT_READ/WANT_WRITE semantics.
 *
 * @param fd Socket descriptor.
 * @param tls_session OpenSSL session for TLS I/O, or NULL for cleartext.
 * @param data Payload bytes for the current chunk.
 * @param size Buffer size in bytes for the I/O operation.
 * @return Zero on success or a negative error code.
 */
int _chttpx_io_send_nonblocking(chttpx_socket_t fd, void* tls_session, const void* data, size_t size)
{
    if (size > 0 && !data)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    if (size == 0)
        return 0;
    size_t wanted = size > INT_MAX ? INT_MAX : size;
#ifdef CHTTPX_ENABLE_TLS
    if (tls_session)
    {
        int result = SSL_write((SSL*)tls_session, data, (int)wanted);
        if (result > 0)
            return result;
        int error = SSL_get_error((SSL*)tls_session, result);
        if (error == SSL_ERROR_WANT_READ)
            return CHTTPX_IO_WANT_READ;
        if (error == SSL_ERROR_WANT_WRITE)
            return CHTTPX_IO_WANT_WRITE;
        return cHTTPX_ERR_TLS;
    }
#else
    (void)tls_session;
#endif
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    int result = send(fd, (const char*)data, (int)wanted, flags);
    if (result >= 0)
        return result;
#ifdef CHTTPX_PLATFORM_WINDOWS
    int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK)
        return CHTTPX_IO_WANT_WRITE;
    if (error == WSAEINTR)
        return CHTTPX_IO_WANT_WRITE;
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        return CHTTPX_IO_WANT_WRITE;
    if (errno == EINTR)
        return CHTTPX_IO_WANT_WRITE;
#endif
    return cHTTPX_ERR_IO;
}

/**
 * Send an entire buffer, retrying through TLS or plain socket.
 *
 * @param fd Socket descriptor.
 * @param tls_session OpenSSL session for TLS I/O, or NULL for cleartext.
 * @param data Payload bytes for the current chunk.
 * @param size Buffer size in bytes for the I/O operation.
 * @return Zero on success or a negative error code.
 */
int _chttpx_io_send_all(chttpx_socket_t fd, void* tls_session, const void* data, size_t size)
{
    if (size > 0 && !data)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    const unsigned char* cursor = data;
    size_t sent = 0;

#ifndef CHTTPX_ENABLE_TLS
    (void)tls_session;
#endif

#ifdef SO_NOSIGPIPE
    if (!tls_session)
    {
        int no_sigpipe = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe));
    }
#endif

    while (sent < size)
    {
        size_t wanted = size - sent;
        if (wanted > INT_MAX)
            wanted = INT_MAX;

#ifdef CHTTPX_ENABLE_TLS
        if (tls_session)
        {
            int result = SSL_write((SSL*)tls_session, cursor + sent, (int)wanted);
            if (result <= 0)
            {
                int error = SSL_get_error((SSL*)tls_session, result);
#ifdef CHTTPX_PLATFORM_POSIX
                if (error == SSL_ERROR_SYSCALL && errno == EINTR)
                    continue;
#endif
                return cHTTPX_ERR_TLS;
            }
            sent += (size_t)result;
            continue;
        }
#endif

        int flags = 0;
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        int result = send(fd, (const char*)cursor + sent, (int)wanted, flags);
        if (result < 0)
        {
#ifdef CHTTPX_PLATFORM_POSIX
            if (errno == EINTR)
                continue;
#endif
            return cHTTPX_ERR_IO;
        }
        if (result == 0)
            return cHTTPX_ERR_IO;
        sent += (size_t)result;
    }

    return cHTTPX_OK;
}
