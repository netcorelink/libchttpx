#ifndef STATUS_H
#define STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

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