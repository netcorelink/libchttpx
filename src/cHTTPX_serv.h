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

#include "cHTTPX_cors.h"
#include "cHTTPX_response.h"
#include "cHTTPX_middlewares.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define CHTTPX_MAX_PATH 4096
#define MAX_CLIENTS_DEFAULT 255

    struct chttpx_app;

    typedef enum
    {
        CHTTPX_OK = 0,
        CHTTPX_ERR_MEMORY = -1,
        CHTTPX_ERR_SOCKET = -2,
        CHTTPX_ERR_BIND = -3,
        CHTTPX_ERR_LISTEN = -4,
        CHTTPX_ERR_INVALID_ARGUMENT = -5,
        CHTTPX_ERR_LIMIT = -6,
        CHTTPX_ERR_IO = -7,
        CHTTPX_ERR_NOT_FOUND = -8,
        CHTTPX_ERR_PROTOCOL = -9,
        CHTTPX_ERR_STATE = -10,
        CHTTPX_ERR_UNAVAILABLE = -11,
        CHTTPX_ERR_TIMEOUT = -12
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

    typedef enum
    {
        CHTTPX_NETWORK_IPV4 = 0,
        CHTTPX_NETWORK_IPV6,
        CHTTPX_NETWORK_DUAL
    } chttpx_network_mode_t;

    typedef struct
    {
        uint16_t port;
        chttpx_network_mode_t network_mode;
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

    typedef struct chttpx_serv
    {
        struct chttpx_app* app;
        char* name;
        bool initialized;

        uint16_t port;
        chttpx_network_mode_t network_mode;
        chttpx_socket_t server_fd;

        size_t max_clients;
        size_t current_clients;
        volatile bool shutdown_requested;
        volatile bool listening;

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

        chttpx_route_t** routes;
        size_t routes_count;
        size_t routes_capacity;

        chttpx_middleware_stack_t middleware;
        bool logging_enabled;
        void* rate_limiter_state;

        chttpx_cors_t cors;
    } chttpx_serv_t;

    typedef struct
    {
        chttpx_serv_t* server;
        chttpx_socket_t client_fd;
    } chttpx_client_ctx_t;

    typedef struct
    {
        chttpx_serv_t* serv;
        char prefix[CHTTPX_MAX_PATH];
        chttpx_middleware_t middlewares[MAX_MIDDLEWARES];
        size_t middleware_count;
        chttpx_middleware_t after_middlewares[MAX_MIDDLEWARES];
        size_t after_middleware_count;
    } chttpx_router_t;

    /** Return a server configuration initialized with library defaults. */
    chttpx_config_t cHTTPX_DefaultConfig(void);

    /** Create a router bound to one App-managed server. */
    chttpx_router_t cHTTPX_RoutePathPrefix(chttpx_serv_t* server, const char* prefix);

    void cHTTPX_RegisterRoute(chttpx_router_t* r, const char* method, const char* path, chttpx_handler_t handler);

    chttpx_route_t* cHTTPX_Get(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Post(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Put(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Patch(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Delete(chttpx_router_t* router, const char* path, chttpx_handler_t handler);
    chttpx_route_t* cHTTPX_Options(chttpx_router_t* router, const char* path, chttpx_handler_t handler);

    chttpx_router_t cHTTPX_RouteGroup(const chttpx_router_t* parent, const char* prefix);

    int cHTTPX_RouterUse(chttpx_router_t* router, chttpx_middleware_t middleware);
    int cHTTPX_RouterUseAfter(chttpx_router_t* router, chttpx_middleware_t middleware);
    int cHTTPX_RouteUse(chttpx_route_t* route, chttpx_middleware_t middleware);
    int cHTTPX_RouteUseAfter(chttpx_route_t* route, chttpx_middleware_t middleware);
    int cHTTPX_RouteUploadPolicy(chttpx_route_t* route, const chttpx_upload_policy_t* policy);
    void cHTTPX_RouterFree(chttpx_router_t* router);

    /** Configure logging for one App-managed component. */
    void cHTTPX_SetLogger(chttpx_serv_t* server, chttpx_logger_fn logger, void* user_data, chttpx_log_level_t level);

    /* Internal App/runtime helpers. */
    int _chttpx_server_set_languages(chttpx_serv_t* server, const char** languages, size_t count, const char* fallback);
    int _chttpx_server_init(chttpx_serv_t* server, struct chttpx_app* app, const char* name, const chttpx_config_t* config);
    void _chttpx_server_listen(chttpx_serv_t* server);
    void _chttpx_server_shutdown(chttpx_serv_t* server);

#ifdef __cplusplus
}
#endif

#endif
