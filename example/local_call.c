#include <libchttpx.h>

static void internal_process(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"source\":\"internal\"}");
}

static void public_proxy(chttpx_request_t* req, chttpx_response_t* res)
{
    int result = cHTTPX_Call(
        req,
        "internal",
        cHTTPX_MethodPost,
        "/process",
        res
    );

    if (result != CHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusBadGateway, "internal call failed");
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != CHTTPX_OK)
        return 1;

    chttpx_config_t public_config = cHTTPX_DefaultConfig();
    public_config.port = 8080;

    chttpx_config_t internal_config = cHTTPX_DefaultConfig();
    internal_config.port = 9090;

    chttpx_serv_t* public_server = cHTTPX_AppServer(&app, "public", &public_config);
    chttpx_serv_t* internal_server = cHTTPX_AppServer(&app, "internal", &internal_config);

    if (!public_server || !internal_server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t public_router = cHTTPX_RoutePathPrefix(public_server, "");
    chttpx_router_t internal_router = cHTTPX_RoutePathPrefix(internal_server, "");

    cHTTPX_Get(&public_router, "/proxy", public_proxy);
    cHTTPX_Post(&internal_router, "/process", internal_process);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);

    return result == CHTTPX_OK ? 0 : 1;
}
