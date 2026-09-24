/*
 * Copyright (c) 2026 netcorelink
 *
 * cHTTPX application runtime for managing multiple HTTP servers.
 */

#include "cHTTPX_app.h"

#include "cHTTPX_cookies.h"
#include "cHTTPX_headers.h"
#include "cHTTPX_http.h"
#include "cHTTPX_queries.h"
#include "cHTTPX_utils.h"
#include "cHTTPX_tls.h"
#include "cHTTPX_http2.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CHTTPX_PLATFORM_POSIX
#include <fcntl.h>
#include <netdb.h>
#endif

#define CHTTPX_CALL_TIMEOUT_SEC 30

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
    chttpx_tls_client_config_t tls;
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
        return cHTTPX_OK;

    size_t new_capacity = app->_servers_capacity ? app->_servers_capacity * 2 : 4;
    if (new_capacity < app->_servers_capacity || new_capacity > SIZE_MAX / sizeof(chttpx_app_server_t))
        return cHTTPX_ERR_LIMIT;

    void* resized = realloc(app->_servers, new_capacity * sizeof(chttpx_app_server_t));
    if (!resized)
        return cHTTPX_ERR_MEMORY;

    app->_servers = resized;
    memset(app_servers(app) + app->_servers_capacity, 0,
           (new_capacity - app->_servers_capacity) * sizeof(chttpx_app_server_t));
    app->_servers_capacity = new_capacity;
    return cHTTPX_OK;
}

static int ensure_remote_capacity(chttpx_app_t* app)
{
    if (app->_remotes_count < app->_remotes_capacity)
        return cHTTPX_OK;

    size_t new_capacity = app->_remotes_capacity ? app->_remotes_capacity * 2 : 4;
    if (new_capacity < app->_remotes_capacity || new_capacity > SIZE_MAX / sizeof(chttpx_app_remote_t))
        return cHTTPX_ERR_LIMIT;

    void* resized = realloc(app->_remotes, new_capacity * sizeof(chttpx_app_remote_t));
    if (!resized)
        return cHTTPX_ERR_MEMORY;

    app->_remotes = resized;
    memset(app_remotes(app) + app->_remotes_capacity, 0,
           (new_capacity - app->_remotes_capacity) * sizeof(chttpx_app_remote_t));
    app->_remotes_capacity = new_capacity;
    return cHTTPX_OK;
}

int cHTTPX_AppInit(chttpx_app_t* app)
{
    if (!app)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    memset(app, 0, sizeof(*app));

#ifdef CHTTPX_PLATFORM_WINDOWS
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return cHTTPX_ERR_SOCKET;
#endif

    app->_network_initialized = true;
    app->_initialized = true;
    return cHTTPX_OK;
}

chttpx_serv_t* cHTTPX_AppServer(chttpx_app_t* app, const char* name, const chttpx_config_t* config)
{
    if (!app || !app->_initialized || app->_started || !name || !*name || !config)
        return NULL;

    if (find_server(app, name) || find_remote(app, name))
        return NULL;

    if (ensure_server_capacity(app) != cHTTPX_OK)
        return NULL;

    chttpx_serv_t* server = calloc(1, sizeof(*server));
    if (!server)
        return NULL;

    int result = _chttpx_server_init(server, app, name, config);
    if (result != cHTTPX_OK)
    {
        free(server);
        return NULL;
    }

    chttpx_app_server_t* item = &app_servers(app)[app->_servers_count++];
    item->server = server;
    return server;
}


chttpx_tls_client_config_t cHTTPX_DefaultTLSClientConfig(void)
{
    return (chttpx_tls_client_config_t){
        .verify_peer = true,
    };
}

static void free_remote_tls_config(chttpx_tls_client_config_t* tls)
{
    if (!tls)
        return;

    free((void*)tls->ca_file);
    free((void*)tls->client_cert_file);
    free((void*)tls->client_key_file);
    memset(tls, 0, sizeof(*tls));
}

