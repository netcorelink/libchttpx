/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef SERV_H
#define SERV_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cors.h"
#include "response.h"
#include "middlewares.h"

#include <stdio.h>
#include <stdint.h>

#define MAX_PATH 4096
#define MAX_CLIENTS_DEFAULT 255

/* Base struct route for library */
typedef struct {
    const char* method;
    const char* path;
    chttpx_handler_t handler;
} chttpx_route_t;

typedef struct {
    uint16_t port;

    size_t server_fd;

    size_t max_clients;
    size_t current_clients;

    /* Server timeout params */
    uint16_t read_timeout_sec; // 2b
    uint16_t write_timeout_sec; // 2b
    uint16_t idle_timeout_sec; // 2b

    /* Routes params */
    chttpx_route_t* routes;
    size_t routes_count;
    size_t routes_capacity;

    /* Middlewares */
    chttpx_middleware_stack_t middleware;

    /* Cors */
    chttpx_cors_t cors;
} chttpx_serv_t;

/* Structure for register routes */
typedef struct {
    chttpx_serv_t* serv;
    char* prefix;
} chttpx_router_t;

extern chttpx_serv_t *serv;

/**
 * Initialize the HTTP server.
 * @param serv_p The basic structure for working with a server.
 * @param port The TCP port on which the server will listen (e.g., 80, 8080).
 * This function must be called before registering routes or starting the server.
 */
int cHTTPX_Init(chttpx_serv_t* serv_p, uint16_t port, void* max_clients);

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
void cHTTPX_RegisterRoute(chttpx_router_t* r, const char *method, const char *path, chttpx_handler_t handler);

/**
 * Start the server loop to listen for incoming connections.
 * This function blocks indefinitely, accepting new client connections
 * and dispatching them to cHTTPX_Handle.
 */
void cHTTPX_Listen();

void cHTTPX_Shutdown();

#ifdef __cplusplus
extern }
#endif

#endif