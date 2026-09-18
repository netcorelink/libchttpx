/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef SERV_H
#define SERV_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "cors.h"
#include "response.h"
#include "middlewares.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define CHTTPX_MAX_PATH 4096
#define MAX_CLIENTS_DEFAULT 255

    typedef enum
    {
        CHTTPX_OK = 0,
        CHTTPX_ERR_MEMORY = -1,
        CHTTPX_ERR_SOCKET = -2,
        CHTTPX_ERR_BIND = -3,
        CHTTPX_ERR_LISTEN = -4,
        CHTTPX_ERR_INVALID_ARGUMENT = -5,
        CHTTPX_ERR_LIMIT = -6,
        CHTTPX_ERR_IO = -7
    } chttpx_error_t;

    typedef enum
    {
        CHTTPX_LOG_DEBUG,
        CHTTPX_LOG_INFO,
        CHTTPX_LOG_WARN,
        CHTTPX_LOG_ERROR,
        CHTTPX_LOG_OFF
    } chttpx_log_level_t;

    typedef void (*chttpx_logger_fn)(chttpx_log_level_t level, const char* request_id, const char* message, void* user_data);

    typedef struct
    {
        uint16_t port;
        size_t max_clients;
        uint16_t read_timeout_sec;
        uint16_t write_timeout_sec;
        uint16_t idle_timeout_sec;
        size_t max_body_size;
        size_t max_upload_size;
        size_t max_header_size;
        bool request_id_enabled;
        const char** languages;
        size_t languages_count;
        const char* default_language;
        chttpx_log_level_t log_level;
        chttpx_logger_fn logger;
        void* logger_data;
    } chttpx_config_t;

    typedef struct
    {
        size_t max_size;
        const char** allowed_types;
        size_t allowed_types_count;
    } chttpx_upload_policy_t;

    /* Base struct route for library */
    typedef struct
    {
        const char* method;
        const char* path;
        chttpx_handler_t handler;
        chttpx_middleware_t middlewares[MAX_MIDDLEWARES];
        size_t middleware_count;
        chttpx_middleware_t after_middlewares[MAX_MIDDLEWARES];
        size_t after_middleware_count;
        chttpx_upload_policy_t upload_policy;
        bool has_upload_policy;
    } chttpx_route_t;

    typedef struct
    {
        uint16_t port;

        chttpx_socket_t server_fd;

        size_t max_clients;
        size_t current_clients;
        volatile bool shutdown_requested;
        volatile bool listening;

        /* Server timeout params */
        uint16_t read_timeout_sec;  // 2b
        uint16_t write_timeout_sec; // 2b
        uint16_t idle_timeout_sec;  // 2b
        size_t max_body_size;
        size_t max_upload_size;
        size_t max_header_size;

        bool request_id_enabled;
        const char** languages;
        size_t languages_count;
        const char* default_language;

        /* Internally owned copies backing the public language views above. */
        char** _owned_languages;
        char* _owned_default_language;

        chttpx_log_level_t log_level;
        chttpx_logger_fn logger;
        void* logger_data;

        /* Routes params.
         * Route objects are allocated separately so pointers returned by
         * cHTTPX_Get()/Post()/... stay valid when the registry grows.
         */
        chttpx_route_t** routes;
        size_t routes_count;
        size_t routes_capacity;

        /* Middlewares */
        chttpx_middleware_stack_t middleware;

        /* Cors */
        chttpx_cors_t cors;
    } chttpx_serv_t;

    /* Structure for register routes */
    typedef struct
    {
        chttpx_serv_t* serv;
        char prefix[CHTTPX_MAX_PATH];
        chttpx_middleware_t middlewares[MAX_MIDDLEWARES];
        size_t middleware_count;
        chttpx_middleware_t after_middlewares[MAX_MIDDLEWARES];
        size_t after_middleware_count;
    } chttpx_router_t;

    extern chttpx_serv_t* serv;

    /* Internal helper used by i18n configuration to replace owned language state. */
    int _chttpx_server_set_languages(chttpx_serv_t* server, const char** languages, size_t count, const char* fallback);

    /**
     * Initialize the HTTP server.
     * @param serv_p The basic structure for working with a server.
     * @param port The TCP port on which the server will listen (e.g., 80, 8080).
     * This function must be called before registering routes or starting the server.
     */
    int cHTTPX_Init(chttpx_serv_t* serv_p, uint16_t port, void* max_clients);

    /** Return a server configuration initialized with library defaults. */
    chttpx_config_t cHTTPX_DefaultConfig(void);

    /**
     * Initialize a server with an explicit configuration.
     *
     * @return CHTTPX_OK on success, otherwise a negative error code.
 */
    int cHTTPX_InitWithConfig(chttpx_serv_t* serv_p, const chttpx_config_t* config);

    /**
     * Create a router bound to the server with a fixed path prefix.
     *
     * This function allows grouping routes under a common URL prefix
     * without creating intermediate routers.
     *
     * @param serv   Pointer to the initialized HTTP server.
     * @param prefix URL path prefix (e.g. "/api", "/api/v1").
     *
     * @return A router object bound to the server and the given path prefix.
     *
     * @note The returned router owns the prefix string internally.
     *       It should be freed with cHTTPX_RouterFree() if necessary.
     */
    chttpx_router_t cHTTPX_RoutePathPrefix(const char* prefix);

    /**
     * Register a route handler for a specific HTTP method and path.
     * @param r router struct.
     * @param method HTTP method string, e.g., "GET", "POST".
     * @param path URL path to match, e.g., "/users".
     * @param handler Function pointer to handle the request. The handler should return httpx_response_t.
     * This allows the server to call the appropriate function when a matching request is received.
     */
    void cHTTPX_RegisterRoute(chttpx_router_t* r, const char* method, const char* path, chttpx_handler_t handler);

    /** Register a GET route and return its configurable route descriptor. */
    chttpx_route_t* cHTTPX_Get(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    /** Register a POST route and return its configurable route descriptor. */
    chttpx_route_t* cHTTPX_Post(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    /** Register a PUT route and return its configurable route descriptor. */
    chttpx_route_t* cHTTPX_Put(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    /** Register a PATCH route and return its configurable route descriptor. */
    chttpx_route_t* cHTTPX_Patch(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    /** Register a DELETE route and return its configurable route descriptor. */
    chttpx_route_t* cHTTPX_Delete(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    /** Register an OPTIONS route and return its configurable route descriptor. */
    chttpx_route_t* cHTTPX_Options(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    /** Create a child router by extending the parent prefix and middleware stack. */
    chttpx_router_t cHTTPX_RouteGroup(const chttpx_router_t* parent, const char* prefix);

    /** Add middleware that runs before handlers registered through this router. */
    int cHTTPX_RouterUse(chttpx_router_t* router, chttpx_middleware_t middleware);

    /** Add middleware that runs after handlers registered through this router. */
    int cHTTPX_RouterUseAfter(chttpx_router_t* router, chttpx_middleware_t middleware);

    /** Add middleware that runs before one route handler. */
    int cHTTPX_RouteUse(chttpx_route_t* route, chttpx_middleware_t middleware);

    /** Add middleware that runs after one route handler. */
    int cHTTPX_RouteUseAfter(chttpx_route_t* route, chttpx_middleware_t middleware);

    /** Configure upload size and MIME restrictions for one route. */
    int cHTTPX_RouteUploadPolicy(chttpx_route_t* route, const chttpx_upload_policy_t* policy);

    /** Release resources owned by a router and reset it to an empty state. */
    void cHTTPX_RouterFree(chttpx_router_t* router);

    /** Configure the process-wide logging callback, context, and minimum level. */
    void cHTTPX_SetLogger(chttpx_logger_fn logger, void* user_data, chttpx_log_level_t level);

    /**
     * Start the server loop to listen for incoming connections.
     * This function blocks indefinitely, accepting new client connections
     * and dispatching them to cHTTPX_Handle.
     */
    void cHTTPX_Listen();

    /**
     * Shutdown the HTTPX server and release all resources.
     *
     * This function gracefully stops the server:
     *  - closes listening and client sockets
     *  - stops accepting new connections
     *  - releases allocated memory and internal structures
     */
    void cHTTPX_Shutdown();

#ifdef __cplusplus
    extern
}
#endif

#endif
