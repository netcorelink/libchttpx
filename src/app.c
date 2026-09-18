/*
 * Copyright (c) 2026 netcorelink
 *
 * cHTTPX application runtime for managing multiple HTTP servers.
 */

#include "app.h"

#include "cookies.h"
#include "headers.h"
#include "queries.h"
#include "utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CHTTPX_PLATFORM_POSIX
#include <netdb.h>
#endif

typedef struct
{
    chttpx_serv_t* server;
    thread_t thread;
    bool thread_started;
} chttpx_app_server_t;

typedef struct
{
    char* name;
    char* base_url;
} chttpx_app_remote_t;

static chttpx_app_server_t* app_servers(chttpx_app_t* app)
{
    return (chttpx_app_server_t*)app->_servers;
}

static chttpx_app_remote_t* app_remotes(chttpx_app_t* app)
{
    return (chttpx_app_remote_t*)app->_remotes;
}

static chttpx_serv_t* find_server(chttpx_app_t* app, const char* name)
{
    if (!app || !name)
        return NULL;

    chttpx_app_server_t* items = app_servers(app);
    for (size_t i = 0; i < app->_servers_count; i++)
    {
        if (items[i].server && items[i].server->name && strcmp(items[i].server->name, name) == 0)
            return items[i].server;
    }

    return NULL;
}

static chttpx_app_remote_t* find_remote(chttpx_app_t* app, const char* name)
{
    if (!app || !name)
        return NULL;

    chttpx_app_remote_t* items = app_remotes(app);
    for (size_t i = 0; i < app->_remotes_count; i++)
    {
        if (items[i].name && strcmp(items[i].name, name) == 0)
            return &items[i];
    }

    return NULL;
}

static int ensure_server_capacity(chttpx_app_t* app)
{
    if (app->_servers_count < app->_servers_capacity)
        return CHTTPX_OK;

    size_t new_capacity = app->_servers_capacity ? app->_servers_capacity * 2 : 4;
    if (new_capacity < app->_servers_capacity || new_capacity > SIZE_MAX / sizeof(chttpx_app_server_t))
        return CHTTPX_ERR_LIMIT;

    void* resized = realloc(app->_servers, new_capacity * sizeof(chttpx_app_server_t));
    if (!resized)
        return CHTTPX_ERR_MEMORY;

    app->_servers = resized;
    memset(app_servers(app) + app->_servers_capacity, 0,
           (new_capacity - app->_servers_capacity) * sizeof(chttpx_app_server_t));
    app->_servers_capacity = new_capacity;
    return CHTTPX_OK;
}

static int ensure_remote_capacity(chttpx_app_t* app)
{
    if (app->_remotes_count < app->_remotes_capacity)
        return CHTTPX_OK;

    size_t new_capacity = app->_remotes_capacity ? app->_remotes_capacity * 2 : 4;
    if (new_capacity < app->_remotes_capacity || new_capacity > SIZE_MAX / sizeof(chttpx_app_remote_t))
        return CHTTPX_ERR_LIMIT;

    void* resized = realloc(app->_remotes, new_capacity * sizeof(chttpx_app_remote_t));
    if (!resized)
        return CHTTPX_ERR_MEMORY;

    app->_remotes = resized;
    memset(app_remotes(app) + app->_remotes_capacity, 0,
           (new_capacity - app->_remotes_capacity) * sizeof(chttpx_app_remote_t));
    app->_remotes_capacity = new_capacity;
    return CHTTPX_OK;
}

int cHTTPX_AppInit(chttpx_app_t* app)
{
    if (!app)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    memset(app, 0, sizeof(*app));

#ifdef CHTTPX_PLATFORM_WINDOWS
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return CHTTPX_ERR_SOCKET;
#endif

    app->_network_initialized = true;
    app->_initialized = true;
    return CHTTPX_OK;
}

chttpx_serv_t* cHTTPX_AppServerWithConfig(chttpx_app_t* app, const char* name, const chttpx_config_t* config)
{
    if (!app || !app->_initialized || app->_started || !name || !*name || !config)
        return NULL;

    if (find_server(app, name) || find_remote(app, name))
        return NULL;

    if (ensure_server_capacity(app) != CHTTPX_OK)
        return NULL;

    chttpx_serv_t* server = calloc(1, sizeof(*server));
    if (!server)
        return NULL;

    int result = _chttpx_server_init(server, app, name, config);
    if (result != CHTTPX_OK)
    {
        free(server);
        return NULL;
    }

    chttpx_app_server_t* item = &app_servers(app)[app->_servers_count++];
    item->server = server;
    return server;
}

