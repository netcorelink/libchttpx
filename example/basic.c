#include <libchttpx.h>

/** Liveness check; responds with plain text "healthy". */
static void health(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "healthy");
}

/** Minimal single-server example on port 8080. */
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

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    cHTTPX_Get(&router, "/health", health);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);

    return result == cHTTPX_OK ? 0 : 1;
}
