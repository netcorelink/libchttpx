#include "include/libchttpx.h"

#include <stdbool.h>

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

void home_index(chttpx_request_t* req, chttpx_response_t *res)
{
    *res = cHTTPX_ResHtml(cHTTPX_StatusOK, "<h1>This is home page!</h1>");
}

typedef struct
{
    char* uuid;
    char* password;
    bool is_admin;
} user_t;

void create_user(chttpx_request_t* req, chttpx_response_t *res)
{
    user_t user = {0};

    chttpx_validation_t fields[] = {
        chttpx_validation_string("email", &user.uuid, true, 0, 254, VALIDATOR_EMAIL),
        chttpx_validation_string("password", &user.password, false, 6, 16, VALIDATOR_NONE),
        chttpx_validation_boolean("is_admin", &user.is_admin, false),
    };

    if (!cHTTPX_Parse(req, fields, ARRAY_LEN(fields))) goto error;

    if (!cHTTPX_Validate(req, fields, ARRAY_LEN(fields), "ru")) goto error;

    free(user.uuid);
    free(user.password);

    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", cHTTPX_i18n_t("welcome", "ru"));

error:
    free(user.uuid);
    free(user.password);

    *res = cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
}

void picture_handler(chttpx_request_t* req, chttpx_response_t *res)
{
    *res = cHTTPX_ResFile(cHTTPX_StatusOK, cHTTPX_CTYPE_PNG, "./prog.png");
}

int main()
{
    chttpx_serv_t serv = {0};

    if (cHTTPX_Init(&serv, 80) != 0)
    {
        printf("Failed to start server\n");
        return 1;
    }

    cHTTPX_i18n("/home/noneandundefined/Documents/Projects/netcorelink/libchttpx/public-emp");

    // middlewares
    // cHTTPX_MiddlewareRecovery();
    cHTTPX_MiddlewareRateLimiter(3, 1);

    cHTTPX_Route("GET", "/", home_index);
    cHTTPX_Route("GET", "/picture", picture_handler);
    cHTTPX_Route("POST", "/users", create_user);

    /* At the very end, to start listening to incoming requests from users. */
    cHTTPX_Listen();
}