chttpx_serv_t* cHTTPX_AppServer(chttpx_app_t* app, const char* name, uint16_t port)
{
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = port;
    return cHTTPX_AppServerWithConfig(app, name, &config);
}

int cHTTPX_AppRemote(chttpx_app_t* app, const char* name, const char* base_url)
{
    if (!app || !app->_initialized || app->_started || !name || !*name || !base_url ||
        strncmp(base_url, "http://", 7) != 0)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    if (find_server(app, name) || find_remote(app, name))
        return CHTTPX_ERR_STATE;

    int result = ensure_remote_capacity(app);
    if (result != CHTTPX_OK)
        return result;

    chttpx_app_remote_t* item = &app_remotes(app)[app->_remotes_count];
    item->name = strdup(name);
    item->base_url = strdup(base_url);

    if (!item->name || !item->base_url)
    {
        free(item->name);
        free(item->base_url);
        memset(item, 0, sizeof(*item));
        return CHTTPX_ERR_MEMORY;
    }

    app->_remotes_count++;
    return CHTTPX_OK;
}

static void* app_listener(void* arg)
{
    _chttpx_server_listen((chttpx_serv_t*)arg);
    return NULL;
}

int cHTTPX_AppStart(chttpx_app_t* app)
{
    if (!app || !app->_initialized || app->_started)
        return CHTTPX_ERR_STATE;

    chttpx_app_server_t* items = app_servers(app);

    for (size_t i = 0; i < app->_servers_count; i++)
    {
        if (!items[i].server)
            continue;

        if (_thread_create(&items[i].thread, app_listener, items[i].server) != 0)
        {
            for (size_t j = 0; j < i; j++)
                if (items[j].server)
                    _chttpx_server_shutdown(items[j].server);

            for (size_t j = 0; j < i; j++)
            {
                if (!items[j].thread_started)
                    continue;
                _thread_join(items[j].thread);
                items[j].thread_started = false;
            }

            return CHTTPX_ERR_IO;
        }

        items[i].thread_started = true;
    }

    app->_started = true;
    return CHTTPX_OK;
}

int cHTTPX_AppWait(chttpx_app_t* app)
{
    if (!app || !app->_initialized || !app->_started)
        return CHTTPX_ERR_STATE;

    chttpx_app_server_t* items = app_servers(app);

    for (size_t i = 0; i < app->_servers_count; i++)
    {
        if (!items[i].thread_started)
            continue;

        _thread_join(items[i].thread);
        items[i].thread_started = false;
    }

    app->_started = false;
    return CHTTPX_OK;
}

int cHTTPX_AppRun(chttpx_app_t* app)
{
    int result = cHTTPX_AppStart(app);
    if (result != CHTTPX_OK)
        return result;

    return cHTTPX_AppWait(app);
}

void cHTTPX_AppShutdown(chttpx_app_t* app)
{
    if (!app || !app->_initialized)
        return;

    chttpx_app_server_t* items = app_servers(app);

    for (size_t i = 0; i < app->_servers_count; i++)
    {
        if (items[i].server)
            _chttpx_server_shutdown(items[i].server);
    }

    for (size_t i = 0; i < app->_servers_count; i++)
    {
        if (items[i].thread_started)
        {
            _thread_join(items[i].thread);
            items[i].thread_started = false;
        }

        free(items[i].server);
        items[i].server = NULL;
    }

    free(app->_servers);

    chttpx_app_remote_t* remote_items = app_remotes(app);
    for (size_t i = 0; i < app->_remotes_count; i++)
    {
        free(remote_items[i].name);
        free(remote_items[i].base_url);
    }
    free(app->_remotes);

#ifdef CHTTPX_PLATFORM_WINDOWS
    if (app->_network_initialized)
        WSACleanup();
#endif

    memset(app, 0, sizeof(*app));
}

static void free_internal_request(chttpx_request_t* req)
{
    if (!req)
        return;

    cHTTPX_RequestCleanup(req);
    chttpx_free_req_cookie(req);

    free(req->method);
    free(req->path);
    free(req->body);

    for (size_t i = 0; i < req->query_count; i++)
    {
        free(req->query[i].name);
        free(req->query[i].value);
    }

    free(req->query);
}

