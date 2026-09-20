#include "libchttpx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void secure_health(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"secure\":true}");
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s <certificate.pem> <private-key.pem>\n", argv[0]);
        return 2;
    }

    chttpx_app_t app;
    assert(cHTTPX_AppInit(&app) == CHTTPX_OK);

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 0;
    config.tls.enabled = true;
    config.tls.cert_file = argv[1];
    config.tls.key_file = argv[2];

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "secure-local", &config);
    assert(server != NULL);

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    assert(cHTTPX_Get(&router, "/health", secure_health) != NULL);

    char base_url[128];
    snprintf(base_url, sizeof(base_url), "https://localhost:%u", server->port);

    /* HTTPS verifies certificates by default: the self-signed test cert must fail. */
    assert(cHTTPX_AppRemote(&app, "untrusted", base_url) == CHTTPX_OK);

    chttpx_tls_client_config_t tls = cHTTPX_DefaultTLSClientConfig();
    assert(tls.verify_peer);
    tls.ca_file = argv[1];
    assert(cHTTPX_AppRemoteEx(&app, "trusted", base_url, &tls) == CHTTPX_OK);

    assert(cHTTPX_AppStart(&app) == CHTTPX_OK);

    chttpx_request_t source = {0};
    source._server = server;
    snprintf(source.request_id, sizeof(source.request_id), "%s", "tls-integration-test");
    snprintf(source.language, sizeof(source.language), "%s", "en");
    snprintf(source.content_type, sizeof(source.content_type), "%s", cHTTPX_CTYPE_JSON);

    chttpx_call_options_t options = {
        .content_type = cHTTPX_CTYPE_JSON,
    };

    chttpx_response_t rejected = {0};
    assert(cHTTPX_CallEx(&source, "untrusted", cHTTPX_MethodGet, "/health", &options, &rejected) == CHTTPX_ERR_TLS);
    cHTTPX_ResponseCleanup(&rejected);

    chttpx_response_t response = {0};
    assert(cHTTPX_CallEx(&source, "trusted", cHTTPX_MethodGet, "/health", &options, &response) == CHTTPX_OK);
    assert(response.status == cHTTPX_StatusOK);
    assert(response.body != NULL);
    assert(response.body_size == strlen("{\"secure\":true}"));
    assert(memcmp(response.body, "{\"secure\":true}", response.body_size) == 0);
    cHTTPX_ResponseCleanup(&response);

    cHTTPX_AppShutdown(&app);
    puts("TLS integration test passed");
    return 0;
}
