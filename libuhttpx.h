#ifndef LIBUHTTPX
#define LIBUHTTPX

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    const char *method;
    const char *path;
    const char *query;
    const char *body;
} http_request_t;

typedef struct {
    int status;
    const char *content_type;
    const char *body;
} http_response_t;

typedef http_response_t (*http_handler_t)(http_request_t *req);

typedef enum {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_PATCH,
    HTTP_DELETE,
} http_method_t;

void http_init(int port, int max_routes);
void http_route(const char *method, const char *path, http_handler_t handler);
void http_handle(int socket_fd);
void http_serve(void);

// HTTP statuses
// 1xx
int StatusContinue(void);
int StatusSwitchingProtocols(void);
int StatusProcessing(void);
// 2xx
int StatusOK(void);
int StatusCreated(void);
int StatusAccepted(void);
int StatusNonAuthoritativeInformation(void);
int StatusNoContent(void);
int StatusResetContent(void);
// 3xx
int StatusNotModified(void);
int StatusUseProxy(void);
int StatusTemporaryRedirect(void);
// 4xx
int StatusBadRequest(void);
int StatusUnauthorized(void);
int StatusForbidden(void);
int StatusNotFound(void);
int StatusProxyAuthenticationRequired(void);
int StatusRequestTimeout(void);
int StatusConflict(void);
int StatusPayloadTooLarge(void);
int StatusURITooLong(void);
int StatusTooManyRequests(void);
// 5xx
int StatusInternalServerError(void);
int StatusBadGateway(void);
int StatusGatewayTimeout(void);

#ifdef __cplusplus
}
#endif

#endif
