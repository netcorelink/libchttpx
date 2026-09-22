#include <libchttpx.h>

static void upload_avatar(chttpx_request_t* req, chttpx_response_t* res)
{
    const chttpx_file_t* avatar = cHTTPX_FormFile(req, "avatar");

    if (!avatar)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusBadRequest, "avatar is required");
        return;
    }

    *res = cHTTPX_ResJson(
        cHTTPX_StatusOK,
        "{\"size\":%llu}",
        (unsigned long long)avatar->size
    );
}

int main(void)
{
    chttpx_app_t app;
    if (cHTTPX_AppInit(&app) != cHTTPX_OK)
        return 1;

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "main", &config);
    if (!server)
    {
        cHTTPX_AppShutdown(&app);
        return 1;
    }

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "/api");

    chttpx_route_t* route =
        cHTTPX_Post(&router, "/avatar", upload_avatar);

    const char* allowed[] = {"image/jpeg", "image/png"};
    chttpx_upload_policy_t policy = {
        .max_size = 5 * 1024 * 1024,
        .allowed_types = allowed,
        .allowed_types_count = CHTTPX_ARRAY_LEN(allowed),
    };

    cHTTPX_RouteUploadPolicy(route, &policy);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);

    return result == cHTTPX_OK ? 0 : 1;
}
