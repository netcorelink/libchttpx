#include "include/libchttpx.h"

#include <stdbool.h>

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

void create_user(chttpx_request_t* req, chttpx_response_t* res)
{
    user_t user = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("email", &user.uuid, true, 0, 254, VALIDATOR_EMAIL),
        chttpx_validation_string("password", &user.password, false, 6, 16, VALIDATOR_NONE),
        chttpx_validation_boolean("is_admin", &user.is_admin, false),
    };

    if (!cHTTPX_Parse(req, fields, ARRAY_LEN(fields)))
        goto error;

    if (!cHTTPX_Validate(req, fields, ARRAY_LEN(fields), "ru"))
        goto error;

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
    *res = cHTTPX_ResFile(cHTTPX_StatusOK, cHTTPX_CTYPE_PNG, "./prog.png");
}

void file_upload(chttpx_request_t* req, chttpx_response_t* res)
{
    if (req->filename[0] == '\0')
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", "Нет данных для обработки");
        return;
    }

    FILE* f = fopen(req->filename, "rb");
    if (!f)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", "Не удалось открыть временный файл");
        return;
    }

    char dest_path[512];
    snprintf(dest_path, sizeof(dest_path), "/tmp/s3_storage/%s", req->filename);

    FILE* out = fopen(dest_path, "wb");
    if (!out)
    {
        *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", "Не удалось создать файл назначения");

        fclose(f);
        return;
    }

    unsigned char buffer[65536];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0)
    {
        fwrite(buffer, 1, n, out);
    }

    fclose(f);
    fclose(out);

    remove(req->filename);

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"error\": \"%s\"}", "Файл скопирован во временное хранилище S3");
    return;
}

int main()
{
    chttpx_serv_t serv = {0};

    size_t max_clients = 2;
    if (cHTTPX_Init(&serv, 80, &max_clients) != 0)
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
    cHTTPX_RegisterRoute(&v1, "POST", "/file", file_upload);

    /* At the very end, to start listening to incoming requests from users. */
    cHTTPX_Listen();

    /* Shutdown server */
    cHTTPX_Shutdown();
}
