#include <libchttpx.h>

#include <stdio.h>

static void events(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_sse_t* sse = cHTTPX_SSEOpen(req, res);
    if (!sse)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "failed to open SSE stream");
        return;
    }

    if (cHTTPX_SSERetry(sse, 3000) != cHTTPX_OK)
        return;

    for (unsigned int i = 1; i <= 10 && cHTTPX_SSEConnected(sse); i++)
    {
        char id[32];
        char data[128];
        snprintf(id, sizeof(id), "%u", i);
        snprintf(data, sizeof(data), "{\"progress\":%u}", i * 10);

        if (cHTTPX_SSESend(sse, "progress", id, data) != cHTTPX_OK)
            break;

#ifdef CHTTPX_PLATFORM_WINDOWS
        Sleep(1000);
#else
        sleep(1);
#endif
    }

    cHTTPX_SSEClose(sse);
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != cHTTPX_OK)
        return 1;

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "sse", &config);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    cHTTPX_Get(&router, "/events", events);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);
    return result == cHTTPX_OK ? 0 : 1;
}
