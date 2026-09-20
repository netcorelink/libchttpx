/**
 * Copyright (c) 2026 netcorelink
 *
 * Application runtime for managing multiple independent HTTP servers.
 */

#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cHTTPX_serv.h"

#include <stdbool.h>
#include <stddef.h>

    typedef struct chttpx_app
    {
        void* _servers;
        size_t _servers_count;
        size_t _servers_capacity;

        void* _remotes;
        size_t _remotes_count;
        size_t _remotes_capacity;

        bool _initialized;
        bool _started;
        bool _network_initialized;
    } chttpx_app_t;

    /** Initialize an application runtime. */
    int cHTTPX_AppInit(chttpx_app_t* app);

    /** Create an App-managed HTTP server using the supplied configuration. */
    chttpx_serv_t* cHTTPX_AppServer(chttpx_app_t* app, const char* name, const chttpx_config_t* config);

    /** Return client TLS defaults (peer verification enabled, system trust store). */
    chttpx_tls_client_config_t cHTTPX_DefaultTLSClientConfig(void);

    /**
     * Register a server that lives in another process/container.
     *
     * Both http:// and https:// are accepted. HTTPS uses certificate
     * verification by default.
     */
    int cHTTPX_AppRemote(chttpx_app_t* app, const char* name, const char* base_url);

    /**
     * Register a remote server with explicit TLS client settings.
     *
     * ca_file == NULL uses the system trust store. Client certificate/key
     * fields are optional and enable mutual TLS when both are provided.
     */
    int cHTTPX_AppRemoteEx(chttpx_app_t* app, const char* name, const char* base_url,
                           const chttpx_tls_client_config_t* tls_config);

    /** Start every local server in the App in its own listener thread. */
    int cHTTPX_AppStart(chttpx_app_t* app);

    /** Wait until all started server listener threads exit. */
    int cHTTPX_AppWait(chttpx_app_t* app);

    /** Convenience helper equivalent to AppStart followed by AppWait. */
    int cHTTPX_AppRun(chttpx_app_t* app);

    /** Stop every server and release all App-owned resources. */
    void cHTTPX_AppShutdown(chttpx_app_t* app);

    /**
     * Call another server by name.
     *
     * Local AppServer targets are dispatched directly without opening another
     * TCP connection. AppRemote targets are called over HTTP or HTTPS with a
     * fixed 30-second connect/send/receive timeout.
     *
     * The current body, content type, request id, language and request headers
     * are inherited from req.
     */
    int cHTTPX_Call(chttpx_request_t* req, const char* server_name, const char* method, const char* path, chttpx_response_t* res);

    typedef struct
    {
        const void* body;
        size_t body_size;
        const char* content_type;
    } chttpx_call_options_t;

    /**
     * Extended call variant.
     *
     * Use options to override the body/content type inherited by cHTTPX_Call().
     * More call-level customization can be added to this structure later
     * without introducing separate call functions.
     */
    int cHTTPX_CallEx(chttpx_request_t* req, const char* server_name, const char* method, const char* path,
                      const chttpx_call_options_t* options, chttpx_response_t* res);

#ifdef __cplusplus
}
#endif

#endif
