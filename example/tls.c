#include <libchttpx.h>

#include <stdio.h>

static void health(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"https\":true}");
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <certificate.pem> <private-key.pem>\n", argv[0]);
        return 2;
    }

    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != cHTTPX_OK)
        return 1;

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8443;
    config.tls.enabled = true;
    config.tls.cert_file = argv[1];
    config.tls.key_file = argv[2];

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "https", &config);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    cHTTPX_Get(&router, "/health", health);

    printf("HTTPS server listening on https://localhost:%u\n", server->port);
    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);
    return result == cHTTPX_OK ? 0 : 1;
}
