#include "libchttpx.h"

#include <stdio.h>

static void home(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "libchttpx is running");
}

int main(void)
{
    chttpx_serv_t server;
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;

    int result = cHTTPX_InitWithConfig(&server, &config);
    if (result != CHTTPX_OK)
    {
        fprintf(stderr, "Failed to initialize server: %d\n", result);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix("");
    if (!cHTTPX_Get(&router, "/", home))
    {
        fprintf(stderr, "Failed to register route\n");
        cHTTPX_Shutdown();
        return 1;
    }

    cHTTPX_Listen();
    cHTTPX_RouterFree(&router);
    cHTTPX_Shutdown();
    return 0;
}
