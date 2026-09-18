/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "serv.h"

#include "utils.h"
#include "crosspltm.h"
#include "middlewares.h"
#include "http.h"

/* Extern server struct data */
chttpx_serv_t* serv = NULL;

static void default_logger(chttpx_log_level_t level, const char* request_id, const char* message, void* user_data)
{
    (void)user_data;
    static const char* names[] = {"DEBUG", "INFO", "WARN", "ERROR", "OFF"};
    if (level < CHTTPX_LOG_DEBUG || level > CHTTPX_LOG_OFF)
        level = CHTTPX_LOG_ERROR;
    fprintf(stderr, "[%s] request_id=%s %s\n", names[level], request_id && *request_id ? request_id : "-", message ? message : "");
}

/**
 * Initialize the HTTP server.
 * @param serv_p The basic structure for working with a server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * This function must be called before registering routes or starting the server.
 */
int cHTTPX_Init(chttpx_serv_t* serv_p, uint16_t port, void* max_clients)
{
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = port;
    if (max_clients)
        config.max_clients = *(size_t*)max_clients;
    return cHTTPX_InitWithConfig(serv_p, &config);
}

chttpx_config_t cHTTPX_DefaultConfig(void)
{
    return (chttpx_config_t){.port = 8080,
                             .max_clients = MAX_CLIENTS_DEFAULT,
                             .read_timeout_sec = 30,
                             .write_timeout_sec = 30,
                             .idle_timeout_sec = 60,
                             .max_body_size = 10 * 1024 * 1024,
                             .max_upload_size = 500ULL * 1024 * 1024,
                             .max_header_size = BUFFER_SIZE - 1,
                             .request_id_enabled = true,
                             .default_language = "en",
                             .log_level = CHTTPX_LOG_INFO};
}

int cHTTPX_InitWithConfig(chttpx_serv_t* serv_p, const chttpx_config_t* config)
{
    if (!serv_p || !config || config->max_clients == 0)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    memset(serv_p, 0, sizeof(*serv_p));
    serv = serv_p;

    /* Recovery initial */
    _recovery_init();

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        perror("WSAStartup");
        serv = NULL;
        return CHTTPX_ERR_SOCKET;
    }
#endif

    serv->port = config->port;
    serv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    serv->max_clients = config->max_clients;
    serv->current_clients = 0;

#ifdef _WIN32
    if (serv->server_fd == INVALID_SOCKET)
#else
    if (serv->server_fd < 0)
#endif
    {
        serv = NULL;
        return CHTTPX_ERR_SOCKET;
    }

    int opt = 1;
    setsockopt(serv->server_fd, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
               (const char*)&opt,
#else
               &opt,
#endif
               sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config->port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serv->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        chttpx_close(serv->server_fd);
        serv = NULL;
        return CHTTPX_ERR_BIND;
    }

    if (config->port == 0)
    {
        socklen_t address_size = sizeof(addr);
        if (getsockname(serv->server_fd, (struct sockaddr*)&addr, &address_size) != 0)
        {
            chttpx_close(serv->server_fd);
            serv = NULL;
            return CHTTPX_ERR_SOCKET;
        }
        serv->port = ntohs(addr.sin_port);
    }

    if (listen(serv->server_fd, 128) < 0)
    {
        chttpx_close(serv->server_fd);
        serv = NULL;
        return CHTTPX_ERR_LISTEN;
    }

    /* Timeouts */
    serv->read_timeout_sec = config->read_timeout_sec;
    serv->write_timeout_sec = config->write_timeout_sec;
    serv->idle_timeout_sec = config->idle_timeout_sec;
    serv->max_body_size = config->max_body_size;
    serv->max_upload_size = config->max_upload_size;
    serv->max_header_size = config->max_header_size;
    serv->request_id_enabled = config->request_id_enabled;
    serv->languages = config->languages;
    serv->languages_count = config->languages_count;
    serv->default_language = config->default_language ? config->default_language : "en";
    serv->log_level = config->log_level;
    serv->logger = config->logger ? config->logger : default_logger;
    serv->logger_data = config->logger_data;

    /* Default values for routes */
    serv->routes = NULL;
    serv->routes_count = 0;
    serv->routes_capacity = 0;

    return CHTTPX_OK;
}