int cHTTPX_AppRemoteEx(chttpx_app_t* app, const char* name, const char* base_url,
                       const chttpx_tls_client_config_t* tls_config)
{
    bool https = base_url && strncmp(base_url, "https://", 8) == 0;
    bool http = base_url && strncmp(base_url, "http://", 7) == 0;

    if (!app || !app->_initialized || app->_started || !name || !*name || (!http && !https))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (https && !_chttpx_tls_available())
        return cHTTPX_ERR_UNAVAILABLE;

    chttpx_tls_client_config_t selected = tls_config ? *tls_config : cHTTPX_DefaultTLSClientConfig();
    if ((selected.client_cert_file && !selected.client_key_file) ||
        (!selected.client_cert_file && selected.client_key_file))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (find_server(app, name) || find_remote(app, name))
        return cHTTPX_ERR_STATE;

    int result = ensure_remote_capacity(app);
    if (result != cHTTPX_OK)
        return result;

    chttpx_app_remote_t* item = &app_remotes(app)[app->_remotes_count];
    item->name = strdup(name);
    item->base_url = strdup(base_url);
    item->tls.verify_peer = selected.verify_peer;
    item->tls.ca_file = selected.ca_file ? strdup(selected.ca_file) : NULL;
    item->tls.client_cert_file = selected.client_cert_file ? strdup(selected.client_cert_file) : NULL;
    item->tls.client_key_file = selected.client_key_file ? strdup(selected.client_key_file) : NULL;

    if (!item->name || !item->base_url ||
        (selected.ca_file && !item->tls.ca_file) ||
        (selected.client_cert_file && !item->tls.client_cert_file) ||
        (selected.client_key_file && !item->tls.client_key_file))
    {
        free(item->name);
        free(item->base_url);
        free_remote_tls_config(&item->tls);
        memset(item, 0, sizeof(*item));
        return cHTTPX_ERR_MEMORY;
    }

    app->_remotes_count++;
    return cHTTPX_OK;
}

int cHTTPX_AppRemote(chttpx_app_t* app, const char* name, const char* base_url)
{
    chttpx_tls_client_config_t tls = cHTTPX_DefaultTLSClientConfig();
    return cHTTPX_AppRemoteEx(app, name, base_url, &tls);
}

static void* app_listener(void* arg)
{
    _chttpx_server_listen((chttpx_serv_t*)arg);
    return NULL;
}

int cHTTPX_AppStart(chttpx_app_t* app)
{
    if (!app || !app->_initialized || app->_started)
        return cHTTPX_ERR_STATE;

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

            return cHTTPX_ERR_IO;
        }

        items[i].thread_started = true;
    }

    app->_started = true;
    return cHTTPX_OK;
}

int cHTTPX_AppWait(chttpx_app_t* app)
{
    if (!app || !app->_initialized || !app->_started)
        return cHTTPX_ERR_STATE;

    chttpx_app_server_t* items = app_servers(app);

    for (size_t i = 0; i < app->_servers_count; i++)
    {
        if (!items[i].thread_started)
            continue;

        _thread_join(items[i].thread);
        items[i].thread_started = false;
    }

    app->_started = false;
    return cHTTPX_OK;
}

int cHTTPX_AppRun(chttpx_app_t* app)
{
    int result = cHTTPX_AppStart(app);
    if (result != cHTTPX_OK)
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
        free_remote_tls_config(&remote_items[i].tls);
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
    if (!res || !res->body || res->body_size == 0 || res->body_ownership == cHTTPX_BODY_OWNED)
        return cHTTPX_OK;

    unsigned char* copy = malloc(res->body_size);
    if (!copy)
        return cHTTPX_ERR_MEMORY;

    memcpy(copy, res->body, res->body_size);
    res->body = copy;
    res->body_ownership = cHTTPX_BODY_OWNED;
    return cHTTPX_OK;
}

static int local_call(chttpx_request_t* source, chttpx_serv_t* target, const char* method, const char* path, const void* body,
                      size_t body_size, const char* content_type, chttpx_response_t* res)
{
    if (!source || !target || !target->initialized || !method || !path || !res || (body_size && !body))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (__atomic_load_n(&target->current_clients, __ATOMIC_SEQ_CST) >= target->max_clients)
        return cHTTPX_ERR_LIMIT;

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
        return cHTTPX_ERR_MEMORY;
    }

    if (body_size)
    {
        internal.body = malloc(body_size + 1);
        if (!internal.body)
        {
            free_internal_request(&internal);
            return cHTTPX_ERR_MEMORY;
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
            return cHTTPX_ERR_PROTOCOL;
        }
    }

    _parse_req_cookies(&internal);
    if (internal._parse_status)
    {
        free_internal_request(&internal);
        return cHTTPX_ERR_PROTOCOL;
    }

    __atomic_fetch_add(&target->current_clients, 1, __ATOMIC_SEQ_CST);
    int result = _chttpx_dispatch(target, &internal, res);
    __atomic_fetch_sub(&target->current_clients, 1, __ATOMIC_SEQ_CST);

    if (result == cHTTPX_OK)
        result = stabilize_response(res);

    free_internal_request(&internal);
    return result;
}

typedef struct
{
    char host[256];
    char port[16];
    char base_path[CHTTPX_MAX_PATH];
    bool tls;
} chttpx_remote_url_t;

