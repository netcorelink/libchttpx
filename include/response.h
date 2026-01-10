/**
 * Copyright (c) 2026 netcorelink
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `libchttpx.c` for details.
 */

#ifndef RESPONSE_H
#define RESPONSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "include/request.h"

/* Threads */
#ifdef defined(_WIN32) || defined(_WIN64)
#include <windows.h>

typedef HANDLE thread_t;

inline int _thread_create(thread_t *thread, void *(*func)(void*), void *arg) {
    *thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)func, arg, 0, NULL);
    return *thread ? 0 : -1;
}

inline int _thread_join(thread_t thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return 0;
}
#else
#include <pthread.h>

typedef pthread_t thread_t;

inline int thread_create(thread_t *thread, void *(*func)(void*), void *arg) {
    return pthread_create(thread, NULL, func, arg);
}

inline int thread_join(thread_t thread) {
    return pthread_join(thread, NULL);
}
#endif

// RESponse
typedef struct {
    int status;
    const char *content_type;
    const char *body;
} chttpx_response_t;

typedef chttpx_response_t (*chttpx_handler_t)(chttpx_request_t *req);

/**
 * Handle a single client connection.
 * @param client_fd The file descriptor of the accepted client socket.
 * This function reads the request, parses it, calls the matching route handler,
 * and sends the response back to the client.
 */
void cHTTPX_Handle(int client_fd);

/**
 * Create a JSON HTTP response with formatted content.
 *
 * Formats a JSON response body using printf-style arguments,
 * allocates memory for the response body, and returns a
 * fully initialized chttpx_response_t structure.
 *
 * @param status HTTP status code (e.g. 200, 400, 404).
 * @param fmt    printf-style format string for the JSON body.
 * @param ...    Format arguments.
 */
chttpx_response_t cHTTPX_JsonResponse(int status, const char *fmt, ...);

#ifdef __cplusplus
extern }
#endif

#endif