/* Register a route handler for a specific HTTP method and path. */
static chttpx_route_t* route(chttpx_router_t* router, const char* method, const char* path, chttpx_handler_t handler)
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return NULL;
    }

    if (serv->routes_count == serv->routes_capacity)
    {
        size_t new_capacity = (serv->routes_capacity == 0) ? 4 : serv->routes_capacity * 2;
        if (new_capacity < serv->routes_capacity || new_capacity > SIZE_MAX / sizeof(*serv->routes))
            return NULL;

        chttpx_route_t** new_routes = realloc(serv->routes, sizeof(*serv->routes) * new_capacity);
        if (!new_routes)
            return NULL;

        serv->routes = new_routes;
        serv->routes_capacity = new_capacity;
    }

    chttpx_route_t* registered = calloc(1, sizeof(*registered));
    if (!registered)
        return NULL;

    registered->method = strdup(method);
    registered->path = strdup(path);
    if (!registered->method || !registered->path)
    {
        free((char*)registered->method);
        free((char*)registered->path);
        free(registered);
        return NULL;
    }
    registered->handler = handler;
    registered->middleware_count = router->middleware_count;
    memcpy(registered->middlewares, router->middlewares, sizeof(chttpx_middleware_t) * router->middleware_count);
    registered->after_middleware_count = router->after_middleware_count;
    memcpy(registered->after_middlewares, router->after_middlewares, sizeof(chttpx_middleware_t) * router->after_middleware_count);
    serv->routes[serv->routes_count++] = registered;
    return registered;
}

chttpx_router_t cHTTPX_RoutePathPrefix(const char* prefix)
{
    chttpx_router_t r;
    memset(&r, 0, sizeof(r));
    r.serv = serv;
    snprintf(r.prefix, sizeof(r.prefix), "%s", prefix ? prefix : "");

    return r;
}

void cHTTPX_RegisterRoute(chttpx_router_t* r, const char* method, const char* path, chttpx_handler_t handler)
{
    if (!r || !r->serv || !method || !path || !handler)
        return;

    char fpath[CHTTPX_MAX_PATH];

    if (snprintf(fpath, sizeof(fpath), "%s%s", r->prefix, path) >= (int)sizeof(fpath))
        return;

    route(r, method, fpath, handler);
}

static chttpx_route_t* register_route(chttpx_router_t* router, const char* method, const char* path, chttpx_handler_t handler)
{
    if (!router || !router->serv || !method || !path || !handler)
        return NULL;
    char full_path[CHTTPX_MAX_PATH];
    if (snprintf(full_path, sizeof(full_path), "%s%s", router->prefix, path) >= (int)sizeof(full_path))
        return NULL;
    return route(router, method, full_path, handler);
}

#define CHTTPX_ROUTE_HELPER(name, method)                                                                                                            \
    chttpx_route_t* name(chttpx_router_t* router, const char* path, chttpx_handler_t handler)                                                        \
    {                                                                                                                                                \
        return register_route(router, method, path, handler);                                                                                        \
    }

CHTTPX_ROUTE_HELPER(cHTTPX_Get, cHTTPX_MethodGet)
CHTTPX_ROUTE_HELPER(cHTTPX_Post, cHTTPX_MethodPost)
CHTTPX_ROUTE_HELPER(cHTTPX_Put, cHTTPX_MethodPut)
CHTTPX_ROUTE_HELPER(cHTTPX_Patch, cHTTPX_MethodPatch)
CHTTPX_ROUTE_HELPER(cHTTPX_Delete, cHTTPX_MethodDelete)
CHTTPX_ROUTE_HELPER(cHTTPX_Options, cHTTPX_MethodOptions)

chttpx_router_t cHTTPX_RouteGroup(const chttpx_router_t* parent, const char* prefix)
{
    chttpx_router_t group;
    memset(&group, 0, sizeof(group));
    if (!parent)
        return group;
    group.serv = parent->serv;
    snprintf(group.prefix, sizeof(group.prefix), "%s%s", parent->prefix, prefix ? prefix : "");
    group.middleware_count = parent->middleware_count;
    memcpy(group.middlewares, parent->middlewares, sizeof(chttpx_middleware_t) * parent->middleware_count);
    group.after_middleware_count = parent->after_middleware_count;
    memcpy(group.after_middlewares, parent->after_middlewares, sizeof(chttpx_middleware_t) * parent->after_middleware_count);
    return group;
}

int cHTTPX_RouterUse(chttpx_router_t* router, chttpx_middleware_t middleware)
{
    if (!router || !middleware || router->middleware_count >= MAX_MIDDLEWARES)
        return CHTTPX_ERR_INVALID_ARGUMENT;
    router->middlewares[router->middleware_count++] = middleware;
    return CHTTPX_OK;
}

int cHTTPX_RouterUseAfter(chttpx_router_t* router, chttpx_middleware_t middleware)
{
    if (!router || !middleware || router->after_middleware_count >= MAX_MIDDLEWARES)
        return CHTTPX_ERR_INVALID_ARGUMENT;
    router->after_middlewares[router->after_middleware_count++] = middleware;
    return CHTTPX_OK;
}

int cHTTPX_RouteUse(chttpx_route_t* registered, chttpx_middleware_t middleware)
{
    if (!registered || !middleware || registered->middleware_count >= MAX_MIDDLEWARES)
        return CHTTPX_ERR_INVALID_ARGUMENT;
    registered->middlewares[registered->middleware_count++] = middleware;
    return CHTTPX_OK;
}