static int parse_remote_url(const char* url, chttpx_remote_url_t* parsed)
{
    if (!url || !parsed)
        return 0;

    size_t scheme_size = 0;
    bool tls = false;
    if (strncmp(url, "http://", 7) == 0)
        scheme_size = 7;
    else if (strncmp(url, "https://", 8) == 0)
    {
        scheme_size = 8;
        tls = true;
    }
    else
        return 0;

    memset(parsed, 0, sizeof(*parsed));
    parsed->tls = tls;

    const char* authority = url + scheme_size;
    const char* slash = strchr(authority, '/');
    const char* authority_end = slash ? slash : authority + strlen(authority);
    const char* port_start = NULL;
    const char* host_start = authority;
    const char* host_end = authority_end;

    if (authority < authority_end && *authority == '[')
    {
        const char* closing = memchr(authority, ']', (size_t)(authority_end - authority));
        if (!closing)
            return 0;
        host_start = authority + 1;
        host_end = closing;
        if (closing + 1 < authority_end)
        {
            if (closing[1] != ':')
                return 0;
            port_start = closing + 2;
        }
    }
    else
    {
        const char* colon = NULL;
        for (const char* cursor = authority; cursor < authority_end; cursor++)
            if (*cursor == ':')
                colon = cursor;
        if (colon)
        {
            host_end = colon;
            port_start = colon + 1;
        }
    }

    size_t host_size = (size_t)(host_end - host_start);
    if (host_size == 0 || host_size >= sizeof(parsed->host))
        return 0;

    memcpy(parsed->host, host_start, host_size);
    parsed->host[host_size] = '\0';

    if (port_start)
    {
        size_t port_size = (size_t)(authority_end - port_start);
        if (port_size == 0 || port_size >= sizeof(parsed->port))
            return 0;
        memcpy(parsed->port, port_start, port_size);
        parsed->port[port_size] = '\0';
    }
    else
        snprintf(parsed->port, sizeof(parsed->port), "%s", tls ? "443" : "80");

    if (slash)
        snprintf(parsed->base_path, sizeof(parsed->base_path), "%s", slash);

    return 1;
}

static int socket_last_error(void)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    return WSAGetLastError();
#else
    return errno;
#endif
}

static bool socket_error_is_timeout(int error)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
#else
    return error == ETIMEDOUT || error == EAGAIN || error == EWOULDBLOCK;
#endif
}

static bool socket_error_is_in_progress(int error)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return error == EINPROGRESS || error == EWOULDBLOCK;
#endif
}

static int socket_set_nonblocking(chttpx_socket_t socket_fd, bool enabled)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    u_long mode = enabled ? 1UL : 0UL;
    return ioctlsocket(socket_fd, FIONBIO, &mode) == 0 ? cHTTPX_OK : cHTTPX_ERR_UNAVAILABLE;
#else
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0)
        return cHTTPX_ERR_UNAVAILABLE;

    if (enabled)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;

    return fcntl(socket_fd, F_SETFL, flags) == 0 ? cHTTPX_OK : cHTTPX_ERR_UNAVAILABLE;
#endif
}

static int socket_set_call_timeouts(chttpx_socket_t socket_fd)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    DWORD timeout_ms = CHTTPX_CALL_TIMEOUT_SEC * 1000U;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) != 0)
        return cHTTPX_ERR_UNAVAILABLE;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) != 0)
        return cHTTPX_ERR_UNAVAILABLE;
#else
    struct timeval timeout = {.tv_sec = CHTTPX_CALL_TIMEOUT_SEC, .tv_usec = 0};
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
        return cHTTPX_ERR_UNAVAILABLE;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0)
        return cHTTPX_ERR_UNAVAILABLE;
#endif
    return cHTTPX_OK;
}

static int wait_for_connect(chttpx_socket_t socket_fd)
{
    fd_set write_set;
    fd_set error_set;
    FD_ZERO(&write_set);
    FD_ZERO(&error_set);
    FD_SET(socket_fd, &write_set);
    FD_SET(socket_fd, &error_set);

    struct timeval timeout = {.tv_sec = CHTTPX_CALL_TIMEOUT_SEC, .tv_usec = 0};

#ifdef CHTTPX_PLATFORM_WINDOWS
    int ready = select(0, NULL, &write_set, &error_set, &timeout);
#else
    int ready = select(socket_fd + 1, NULL, &write_set, &error_set, &timeout);
#endif

    if (ready == 0)
        return cHTTPX_ERR_TIMEOUT;

    if (ready < 0)
        return socket_error_is_timeout(socket_last_error()) ? cHTTPX_ERR_TIMEOUT : cHTTPX_ERR_UNAVAILABLE;

    int socket_error = 0;
#ifdef CHTTPX_PLATFORM_WINDOWS
    int error_size = sizeof(socket_error);
#else
    socklen_t error_size = sizeof(socket_error);
#endif

    if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR,
#ifdef CHTTPX_PLATFORM_WINDOWS
                   (char*)&socket_error,
#else
                   &socket_error,
#endif
                   &error_size) != 0)
        return cHTTPX_ERR_UNAVAILABLE;

    if (socket_error == 0)
        return cHTTPX_OK;

    return socket_error_is_timeout(socket_error) ? cHTTPX_ERR_TIMEOUT : cHTTPX_ERR_UNAVAILABLE;
}

