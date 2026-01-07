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

chttpx_response_t home_index(chttpx_request_t *req) {
    return (chttpx_response_t){cHTTPX_StatusOK, cHTTPX_CTYPE_HTML, "<h1>This is home page!</h1>"};
}

chttpx_response_t create_user(chttpx_request_t *req) {
    user_t user;
    char *error_msg = NULL;

    chttpx_validation_t fields[] = {
        chttpx_validation_str("uuid", true, 0, 36, &user.uuid),
        chttpx_validation_str("password", true, 6, 16, &user.password),
        chttpx_validation_bool("is_admin", false, &user.is_admin)
    };

    if (!cHTTPX_Parse(req, fields, cHTTPX_ARRAY_LEN(fields), &error_msg)) {
        return cHTTPX_JsonResponse(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", error_msg);
    }

    if (!cHTTPX_Validate(fields, cHTTPX_ARRAY_LEN(fields), &error_msg)) {
        return cHTTPX_JsonResponse(cHTTPX_StatusBadRequest, "{\"error\": \"%s\"}", error_msg);
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

    char *orgParam = cHTTPX_Query(req, "org");
    if (!orgParam) orgParam = "";

    return cHTTPX_JsonResponse(cHTTPX_StatusOK, "{\"message\": {\"uuid\": \"%s\", \"page\": \"%s\", \"org\": \"%s\"}}", uuid, page, orgParam);
}

int main() {
    cHTTPX_Init(8080, 16);

    cHTTPX_Route("GET", "/", home_index);
    cHTTPX_Route("GET", "/users/{uuid}/{page}", get_user); // ?org=netcorelink
    cHTTPX_Route("POST", "/users", create_user);

    cHTTPX_Listen();
}