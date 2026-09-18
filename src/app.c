/*
 * Copyright (c) 2026 netcorelink
 *
 * cHTTPX application runtime: independent local components plus a small
 * service registry used by cHTTPX_Call().
 */

#include "app.h"

#include "cookies.h"
#include "headers.h"
#include "http.h"
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
} chttpx_app_component_t;

typedef struct
{
    char* name;
    char* base_url;
} chttpx_app_remote_t;

static chttpx_app_component_t* components(chttpx_app_t* app)
{
    return (chttpx_app_component_t*)app->_components;
}

static chttpx_app_remote_t* remotes(chttpx_app_t* app)
{
    return (chttpx_app_remote_t*)app->_remotes;
}

static chttpx_serv_t* find_local(chttpx_app_t* app, const char* name)
{
    if (!app || !name)
        return NULL;

    chttpx_app_component_t* items = components(app);
    for (size_t i = 0; i < app->_components_count; i++)
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

    chttpx_app_remote_t* items = remotes(app);
    for (size_t i = 0; i < app->_remotes_count; i++)
    {
        if (items[i].name && strcmp(items[i].name, name) == 0)
            return &items[i];
    }

    return NULL;
}

static int ensure_component_capacity(chttpx_app_t* app)
{
    if (app->_components_count < app->_components_capacity)
        return CHTTPX_OK;

    size_t new_capacity = app->_components_capacity ? app->_components_capacity * 2 : 4;
    if (new_capacity < app->_components_capacity || new_capacity > SIZE_MAX / sizeof(chttpx_app_component_t))
        return CHTTPX_ERR_LIMIT;

    void* resized = realloc(app->_components, new_capacity * sizeof(chttpx_app_component_t));
    if (!resized)
        return CHTTPX_ERR_MEMORY;

    app->_components = resized;
    memset(components(app) + app->_components_capacity, 0,
           (new_capacity - app->_components_capacity) * sizeof(chttpx_app_component_t));
    app->_components_capacity = new_capacity;
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
    memset(remotes(app) + app->_remotes_capacity, 0, (new_capacity - app->_remotes_capacity) * sizeof(chttpx_app_remote_t));
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
    app->_network_initialized = true;
#else
    app->_network_initialized = true;
#endif

    app->_initialized = true;
    return CHTTPX_OK;
}

static chttpx_serv_t* create_local(chttpx_app_t* app, const char* name, const chttpx_config_t* config, chttpx_component_kind_t kind,
                                   bool network_enabled)
{
    if (!app || !app->_initialized || app->_started || !name || !*name || !config)
        return NULL;

    if (find_local(app, name) || find_remote(app, name))
        return NULL;

    if (ensure_component_capacity(app) != CHTTPX_OK)
        return NULL;

    chttpx_serv_t* server = calloc(1, sizeof(*server));
    if (!server)
        return NULL;

    int result = _chttpx_server_init(server, app, name, config, kind, network_enabled);
    if (result != CHTTPX_OK)
    {
        free(server);
        return NULL;
    }

    chttpx_app_component_t* item = &components(app)[app->_components_count++];
    item->server = server;
    return server;
}

chttpx_serv_t* cHTTPX_AppMicroserver(chttpx_app_t* app, const char* name, uint16_t port)
{
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = port;
    return create_local(app, name, &config, CHTTPX_COMPONENT_MICROSERVER, true);
}

chttpx_serv_t* cHTTPX_AppMicroserverWithConfig(chttpx_app_t* app, const char* name, const chttpx_config_t* config)
{
    return create_local(app, name, config, CHTTPX_COMPONENT_MICROSERVER, true);
}

chttpx_serv_t* cHTTPX_AppMicroservice(chttpx_app_t* app, const char* name)
{
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 0;
    return create_local(app, name, &config, CHTTPX_COMPONENT_MICROSERVICE, false);
}