static int connect_remote(const chttpx_remote_url_t* remote, chttpx_socket_t* connected)
{
    if (!remote || !connected)
        return cHTTPX_ERR_INVALID_ARGUMENT;

#ifdef CHTTPX_PLATFORM_WINDOWS
    *connected = INVALID_SOCKET;
#else
    *connected = -1;
#endif

    struct addrinfo hints;
    struct addrinfo* result = NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(remote->host, remote->port, &hints, &result) != 0)
        return cHTTPX_ERR_UNAVAILABLE;

    int final_result = cHTTPX_ERR_UNAVAILABLE;

    for (struct addrinfo* current = result; current; current = current->ai_next)
    {
        chttpx_socket_t socket_fd = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
#ifdef CHTTPX_PLATFORM_WINDOWS
        if (socket_fd == INVALID_SOCKET)
#else
        if (socket_fd < 0)
#endif
            continue;

        if (socket_set_nonblocking(socket_fd, true) != cHTTPX_OK)
        {
            chttpx_close(socket_fd);
            continue;
        }

        int connect_result = connect(socket_fd, current->ai_addr, (int)current->ai_addrlen);
        if (connect_result != 0)
        {
            int error = socket_last_error();
            if (!socket_error_is_in_progress(error))
            {
                chttpx_close(socket_fd);
                continue;
            }

            int wait_result = wait_for_connect(socket_fd);
            if (wait_result != cHTTPX_OK)
            {
                chttpx_close(socket_fd);
                final_result = wait_result;
                if (wait_result == cHTTPX_ERR_TIMEOUT)
                    break;
                continue;
            }
        }

        if (socket_set_nonblocking(socket_fd, false) != cHTTPX_OK ||
            socket_set_call_timeouts(socket_fd) != cHTTPX_OK)
        {
            chttpx_close(socket_fd);
            continue;
        }

        *connected = socket_fd;
        final_result = cHTTPX_OK;
        break;
    }

    freeaddrinfo(result);
    return final_result;
}

static const char* remote_response_content_type(const char* value)
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

static const char* find_remote_content_type(char* headers)
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

static void log_remote_tls_error(chttpx_request_t* source, const char* message)
{
    chttpx_serv_t* server = source ? source->_server : NULL;
    if (server && server->logger && server->log_level <= cHTTPX_LOG_ERROR)
        server->logger(cHTTPX_LOG_ERROR, source->request_id, message, server->logger_data);
}

static int remote_call(chttpx_request_t* source, const chttpx_app_remote_t* target, const char* method, const char* path, const void* body,
                       size_t body_size, const char* content_type, chttpx_response_t* res)
{
    if (!source || !target)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    int result = _chttpx_http2_call(source, target->base_url, &target->tls, method, path, body, body_size, content_type, res);
    if (result == cHTTPX_ERR_TLS)
        log_remote_tls_error(source, "remote HTTP/2 TLS handshake, ALPN, or certificate verification failed");
    return result;
}

int cHTTPX_CallEx(chttpx_request_t* source, const char* server_name, const char* method, const char* path,
                  const chttpx_call_options_t* options, chttpx_response_t* res)
{
    if (!source || !source->_server || !source->_server->app || !server_name || !*server_name || !method || !path || !options || !res ||
        (options->body_size && !options->body))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    const void* body = options->body;
    size_t body_size = options->body_size;
    const char* content_type = options->content_type;

    chttpx_app_t* app = source->_server->app;

    chttpx_serv_t* local = find_server(app, server_name);
    if (local)
        return local_call(source, local, method, path, body, body_size, content_type, res);

    chttpx_app_remote_t* remote = find_remote(app, server_name);
    if (remote)
        return remote_call(source, remote, method, path, body, body_size, content_type, res);

    return cHTTPX_ERR_NOT_FOUND;
}

int cHTTPX_Call(chttpx_request_t* req, const char* server_name, const char* method, const char* path, chttpx_response_t* res)
{
    if (!req)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_call_options_t options = {
        .body = req->body,
        .body_size = req->body_size,
        .content_type = req->content_type[0] ? req->content_type : cHTTPX_CTYPE_JSON,
    };

    return cHTTPX_CallEx(req, server_name, method, path, &options, res);
}
