#include <libchttpx.h>

typedef struct
{
    char* email;
    char* name;
} create_user_t;

static void create_user(chttpx_request_t* req, chttpx_response_t* res)
{
    create_user_t payload = {0};

    chttpx_validation_t fields[] = {
        cHTTPX_StringField(
            "email",
            &payload.email,
            true,
            3,
            254,
            CHTTPX_TRIM | CHTTPX_LOWERCASE,
            NULL
        ),
        cHTTPX_StringField(
            "name",
            &payload.name,
            true,
            1,
            64,
            CHTTPX_TRIM,
            NULL
        ),
    };

    if (!cHTTPX_BindJSON(req, res, fields, CHTTPX_ARRAY_LEN(fields)))
        return;

    chttpx_json_t* json = cHTTPX_JsonObject(req);
    cHTTPX_JsonString(json, "email", payload.email);
    cHTTPX_JsonString(json, "name", payload.name);

    *res = cHTTPX_ResJsonObject(cHTTPX_StatusCreated, json);
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

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "/api");
    cHTTPX_Post(&router, "/users", create_user);

    int result = cHTTPX_AppRun(&app);
    cHTTPX_AppShutdown(&app);

    return result == CHTTPX_OK ? 0 : 1;
}