int cHTTPX_AppRemote(chttpx_app_t* app, const char* name, const char* base_url)
{
    if (!app || !app->_initialized || app->_started || !name || !*name || !base_url || strncmp(base_url, "http://", 7) != 0)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    if (find_local(app, name) || find_remote(app, name))
        return CHTTPX_ERR_STATE;

    int result = ensure_remote_capacity(app);
    if (result != CHTTPX_OK)
        return result;

    chttpx_app_remote_t* item = &remotes(app)[app->_remotes_count];
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

    chttpx_app_component_t* items = components(app);
    for (size_t i = 0; i < app->_components_count; i++)
    {
        chttpx_serv_t* server = items[i].server;
        if (!server || !server->network_enabled)
            continue;

        if (_thread_create(&items[i].thread, app_listener, server) != 0)
        {
            for (size_t j = 0; j < i; j++)
                if (items[j].server && items[j].server->network_enabled)
                    _chttpx_server_shutdown(items[j].server);
            for (size_t j = 0; j < i; j++)
                if (items[j].thread_started)
                {
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

    chttpx_app_component_t* items = components(app);
    for (size_t i = 0; i < app->_components_count; i++)
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

    chttpx_app_component_t* items = components(app);

    for (size_t i = 0; i < app->_components_count; i++)
        if (items[i].server)
            _chttpx_server_shutdown(items[i].server);

    for (size_t i = 0; i < app->_components_count; i++)
    {
        if (items[i].thread_started)
        {
            _thread_join(items[i].thread);
            items[i].thread_started = false;
        }
        free(items[i].server);
        items[i].server = NULL;
    }

    free(app->_components);

    chttpx_app_remote_t* remote_items = remotes(app);
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

static int local_call(chttpx_request_t* source, chttpx_serv_t* target, const char* method, const char* path, const void* body, size_t body_size,
                      const char* content_type, chttpx_response_t* res)
{
    if (!source || !target || !target->initialized || !method || !path || !res || (body_size && !body))
        return CHTTPX_ERR_INVALID_ARGUMENT;

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

    internal.headers_count = source->headers_count;
    if (internal.headers_count > MAX_HEADERS)
        internal.headers_count = MAX_HEADERS;
    memcpy(internal.headers, source->headers, internal.headers_count * sizeof(chttpx_header_t));

    cHTTPX_HeaderSet(&internal, "Content-Type", internal.content_type);
    char length[32];
    snprintf(length, sizeof(length), "%zu", body_size);
    cHTTPX_HeaderSet(&internal, "Content-Length", length);

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

typedef struct
{
    char host[256];
    char port[16];
    char base_path[CHTTPX_MAX_PATH];
} remote_url_t;

static int parse_remote_url(const char* url, remote_url_t* parsed)
{
    if (!url || !parsed || strncmp(url, "http://", 7) != 0)
        return 0;

    memset(parsed, 0, sizeof(*parsed));
    const char* authority = url + 7;
    const char* slash = strchr(authority, '/');
    const char* authority_end = slash ? slash : authority + strlen(authority);
    const char* colon = NULL;

    for (const char* cursor = authority; cursor < authority_end; cursor++)
        if (*cursor == ':')
            colon = cursor;

    const char* host_end = colon ? colon : authority_end;
    size_t host_size = (size_t)(host_end - authority);
    if (host_size == 0 || host_size >= sizeof(parsed->host))
        return 0;

    memcpy(parsed->host, authority, host_size);
    parsed->host[host_size] = '\0';

    if (colon)
    {
        size_t port_size = (size_t)(authority_end - colon - 1);
        if (port_size == 0 || port_size >= sizeof(parsed->port))
            return 0;
        memcpy(parsed->port, colon + 1, port_size);
        parsed->port[port_size] = '\0';
    }
    else
    {
        snprintf(parsed->port, sizeof(parsed->port), "80");
    }

    if (slash)
        snprintf(parsed->base_path, sizeof(parsed->base_path), "%s", slash);

    return 1;
}

static chttpx_socket_t connect_remote(const remote_url_t* remote)
{
    struct addrinfo hints;
    struct addrinfo* result = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(remote->host, remote->port, &hints, &result) != 0)
    {
#ifdef CHTTPX_PLATFORM_WINDOWS
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }

#ifdef CHTTPX_PLATFORM_WINDOWS
    chttpx_socket_t connected = INVALID_SOCKET;
#else
    chttpx_socket_t connected = -1;
#endif

    for (struct addrinfo* current = result; current; current = current->ai_next)
    {
        chttpx_socket_t socket_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
#ifdef CHTTPX_PLATFORM_WINDOWS
        if (socket_fd == INVALID_SOCKET)
#else
        if (socket_fd < 0)
#endif
            continue;

        if (connect(socket_fd, current->ai_addr, (int)current->ai_addrlen) == 0)
        {
            connected = socket_fd;
            break;
        }

        chttpx_close(socket_fd);
    }

    freeaddrinfo(result);
    return connected;
}

static const char* mapped_content_type(const char* value)
{
    if (!value)
        return cHTTPX_CTYPE_OCTET;
    if (strncasecmp(value, "application/json", 16) == 0)
        return cHTTPX_CTYPE_JSON;
    if (strncasecmp(value, "text/plain", 10) == 0)
        return cHTTPX_CTYPE_TEXT;
    if (strncasecmp(value, "text/html", 9) == 0)
        return cHTTPX_CTYPE_HTML;
    return cHTTPX_CTYPE_OCTET;
}

static const char* find_response_content_type(char* headers)
{
    char* line = strstr(headers, "\r\n");
    if (!line)
        return NULL;
    line += 2;

    while (*line)
    {
        char* end = strstr(line, "\r\n");
        if (!end || end == line)
            break;

        if ((size_t)(end - line) > 13 && strncasecmp(line, "Content-Type:", 13) == 0)
        {
            char* value = line + 13;
            while (value < end && (*value == ' ' || *value == '\t'))
                value++;
            *end = '\0';
            return value;
        }

        line = end + 2;
    }

    return NULL;
}

static int remote_call(chttpx_request_t* source, const char* base_url, const char* method, const char* path, const void* body, size_t body_size,
                       const char* content_type, chttpx_response_t* res)
{
    remote_url_t remote;
    if (!parse_remote_url(base_url, &remote))
        return CHTTPX_ERR_PROTOCOL;

    chttpx_socket_t socket_fd = connect_remote(&remote);
#ifdef CHTTPX_PLATFORM_WINDOWS
    if (socket_fd == INVALID_SOCKET)
#else
    if (socket_fd < 0)
#endif
        return CHTTPX_ERR_SOCKET;

    char full_path[CHTTPX_MAX_PATH];
    if (snprintf(full_path, sizeof(full_path), "%s%s", remote.base_path, path) >= (int)sizeof(full_path))
    {
        chttpx_close(socket_fd);
        return CHTTPX_ERR_LIMIT;
    }

    const char* selected_content_type =
        content_type && *content_type ? content_type : (source->content_type[0] ? source->content_type : cHTTPX_CTYPE_JSON);
    const char* authorization = cHTTPX_HeaderGet(source, "Authorization");

    char header[8192];
    int header_size = snprintf(header, sizeof(header),
                               "%s %s HTTP/1.1\r\n"
                               "Host: %s:%s\r\n"
                               "Connection: close\r\n"
                               "Content-Type: %s\r\n"
                               "Content-Length: %zu\r\n"
                               "X-Request-ID: %s\r\n"
                               "Accept-Language: %s\r\n",
                               method, full_path, remote.host, remote.port, selected_content_type, body_size, source->request_id, source->language);
    if (header_size < 0 || (size_t)header_size >= sizeof(header))
    {
        chttpx_close(socket_fd);
        return CHTTPX_ERR_LIMIT;
    }

    size_t used = (size_t)header_size;
    if (authorization && *authorization)
    {
        int added = snprintf(header + used, sizeof(header) - used, "Authorization: %s\r\n", authorization);
        if (added < 0 || (size_t)added >= sizeof(header) - used)
        {
            chttpx_close(socket_fd);
            return CHTTPX_ERR_LIMIT;
        }
        used += (size_t)added;
    }

    if (used + 2 >= sizeof(header))
    {
        chttpx_close(socket_fd);
        return CHTTPX_ERR_LIMIT;
    }
    memcpy(header + used, "\r\n", 2);
    used += 2;

    if (cHTTPX_SendAll(socket_fd, header, used) != CHTTPX_OK || (body_size && cHTTPX_SendAll(socket_fd, body, body_size) != CHTTPX_OK))
    {
        chttpx_close(socket_fd);
        return CHTTPX_ERR_IO;
    }

    size_t limit = source->_server && source->_server->max_body_size ? source->_server->max_body_size + 64 * 1024 : 10 * 1024 * 1024;
    size_t capacity = 8192;
    if (capacity > limit)
        capacity = limit;
    char* response = malloc(capacity + 1);
    if (!response)
    {
        chttpx_close(socket_fd);
        return CHTTPX_ERR_MEMORY;
    }

    size_t total = 0;
    for (;;)
    {
        if (total == capacity)
        {
            if (capacity >= limit)
            {
                free(response);
                chttpx_close(socket_fd);
                return CHTTPX_ERR_LIMIT;
            }

            size_t next = capacity * 2;
            if (next > limit)
                next = limit;
            char* resized = realloc(response, next + 1);
            if (!resized)
            {
                free(response);
                chttpx_close(socket_fd);
                return CHTTPX_ERR_MEMORY;
            }
            response = resized;
            capacity = next;
        }

        int received = recv(socket_fd, response + total, capacity - total, 0);
        if (received < 0)
        {
#ifdef CHTTPX_PLATFORM_POSIX
            if (errno == EINTR)
                continue;
#endif
            free(response);
            chttpx_close(socket_fd);
            return CHTTPX_ERR_IO;
        }
        if (received == 0)
            break;
        total += (size_t)received;
    }

    chttpx_close(socket_fd);
    response[total] = '\0';

    int status = 0;
    if (sscanf(response, "HTTP/%*s %d", &status) != 1 || status < 100 || status > 599)
    {
        free(response);
        return CHTTPX_ERR_PROTOCOL;
    }

    char* delimiter = chttpx_memmem(response, total, "\r\n\r\n", 4);
    if (!delimiter)
    {
        free(response);
        return CHTTPX_ERR_PROTOCOL;
    }

    size_t header_bytes = (size_t)(delimiter - response) + 4;
    char* body_start = response + header_bytes;
    size_t response_body_size = total - header_bytes;

    const char* response_type = mapped_content_type(find_response_content_type(response));
    *res = cHTTPX_ResBinary((uint16_t)status, response_type, (const unsigned char*)body_start, response_body_size);
    free(response);
    return res->status ? CHTTPX_OK : CHTTPX_ERR_MEMORY;
}

int cHTTPX_CallWithBody(chttpx_request_t* req, const char* service, const char* method, const char* path, const void* body, size_t body_size,
                       const char* content_type, chttpx_response_t* res)
{
    if (!req || !req->_server || !req->_server->app || !service || !*service || !method || !path || !res || (body_size && !body))
        return CHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_app_t* app = req->_server->app;
    chttpx_serv_t* local = find_local(app, service);
    if (local)
        return local_call(req, local, method, path, body, body_size, content_type, res);

    chttpx_app_remote_t* remote = find_remote(app, service);
    if (remote)
        return remote_call(req, remote->base_url, method, path, body, body_size, content_type, res);

    return CHTTPX_ERR_NOT_FOUND;
}

int cHTTPX_Call(chttpx_request_t* req, const char* service, const char* method, const char* path, chttpx_response_t* res)
{
    if (!req)
        return CHTTPX_ERR_INVALID_ARGUMENT;

    return cHTTPX_CallWithBody(req, service, method, path, req->body, req->body_size,
                               req->content_type[0] ? req->content_type : cHTTPX_CTYPE_JSON, res);
}
