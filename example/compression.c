#include <libchttpx.h>

#include <stdio.h>

static const char page[] =
    "<!doctype html><html><body>"
    "<h1>libchttpx compression example</h1>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "<p>This intentionally repetitive response demonstrates gzip negotiation.</p>"
    "</body></html>";

static void page_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "%s", page);
}

static void health_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "ok");
    cHTTPX_ResponseCompression(res, false);
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != CHTTPX_OK)
        return 1;

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "main", &config);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_compression_config_t compression = cHTTPX_CompressionDefault();
    compression.min_size = 256;
    compression.level = 5;

    int compression_result = cHTTPX_CompressionUse(server, &compression);
    if (compression_result != CHTTPX_OK)
    {
        fprintf(stderr, "compression unavailable: %d (build with COMPRESSION=1)\n", compression_result);
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    cHTTPX_Get(&router, "/", page_handler);

    chttpx_route_t* health = cHTTPX_Get(&router, "/health", health_handler);
    cHTTPX_RouteCompression(health, false);

    printf("Try: curl --compressed -i http://127.0.0.1:8080/\n");

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);
    return result == CHTTPX_OK ? 0 : 1;
}
