#include "include/libchttpx.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

void home_index(chttpx_request_t* req, chttpx_response_t* res)
{
    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "<h1>This is home page!</h1>");
}

typedef struct
{
    char* uuid;
    char* password;
    bool is_admin;
} user_t;

void swagger_json_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    *res = cHTTPX_ResFile(cHTTPX_StatusOK, cHTTPX_CTYPE_JSON, "swagger.json");
    return;
}

void swagger_gui_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    const char* port_env = getenv("CHTTPX_PORT");
    const char* port = port_env && port_env[0] != '\0' ? port_env : "8080";
    char url[128];
    snprintf(url, sizeof(url), "http://localhost:%s/api/v1", port);

    char swagger_html[8192];
    snprintf(swagger_html, sizeof(swagger_html),
             "<!DOCTYPE html>\n"
             "<html lang=\"en\">\n"
             "<head>\n"
             "  <meta charset=\"utf-8\" />\n"
             "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
             "  <meta name=\"description\" content=\"SwaggerUI\" />\n"
             "  <title>SwaggerUI</title>\n"
             "  <link rel=\"stylesheet\" href=\"https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui.css\" />\n"
             "</head>\n"
             "<body>\n"
             "<div id=\"swagger-ui\"></div>\n"
             "<script src=\"https://unpkg.com/swagger-ui-dist@5.11.0/swagger-ui-bundle.js\" crossorigin></script>\n"
             "<script>\n"
             "window.onload = () => {\n"
             "  window.ui = SwaggerUIBundle({\n"
             "    url: '%s/doc.api/swagger/json',\n"
             "    dom_id: '#swagger-ui'\n"
             "  });\n"
             "};\n"
             "</script>\n"
             "</body>\n"
             "</html>\n",
             url);

    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, swagger_html);
    return;
}

void array(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_string_array_t tags = {0};

    chttpx_validation_t fields[] = {
        {.name = "tags", .type = FIELD_STRING_ARRAY, .target = &tags},
    };

    if (!cHTTPX_Parse(req, fields, ARRAY_LEN(fields)))
        goto error;

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", tags.items[0]);
    return;

error:
    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
    return;
}

void create_user(chttpx_request_t* req, chttpx_response_t* res)
{
    user_t user = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("uuid", &user.uuid, true, 0, 254, VALIDATOR_NONE),
        chttpx_validation_string("password", &user.password, false, 6, 16, VALIDATOR_NONE),
        chttpx_validation_boolean("is_admin", &user.is_admin, false),
    };

    if (!cHTTPX_Parse(req, fields, ARRAY_LEN(fields)))
        goto error;

    if (!cHTTPX_Validate(req, fields, ARRAY_LEN(fields), "ru"))
        goto error;

    printf("%s", user.uuid);

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", cHTTPX_i18n_t("welcome", "ru"));
    return;

error:
    free(user.uuid);
    free(user.password);

    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
    return;
}

void picture_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_cookie_t cookieAccess = {.name = "access",
                                    .value = "123-456-789-0",
                                    .path = "/",
                                    .domain = "localhost",
                                    .expires = time(NULL) + (24 * 60 * 60),
                                    .same_site = 2,
                                    .http_only = false,
                                    .secure = false};

    if (cHTTPX_CookieSet(res, &cookieAccess) != 0)
    {
        goto error;
    }

    *res = cHTTPX_ResFile(cHTTPX_StatusOK, cHTTPX_CTYPE_PNG, "./prog.png");
    return;

error:
    *res = cHTTPX_ResJson(cHTTPX_StatusInternalServerError, "{\"error\": \"oops, same error in server\"}");
}

void file_upload(chttpx_request_t* req, chttpx_response_t* res)
{
    if (req->filename[0] == '\0')
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", "Нет данных для обработки");
        return;
    }

    printf("%s\n", req->content_type);

    FILE* f = fopen(req->filename, "rb");
    if (!f)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", "Не удалось открыть временный файл");
        return;
    }

    fclose(f);

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", "Файл скопирован во временное хранилище S3");

    remove(req->filename);
    return;
}

void chat_on_open(chttpx_wsocket_t* ws, void* userdata)
{
    (void)userdata;
    const char* room = cHTTPX_WSocketParam(ws, "room_id");
    char welcome[256];
    snprintf(welcome, sizeof(welcome), "Welcome to room %s!", room ? room : "?");
    cHTTPX_WSocketSend(ws, welcome);
}

void chat_on_message(chttpx_wsocket_t* ws, const unsigned char* data, size_t len, int opcode, void* userdata)
{
    (void)opcode;
    (void)userdata;

    char msg[4096];
    if (len >= sizeof(msg))
        len = sizeof(msg) - 1;
    memcpy(msg, data, len);
    msg[len] = '\0';

    /* Only clients in the same room (same URL path) receive the message. */
    cHTTPX_WSocketBroadcastPeers(ws, msg);
}

void chat_on_close(chttpx_wsocket_t* ws, void* userdata)
{
    (void)ws;
    (void)userdata;
}

static chttpx_wsocket_callbacks_t chat_callbacks = {
    .on_open = chat_on_open,
    .on_message = chat_on_message,
    .on_close = chat_on_close,
};

int main()
{
    chttpx_serv_t serv = {0};

    const char* port_env = getenv("CHTTPX_PORT");
    uint16_t port = port_env ? (uint16_t)atoi(port_env) : 8080;

    size_t max_clients = 1024;
    if (cHTTPX_Init(&serv, port, &max_clients) != 0)
    {
        printf("Failed to start server\n");
        return 1;
    }

    cHTTPX_i18n("/home/noneandundefined/Documents/Projects/netcorelink/libchttpx/public-emp");

    // middlewares
    cHTTPX_MiddlewareRecovery();
    cHTTPX_MiddlewareLogging();
    cHTTPX_MiddlewareRateLimiter(3, 1);

    /* Initial router */
    chttpx_router_t v1 = cHTTPX_RoutePathPrefix("/api/v1");

    cHTTPX_RegisterRoute(&v1, "GET", "/", home_index);
    cHTTPX_RegisterRoute(&v1, "GET", "/picture", picture_handler);
    cHTTPX_RegisterRoute(&v1, "POST", "/users", create_user);
    cHTTPX_RegisterRoute(&v1, "PATCH", "/file", file_upload);
    cHTTPX_RegisterRoute(&v1, "GET", "/doc.api/swagger/json", swagger_json_handler);
    cHTTPX_RegisterRoute(&v1, "GET", "/doc.api/swagger/gui", swagger_gui_handler);
    /* Client connects: ws://host/api/v1/ws/chat/lobby  (room_id = "lobby") */
    cHTTPX_WSocketRegisterRoute(&v1, "/ws/chat/{room_id}", &chat_callbacks);

    /* At the very end, to start listening to incoming requests from users. */
    cHTTPX_Listen();

    /* Shutdown server */
    cHTTPX_Shutdown();
}
