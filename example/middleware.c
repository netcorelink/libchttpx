#include <libchttpx.h>

/** Requires a Bearer token; stops the chain with 401 when absent. */
static chttpx_middleware_result_t authenticate(chttpx_request_t* req, chttpx_response_t* res)
{
    if (!cHTTPX_BearerToken(req))
    {
        *res = cHTTPX_ResError(cHTTPX_StatusUnauthorized, "bearer token required");
        return out;
    }

    return next;
}

/** Protected route returning a minimal authenticated JSON payload. */
static void me(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"authenticated\":true}");
}

/** Router-scoped authentication middleware on /api/me. */
int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != cHTTPX_OK)
        return 1;

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "main", &config);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t api = cHTTPX_RoutePathPrefix(server, "/api");
    chttpx_router_t private_api = cHTTPX_RouteGroup(&api, "");

    cHTTPX_RouterUse(&private_api, authenticate);
    cHTTPX_Get(&private_api, "/me", me);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);

    return result == cHTTPX_OK ? 0 : 1;
}
