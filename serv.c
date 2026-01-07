#include "cHTTPX/libchttpx.h"
#include "cHTTPX/libchttpx_utils.h"

#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    char *uuid;
    char *password;
    int is_admin;
} user_t;

int auth_middleware(chttpx_request_t *req, chttpx_response_t *res) {
    const char *token = cHTTPX_Header(req, "Auth");

    if (!token) {
        *res = cHTTPX_JsonResponse(cHTTPX_StatusUnauthorized, "{\"error\": \"unauthorized\"}");
        return 0;
    }

    return 1;
}

chttpx_response_t home_index(chttpx_request_t *req) {
    return (chttpx_response_t){cHTTPX_StatusOK, cHTTPX_CTYPE_HTML, "<h1>This is home page!</h1>"};
}

chttpx_response_t create_user(chttpx_request_t *req) {
    user_t user;

    chttpx_validation_t fields[] = {
        chttpx_validation_str("uuid", true, 0, 36, &user.uuid),
        chttpx_validation_str("password", true, 6, 16, &user.password),
        chttpx_validation_bool("is_admin", false, &user.is_admin)
    };

    if (!cHTTPX_Parse(req, fields, cHTTPX_ARRAY_LEN(fields))) {
        return cHTTPX_JsonResponse(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
    }

    if (!cHTTPX_Validate(req, fields, cHTTPX_ARRAY_LEN(fields))) {
        return cHTTPX_JsonResponse(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", req->error_msg);
    }

    return cHTTPX_JsonResponse(cHTTPX_StatusCreated, "{\"message\":\"user has been successeful created!\"}");
}

chttpx_response_t get_user(chttpx_request_t *req) {
    const char *uuid = cHTTPX_Param(req, "uuid");
    if (!uuid) {
        return cHTTPX_JsonResponse(cHTTPX_StatusNotFound, "{\"error\": \"uuid not found\"}");
    }

    const char *page = cHTTPX_Param(req, "page");
    if (!page) {
        return cHTTPX_JsonResponse(cHTTPX_StatusNotFound, "{\"error\": \"page not found\"}");
    }

    const char *sizeParam = cHTTPX_Query(req, "size");
    if (!sizeParam) sizeParam = "0";

    int size = atoi(sizeParam);

    return cHTTPX_JsonResponse(cHTTPX_StatusOK, "{\"message\": {\"uuid\": \"%s\", \"page\": \"%s\", \"size\": \"%d\"}}", uuid, page, size);
}

int main() {
    chttpx_server_t serv;

    if (cHTTPX_Init(&serv, 8080) != 0) {
        printf("Failed to start server\n");
        return 1;
    }

    // timeouts
    serv.read_timeout_sec = 300;
    serv.write_timeout_sec = 300;
    serv.idle_timeout_sec = 90;

    /* Cors */
    const char *allowed_origins[] = {
        "https://neosync.neomatica.ru",
    };

    cHTTPX_Cors(allowed_origins, cHTTPX_ARRAY_LEN(allowed_origins), NULL, NULL);

    // cHTTPX_MiddlewareUse(auth_middleware);

    cHTTPX_Route("GET", "/", home_index);
    cHTTPX_Route("GET", "/users/{uuid}/{page}", get_user); // ?org=netcorelink
    cHTTPX_Route("POST", "/users", create_user);

    cHTTPX_Listen();
}