static int stabilize_response(chttpx_response_t* res)
{
    if (!res || !res->body || res->body_size == 0 || res->body_ownership == CHTTPX_BODY_OWNED)
        return CHTTPX_OK;

    unsigned char* copy = malloc(res->body_size);
    if (!copy)
        return CHTTPX_ERR_MEMORY;

    memcpy(copy, res->body, res->body_size);
    res->body = copy;
    res->body_ownership = CHTTPX_BODY_OWNED;
    return CHTTPX_OK;
}

int cHTTPX_CallWithBody(chttpx_request_t* source, const char* server_name, const char* method, const char* path, const void* body,
                       size_t body_size, const char* content_type, chttpx_response_t* res)
{
    if (!source || !source->_server || !source->_server->app || !server_name || !*server_name || !method || !path || !res ||
        (body_size && !body))
        return CHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_serv_t* target = find_server(source->_server->app, server_name);
    if (!target || !target->initialized)
        return CHTTPX_ERR_NOT_FOUND;

    if (__atomic_load_n(&target->current_clients, __ATOMIC_SEQ_CST) >= target->max_clients)
        return CHTTPX_ERR_LIMIT;

    chttpx_request_t internal = {0};
    internal._server = target;
#ifdef CHTTPX_PLATFORM_WINDOWS
    internal.client_fd = INVALID_SOCKET;
#else
    internal.client_fd = -1;
#endif

    internal.method = strdup(method);
    internal.path = strdup(path);
    if (!internal.method || !internal.path)
    {
        free_internal_request(&internal);
        return CHTTPX_ERR_MEMORY;
    }

    if (body_size)
    {
        internal.body = malloc(body_size + 1);
        if (!internal.body)
        {
            free_internal_request(&internal);
            return CHTTPX_ERR_MEMORY;
        }

        memcpy(internal.body, body, body_size);
        internal.body[body_size] = '\0';
        internal.body_size = body_size;
        internal.content_length = body_size;
    }

    snprintf(internal.content_type, sizeof(internal.content_type), "%s",
             content_type && *content_type ? content_type : (source->content_type[0] ? source->content_type : cHTTPX_CTYPE_JSON));
    snprintf(internal.request_id, sizeof(internal.request_id), "%s", source->request_id);
    snprintf(internal.language, sizeof(internal.language), "%s", source->language);
    snprintf(internal.client_ip, sizeof(internal.client_ip), "%s", source->client_ip);
    snprintf(internal.user_agent, sizeof(internal.user_agent), "%s", source->user_agent);
    snprintf(internal.protocol, sizeof(internal.protocol), "INTERNAL/1.0");

    internal.headers_count = source->headers_count > MAX_HEADERS ? MAX_HEADERS : source->headers_count;
    memcpy(internal.headers, source->headers, internal.headers_count * sizeof(chttpx_header_t));

    cHTTPX_HeaderSet(&internal, "Content-Type", internal.content_type);

    char content_length[32];
    snprintf(content_length, sizeof(content_length), "%zu", body_size);
    cHTTPX_HeaderSet(&internal, "Content-Length", content_length);

    char* query = strchr(internal.path, '?');
    if (query)
    {
        *query++ = '\0';
        _parse_req_query(&internal, query);
        if (internal._parse_status)
        {
            free_internal_request(&internal);
            return CHTTPX_ERR_PROTOCOL;
        }
    }

    _parse_req_cookies(&internal);
    if (internal._parse_status)
    {
        free_internal_request(&internal);
        return CHTTPX_ERR_PROTOCOL;
    }

    __atomic_fetch_add(&target->current_clients, 1, __ATOMIC_SEQ_CST);
    int result = _chttpx_dispatch(target, &internal, res);
    __atomic_fetch_sub(&target->current_clients, 1, __ATOMIC_SEQ_CST);

    if (result == CHTTPX_OK)
        result = stabilize_response(res);

    free_internal_request(&internal);
    return result;
}

int cHTTPX_Call(chttpx_request_t* req, const char* server_name, const char* method, const char* path, chttpx_response_t* res)
{
    if (!req)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    return cHTTPX_CallWithBody(req, server_name, method, path, req->body, req->body_size,
                               req->content_type[0] ? req->content_type : cHTTPX_CTYPE_JSON, res);
}
