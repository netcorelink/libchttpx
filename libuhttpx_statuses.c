#include "libuhttpx.h"

// 1xx
int StatusContinue(void) { return 100; }
int StatusSwitchingProtocols(void) { return 101; }
int StatusProcessing(void) { return 102; }
// 2xx
int StatusOK(void) { return 200; }
int StatusCreated(void) { return 201; }
int StatusAccepted(void) { return 202; }
int StatusNonAuthoritativeInformation(void) { return 203; }
int StatusNoContent(void) { return 204; }
int StatusResetContent(void) { return 205; }
// 3xx
int StatusNotModified(void) { return 304; }
int StatusUseProxy(void) { return 305; }
int StatusTemporaryRedirect(void) { return 307; }
// 4xx
int StatusBadRequest(void) { return 400; }
int StatusUnauthorized(void) { return 401; }
int StatusForbidden(void) { return 403; }
int StatusNotFound(void) { return 404; }
int StatusProxyAuthenticationRequired(void) { return 407; }
int StatusRequestTimeout(void) { return 408; }
int StatusConflict(void) { return 409; }
int StatusPayloadTooLarge(void) { return 413; }
int StatusURITooLong(void) { return 414; }
int StatusTooManyRequests(void) { return 429; }
// 5xx
int StatusInternalServerError(void) { return 500; }
int StatusBadGateway(void) { return 502; }
int StatusGatewayTimeout(void) { return 504; }
