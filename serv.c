#include "libchttpx.h"
#include "libchttpx_utils.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

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

    cHTTPX_FieldValidation fields[] = {
        cHTTPX_FIELD_STRING("uuid", 1, 0, 36, &user.uuid),
        cHTTPX_FIELD_STRING("password", 1, 6, 16, &user.password),
        cHTTPX_FIELD_BOOL("is_admin", 0, &user.is_admin)
    };

    if (!cHTTPX_Parse(req, fields, 3, &error_msg)) {
        chttpx_response_t res = (chttpx_response_t){cHTTPX_StatusBadRequest, cHTTPX_CTYPE_JSON, cHTTPX_strdup(error_msg)};
        free(error_msg);
        return res;
    }

    return (chttpx_response_t){cHTTPX_StatusCreated, cHTTPX_CTYPE_JSON, "{\"message\":\"user has been successeful created!\"}"};
}

chttpx_response_t get_user(chttpx_request_t *req) {
    const char *uuid = cHTTPX_Param(req, "uuid");
    if (!uuid) {
        return (chttpx_response_t){cHTTPX_StatusNotFound, cHTTPX_CTYPE_JSON, "{\"error\": \"uuid not found\"}"};
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "{\"uuid\": \"%s\"}", uuid);
    return (chttpx_response_t){cHTTPX_StatusOK, cHTTPX_CTYPE_JSON, cHTTPX_strdup(buffer)};
}

int main() {
    cHTTPX_Init(8080, 16);

    cHTTPX_Route("GET", "/", home_index);
    cHTTPX_Route("GET", "/users/{uuid}", get_user);

    cHTTPX_Listen();
}