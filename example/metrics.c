#include <libchttpx.h>

#include <stdio.h>

static void hello(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "hello");
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != cHTTPX_OK)
        return 1;

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;
    config.metrics_enabled = true;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "main", &config);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    if (!cHTTPX_Get(&router, "/hello", hello) ||
        cHTTPX_MetricsRoute(&router, "/metrics") != cHTTPX_OK)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    puts("server:  http://127.0.0.1:8080/hello");
    puts("metrics: http://127.0.0.1:8080/metrics");

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);
    return result == cHTTPX_OK ? 0 : 1;
}
