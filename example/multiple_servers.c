#include <libchttpx.h>

static void public_health(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "public");
}

static void admin_health(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "admin");
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != CHTTPX_OK)
        return 1;

    chttpx_config_t public_config = cHTTPX_DefaultConfig();
    public_config.port = 8080;

    chttpx_config_t admin_config = cHTTPX_DefaultConfig();
    admin_config.port = 9090;

    chttpx_serv_t* public_server = cHTTPX_AppServer(&app, "public", &public_config);
    chttpx_serv_t* admin_server = cHTTPX_AppServer(&app, "admin", &admin_config);

    if (!public_server || !admin_server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t public_router = cHTTPX_RoutePathPrefix(public_server, "");
    chttpx_router_t admin_router = cHTTPX_RoutePathPrefix(admin_server, "");

    cHTTPX_Get(&public_router, "/health", public_health);
    cHTTPX_Get(&admin_router, "/health", admin_health);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);

    return result == CHTTPX_OK ? 0 : 1;
}
