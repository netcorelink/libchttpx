#ifndef LIBUHTTPX_H
#define LIBUHTTPX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "status.h"

#include <stdint.h>

#define BUFFER_SIZE 1024

// HTTP Content Types
#define CT_HTML  "text/html"
#define CT_JSON  "application/json"
#define CT_PNG   "image/png"
#define CT_JPEG  "image/jpeg"
#define CT_TEXT  "text/plain"
#define CT_CSS   "text/css"
#define CT_JS    "application/javascript"

// REQuest
typedef struct {
    const char *method;
    const char *path;
    const char *query;
    const char *body;
} http_request_t;

// RESponse
typedef struct {
    int status;
    const char *content_type;
    const char *body;
} http_response_t;

typedef http_response_t (*http_handler_t)(http_request_t *req);

// Route
typedef struct {
    const char *method;
    const char *path;
    http_handler_t handler;
} route_t;

// HTTP methods
typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE,
} http_method_t;

// http fns
void http_init(int port, int max_routes);
void http_route(const char *method, const char *path, http_handler_t handler);
void http_handle(int client_fd);
void http_serve(void);

// response fns
void send_response(int client_fd, http_response_t res);

#ifdef __cplusplus
}
#endif

#endif
