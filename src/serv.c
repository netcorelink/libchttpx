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

#include "include/serv.h"

#include "include/crosspltm.h"

/* Extern server struct data */
chttpx_serv_t *serv = NULL;

/**
 * Initialize the HTTP server.
 * @param serv_p The basic structure for working with a server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * This function must be called before registering routes or starting the server.
 */
int cHTTPX_Init(chttpx_serv_t *serv_p, unsigned short port) {
    serv = serv_p;

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        perror("WSAStartup");
        return -1;
    }
#endif

    serv->port = port;
    serv->server_fd = socket(AF_INET, SOCK_STREAM, 0);

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
    setsockopt(
        serv->server_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
#ifdef _WIN32
        (const char*)&opt,
#else
        &opt,
#endif
        sizeof(opt)
    );

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serv->server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(1);
    }

    if (listen(serv->server_fd, 128) < 0) {
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

/**
 * Register a route handler for a specific HTTP method and path.
 * @param method HTTP method string, e.g., "GET", "POST".
 * @param path URL path to match, e.g., "/users".
 * @param handler Function pointer to handle the request. The handler should return httpx_response_t.
 * This allows the server to call the appropriate function when a matching request is received.
 */
void cHTTPX_Route(const char *method, const char *path, chttpx_handler_t handler) {
    if (!serv) {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    if (serv->routes_count == serv->routes_capacity) {
        size_t new_capacity = (serv->routes_capacity == 0) ? 4 : serv->routes_capacity * 2;
        chttpx_route_t *new_routes = realloc(serv->routes, sizeof(chttpx_route_t) * new_capacity);

        if (!new_routes) {
            perror("realloc routes");
            exit(1);
        }

        serv->routes = new_routes;
        serv->routes_capacity = new_capacity;
    }

    serv->routes[serv->routes_count].method = method;
    serv->routes[serv->routes_count].path = path;
    serv->routes[serv->routes_count].handler = handler;
    serv->routes_count++;
}

/**
 * Start the server loop to listen for incoming connections.
 * This function blocks indefinitely, accepting new client connections
 * and dispatching them to cHTTPX_Handle.
 */
void cHTTPX_Listen() {
    if (!serv) {
        fprintf(stderr, "Error: server is not initialized\n");
        return;
    }

    while(1) {
        int client_fd = accept(serv->server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        cHTTPX_Handle(client_fd);
    }
}