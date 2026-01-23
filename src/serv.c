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

/* Extern server struct data */
chttpx_serv_t* serv = NULL;

/**
 * Initialize the HTTP server.
 * @param serv_p The basic structure for working with a server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * This function must be called before registering routes or starting the server.
 */
int cHTTPX_Init(chttpx_serv_t* serv_p, uint16_t port, void* max_clients)
{
    serv = serv_p;

    /* Recovery initial */
    _recovery_init();

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        perror("WSAStartup");
        return -1;
    }
#endif

    serv->port = port;
    serv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    serv->max_clients = max_clients ? *(size_t*)max_clients : MAX_CLIENTS_DEFAULT;
    serv->current_clients = 0;

#ifdef _WIN32
    if (serv->server_fd == INVALID_SOCKET)
#else
    if (serv->server_fd < 0)
#endif
    {
        perror("socket");
        exit(1);
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
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serv->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        exit(1);
    }

    if (listen(serv->server_fd, 128) < 0)
    {
        perror("listen");
        exit(1);
    }

    /* Timeouts */
    serv->read_timeout_sec = 300;
    serv->write_timeout_sec = 300;
    serv->idle_timeout_sec = 90;

    /* Default values for routes */
    serv->routes = NULL;
    serv->routes_count = 0;
    serv->routes_capacity = 0;

    printf("HTTP server started on port %d...\n", port);

    return 0;
}

/* Register a route handler for a specific HTTP method and path. */
static void route(const char *method, const char *path, chttpx_handler_t handler)
{
    if (!serv)
    {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    if (serv->routes_count == serv->routes_capacity)
    {
        size_t new_capacity = (serv->routes_capacity == 0) ? 4 : serv->routes_capacity * 2;
        chttpx_route_t* new_routes = realloc(serv->routes, sizeof(chttpx_route_t) * new_capacity);

        if (!new_routes)
        {
            perror("realloc routes");
            exit(1);
        }

        serv->routes = new_routes;
        serv->routes_capacity = new_capacity;
    }

    serv->routes[serv->routes_count].method = strdup(method);
    serv->routes[serv->routes_count].path = strdup(path);
    serv->routes[serv->routes_count].handler = handler;
    serv->routes_count++;
}

chttpx_router_t cHTTPX_RoutePathPrefix(const char* prefix)
{
    chttpx_router_t r;
    r.serv = serv;

    if (prefix && *prefix)
    {
        r.prefix = strdup(prefix);
    }
    else 
    {
        r.prefix = strdup("");
    }

    return r;
}

void cHTTPX_RegisterRoute(chttpx_router_t* r, const char *method, const char *path, chttpx_handler_t handler)
{
    if (!r || !r->serv || !method || !path || !handler) return;

    char fpath[MAX_PATH];

    if (snprintf(fpath, sizeof(fpath), "%s%s", r->prefix, path) >= (int)sizeof(fpath)) return;

    route(method, fpath, handler);
}

static void* handle_client_wrapper(void* arg)
{
    if (!serv) return NULL;

    chttpx_handle(arg);

    serv->current_clients--;
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

    while (1)
    {
        if (serv->current_clients >= serv->max_clients) continue;

        chttpx_socket_t client_fd = accept(serv->server_fd, NULL, NULL);
        if (client_fd < 0)
            continue;

        /* Inc. max clients */
        serv->current_clients++;

        /* Get client socket */
        int* client_sock = malloc(sizeof(int));
        if (!client_sock)
        {
            perror("malloc failed");
            chttpx_close(client_fd);
            continue;
        }
        *client_sock = client_fd;

        thread_t thread_id;
        _thread_create(&thread_id, handle_client_wrapper, client_sock);

#if defined(_WIN32) || defined(_WIN64)
        CloseHandle(thread_id);
#else
        pthread_detach(thread_id);
#endif
    }
}

void cHTTPX_Shutdown()
{
    if (!serv) return;

    for (size_t i = 0; i < serv->routes_count; i++)
    {
        free((char*)serv->routes[i].method);
        free((char*)serv->routes[i].path);
    }

    free(serv->routes);
    serv->routes = NULL;
    serv->routes_count = 0;
    serv->routes_capacity = 0;
#ifdef _WIN32
    chttpx_close(serv->server_fd);
#else
    chttpx_close(serv->server_fd);
#endif
    serv->server_fd = 0;

    serv = NULL;
}
