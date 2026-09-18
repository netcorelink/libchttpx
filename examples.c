#include "libchttpx.h"

#include <stdio.h>

static void home(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "libchttpx is running");
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != CHTTPX_OK)
        return 1;

    chttpx_serv_t* server = cHTTPX_AppMicroserver(&app, "main", 8080);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    if (!cHTTPX_Get(&router, "/", home))
    {
        fprintf(stderr, "Failed to register route\n");
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);
    return result == CHTTPX_OK ? 0 : 1;
}
