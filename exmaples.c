#include "include/libchttpx.h"

#include <stdbool.h>

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

chttpx_response_t home_index(chttpx_request_t* req)
{
    return cHTTPX_ResHtml(cHTTPX_StatusOK, "<h1>This is home page!</h1>");
}

typedef struct
{
    char* uuid;
    char* password;
    int is_admin;
} user_t;

chttpx_response_t create_user(chttpx_request_t* req)
{
    user_t user;

    chttpx_validation_t fields[] = {chttpx_validation_str("uuid", true, 0, 36, &user.uuid),
                                    chttpx_validation_str("password", true, 6, 16, &user.password),
                                    chttpx_validation_bool("is_admin", false, &user.is_admin)};

    if (!cHTTPX_Parse(req, fields, ARRAY_LEN(fields)))
    {
        return cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
    }

    if (!cHTTPX_Validate(req, fields, ARRAY_LEN(fields)))
    {
        return cHTTPX_ResJson(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
    }

    // return cHTTPX_ResJson(cHTTPX_StatusOK, "{\"message\": \"%s\"}", cHTTPX_i18n_t("user-created",
    // "ru"));
}

chttpx_response_t picture_handler(chttpx_request_t* req)
{
    return cHTTPX_ResFile(cHTTPX_StatusOK, cHTTPX_CTYPE_PNG, "./prog.png");
}

int main()
{
    chttpx_serv_t serv;

    if (cHTTPX_Init(&serv, 80) != 0)
    {
        printf("Failed to start server\n");
        return 1;
    }

    // cHTTPX_i18n("public-emp");

    // middlewares
    cHTTPX_MiddlewareRateLimiter(3, 1);

    cHTTPX_Route("GET", "/", home_index);
    cHTTPX_Route("GET", "/picture", picture_handler);
    cHTTPX_Route("POST", "/users", create_user);

    /* At the very end, to start listening to incoming requests from users. */
    cHTTPX_Listen();
}