int cHTTPX_RouteUseAfter(chttpx_route_t* registered, chttpx_middleware_t middleware)
{
    if (!registered || !middleware || registered->after_middleware_count >= MAX_MIDDLEWARES)
        return CHTTPX_ERR_INVALID_ARGUMENT;
    registered->after_middlewares[registered->after_middleware_count++] = middleware;
    return CHTTPX_OK;
}

int cHTTPX_RouteUploadPolicy(chttpx_route_t* registered, const chttpx_upload_policy_t* policy)
{
    if (!registered || !policy)
        return CHTTPX_ERR_INVALID_ARGUMENT;
    registered->upload_policy = *policy;
    registered->has_upload_policy = true;
    return CHTTPX_OK;
}

void cHTTPX_RouterFree(chttpx_router_t* router)
{
    if (router)
        memset(router, 0, sizeof(*router));
}

void cHTTPX_SetLogger(chttpx_logger_fn logger, void* user_data, chttpx_log_level_t level)
{
    if (!serv)
        return;
    serv->logger = logger;
    serv->logger_data = user_data;
    serv->log_level = level;
}

static void* handle_client_wrapper(void* arg)
{
    if (!serv)
        return NULL;

    chttpx_handle(arg);

    __atomic_fetch_sub(&serv->current_clients, 1, __ATOMIC_SEQ_CST);
    return NULL;
}

/**
 * Start the server loop to listen for incoming connections.
 * This function blocks indefinitely, accepting new client connections
 * and dispatching them to cHTTPX_Handle.
 */
void cHTTPX_Listen()
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    serv->listening = true;
    while (!serv->shutdown_requested)
    {
        chttpx_socket_t client_fd = accept(serv->server_fd, NULL, NULL);
#ifdef CHTTPX_PLATFORM_WINDOWS
        if (client_fd == INVALID_SOCKET)
#else
        if (client_fd < 0)
#endif
        {
            if (serv->shutdown_requested)
                break;
            continue;
        }

        if (__atomic_load_n(&serv->current_clients, __ATOMIC_SEQ_CST) >= serv->max_clients)
        {
            static const char busy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            cHTTPX_SendAll(client_fd, busy, sizeof(busy) - 1);
            chttpx_close(client_fd);
            continue;
        }

        /* Inc. max clients */
        __atomic_fetch_add(&serv->current_clients, 1, __ATOMIC_SEQ_CST);

        /* Get client socket */
        chttpx_socket_t* client_sock = malloc(sizeof(*client_sock));
        if (!client_sock)
        {
            perror("malloc failed");
            chttpx_close(client_fd);
            __atomic_fetch_sub(&serv->current_clients, 1, __ATOMIC_SEQ_CST);
            continue;
        }
        *client_sock = client_fd;

        thread_t thread_id;
        if (_thread_create(&thread_id, handle_client_wrapper, client_sock) != 0)
        {
            free(client_sock);
            chttpx_close(client_fd);
            __atomic_fetch_sub(&serv->current_clients, 1, __ATOMIC_SEQ_CST);
            continue;
        }

#if defined(_WIN32) || defined(_WIN64)
        CloseHandle(thread_id);
#else
        pthread_detach(thread_id);
#endif
    }
    serv->listening = false;
}

void cHTTPX_Shutdown()
{
    if (!serv)
        return;

    serv->shutdown_requested = true;
#ifdef CHTTPX_PLATFORM_WINDOWS
    shutdown(serv->server_fd, SD_BOTH);
#else
    shutdown(serv->server_fd, SHUT_RDWR);
#endif
    chttpx_close(serv->server_fd);
    serv->server_fd = 0;

    while (serv->listening)
    {
#ifdef CHTTPX_PLATFORM_WINDOWS
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    while (__atomic_load_n(&serv->current_clients, __ATOMIC_SEQ_CST) > 0)
    {
#ifdef CHTTPX_PLATFORM_WINDOWS
        Sleep(10);
#else
        usleep(10000);
#endif
    }

    for (size_t i = 0; i < serv->routes_count; i++)
    {
        chttpx_route_t* registered = serv->routes[i];
        if (!registered)
            continue;
        free((char*)registered->method);
        free((char*)registered->path);
        free(registered);
    }

    free(serv->routes);
    serv->routes = NULL;
    serv->routes_count = 0;
    serv->routes_capacity = 0;
    for (size_t i = 0; i < serv->cors.origins_count; i++)
        free((void*)serv->cors.origins[i]);
    free((void*)serv->cors.origins);
    free((void*)serv->cors.methods);
    free((void*)serv->cors.headers);
    memset(&serv->cors, 0, sizeof(serv->cors));
#ifdef CHTTPX_PLATFORM_WINDOWS
    WSACleanup();
#endif
    serv = NULL;
}
