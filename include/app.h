/**
 * Copyright (c) 2026 netcorelink
 *
 * Application runtime for composing independent cHTTPX microservers and
 * in-process microservices behind one service registry.
 */

#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "serv.h"

#include <stdbool.h>
#include <stddef.h>

    typedef struct chttpx_app
    {
        void* _components;
        size_t _components_count;
        size_t _components_capacity;

        void* _remotes;
        size_t _remotes_count;
        size_t _remotes_capacity;

        bool _initialized;
        bool _started;
        bool _network_initialized;
    } chttpx_app_t;

    /**
     * Initialize an application runtime.
     *
     * A cHTTPX application owns all local microservers, local microservices and
     * remote service registrations. Server creation is intentionally available
     * only through this object.
     */
    int cHTTPX_AppInit(chttpx_app_t* app);

    /**
     * Create a network-facing microserver using the default configuration.
     */
    chttpx_serv_t* cHTTPX_AppMicroserver(chttpx_app_t* app, const char* name, uint16_t port);

    /**
     * Create a network-facing microserver using an explicit configuration.
     */
    chttpx_serv_t* cHTTPX_AppMicroserverWithConfig(chttpx_app_t* app, const char* name, const chttpx_config_t* config);

    /**
     * Create an isolated in-process microservice.
     *
     * A microservice has its own routes and middleware but no listener. It is
     * addressed through cHTTPX_Call() and keeps running independently from the
     * listener state of other microservers in the same application.
     */
    chttpx_serv_t* cHTTPX_AppMicroservice(chttpx_app_t* app, const char* name);

    /**
     * Register a service that lives in another process/container.
     *
     * Only plain HTTP base URLs are supported by the built-in transport for now,
     * e.g. http://payments:8090.
     */
    int cHTTPX_AppRemote(chttpx_app_t* app, const char* name, const char* base_url);

    /** Start all local microserver listeners in their own threads. */
    int cHTTPX_AppStart(chttpx_app_t* app);

    /** Wait until all started microserver listener threads exit. */
    int cHTTPX_AppWait(chttpx_app_t* app);

    /** Convenience helper equivalent to AppStart followed by AppWait. */
    int cHTTPX_AppRun(chttpx_app_t* app);

    /**
     * Stop all microservers and release the application, its microservices and
     * remote registrations.
     */
    void cHTTPX_AppShutdown(chttpx_app_t* app);

    /**
     * Call a local microserver/microservice or a registered remote service.
     *
     * The body, content type, request id, language and request headers are
     * inherited from req. Local targets are dispatched directly without TCP;
     * remote targets are called over HTTP.
     */
    int cHTTPX_Call(chttpx_request_t* req, const char* service, const char* method, const char* path, chttpx_response_t* res);

    /**
     * Same as cHTTPX_Call(), but replaces the inherited request body.
     */
    int cHTTPX_CallWithBody(chttpx_request_t* req, const char* service, const char* method, const char* path, const void* body, size_t body_size,
                           const char* content_type, chttpx_response_t* res);

#ifdef __cplusplus
}
#endif

#endif
