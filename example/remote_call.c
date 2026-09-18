#include <libchttpx.h>

static void create_payment(chttpx_request_t* req, chttpx_response_t* res)
{
    int result = cHTTPX_Call(
        req,
        "payments",
        cHTTPX_MethodPost,
        "/payments/create",
        res
    );

    if (result == CHTTPX_ERR_UNAVAILABLE)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusServiceUnavailable, "payments unavailable");
        return;
    }

    if (result == CHTTPX_ERR_TIMEOUT)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusGatewayTimeout, "payments timeout");
        return;
    }

    if (result != CHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusBadGateway, "payments call failed");
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != CHTTPX_OK)
        return 1;

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "public", &config);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    if (cHTTPX_AppRemote(&app, "payments", "http://payment-server:8090") != CHTTPX_OK)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    cHTTPX_Post(&router, "/payments", create_payment);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);

    return result == CHTTPX_OK ? 0 : 1;
}
