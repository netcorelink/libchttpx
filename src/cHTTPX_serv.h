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
        cHTTPX_OK = 0,
        cHTTPX_ERR_MEMORY = -1,
        cHTTPX_ERR_SOCKET = -2,
        cHTTPX_ERR_BIND = -3,
        cHTTPX_ERR_LISTEN = -4,
        cHTTPX_ERR_INVALID_ARGUMENT = -5,
        cHTTPX_ERR_LIMIT = -6,
        cHTTPX_ERR_IO = -7,
        cHTTPX_ERR_NOT_FOUND = -8,
        cHTTPX_ERR_PROTOCOL = -9,
        cHTTPX_ERR_STATE = -10,
        cHTTPX_ERR_UNAVAILABLE = -11,
        cHTTPX_ERR_TIMEOUT = -12,
        cHTTPX_ERR_TLS = -13,
        cHTTPX_ERR_COMPRESSION = -14
    } chttpx_error_t;

    /* Backward compatibility for the legacy all-uppercase result names. */
#ifndef CHTTPX_DISABLE_LEGACY_ERROR_NAMES
#define CHTTPX_OK cHTTPX_OK
#define CHTTPX_ERR_MEMORY cHTTPX_ERR_MEMORY
#define CHTTPX_ERR_SOCKET cHTTPX_ERR_SOCKET
#define CHTTPX_ERR_BIND cHTTPX_ERR_BIND
#define CHTTPX_ERR_LISTEN cHTTPX_ERR_LISTEN
#define CHTTPX_ERR_INVALID_ARGUMENT cHTTPX_ERR_INVALID_ARGUMENT
#define CHTTPX_ERR_LIMIT cHTTPX_ERR_LIMIT
#define CHTTPX_ERR_IO cHTTPX_ERR_IO
#define CHTTPX_ERR_NOT_FOUND cHTTPX_ERR_NOT_FOUND
#define CHTTPX_ERR_PROTOCOL cHTTPX_ERR_PROTOCOL
#define CHTTPX_ERR_STATE cHTTPX_ERR_STATE
#define CHTTPX_ERR_UNAVAILABLE cHTTPX_ERR_UNAVAILABLE
#define CHTTPX_ERR_TIMEOUT cHTTPX_ERR_TIMEOUT
#define CHTTPX_ERR_TLS cHTTPX_ERR_TLS
#define CHTTPX_ERR_COMPRESSION cHTTPX_ERR_COMPRESSION
#endif


    typedef enum
    {
        cHTTPX_LOG_DEBUG,
        cHTTPX_LOG_INFO,
        cHTTPX_LOG_WARN,
        cHTTPX_LOG_ERROR,
        cHTTPX_LOG_OFF
    } chttpx_log_level_t;

    typedef void (*chttpx_logger_fn)(chttpx_log_level_t level, const char* request_id, const char* message, void* user_data);

    typedef enum
    {
        cHTTPX_NETWORK_IPV4 = 0,
        cHTTPX_NETWORK_IPV6,
        cHTTPX_NETWORK_DUAL
    } chttpx_network_mode_t;

    typedef struct
    {
        bool enabled;
        const char* cert_file;
        const char* key_file;
        const char* client_ca_file;
        bool require_client_cert;
    } chttpx_tls_config_t;

    typedef struct
    {
        bool verify_peer;
        const char* ca_file;
        const char* client_cert_file;
        const char* client_key_file;
    } chttpx_tls_client_config_t;

    typedef struct
    {
        uint16_t port;
        chttpx_network_mode_t network_mode;
        chttpx_tls_config_t tls;
        size_t max_clients;
        uint16_t read_timeout_sec;
        uint16_t write_timeout_sec;
        uint16_t idle_timeout_sec;
        size_t max_body_size;
        size_t max_upload_size;
        size_t max_header_size;
        bool request_id_enabled;
        bool metrics_enabled;
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

    typedef struct chttpx_route
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
        bool compression_disabled;
    } chttpx_route_t;

    typedef struct chttpx_serv
    {
        struct chttpx_app* app;
        char* name;
        bool initialized;

        uint16_t port;
        chttpx_network_mode_t network_mode;
        chttpx_socket_t server_fd;

        chttpx_tls_config_t tls;
        void* _tls_ctx;

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
        void* compression_state;
        void* metrics_state;
        void* runtime_state;

        chttpx_cors_t cors;
    } chttpx_serv_t;

    typedef struct
    {
        chttpx_serv_t* server;
        chttpx_socket_t client_fd;
    } chttpx_client_ctx_t;

    typedef struct chttpx_router
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
