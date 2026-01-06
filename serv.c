#include "libuhttpx.h"
#include "libuhttpx_utils.h"
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    char *uuid;
    char *password;
    int is_admin;
} user_t;

httpx_response_t home_index(httpx_request_t *req) {
    return (httpx_response_t){uHTTPX_StatusOK, uHTTPX_CTYPE_HTML, "<h1>This is home page!</h1>"};
}

httpx_response_t create_user(httpx_request_t *req) {
    user_t user;
    char *error_msg = NULL;

    uHTTPX_FieldValidation fields[] = {
        uHTTPX_FIELD_STRING("uuid", 1, 0, 36, &user.uuid),
        uHTTPX_FIELD_STRING("password", 1, 6, 16, &user.password),
        uHTTPX_FIELD_BOOL("is_admin", 0, &user.is_admin)
    };

    if (!uHTTPX_Parse(req, fields, 3, &error_msg)) {
        httpx_response_t res = (httpx_response_t){uHTTPX_StatusBadRequest, uHTTPX_CTYPE_JSON, uHTTPX_strdup(error_msg)};
        free(error_msg);
        return res;
    }

    return (httpx_response_t){uHTTPX_StatusCreated, uHTTPX_CTYPE_JSON, "{\"message\":\"user has been successeful created!\"}"};
}

int main() {
    uHTTPX_Init(8080, 16);

    uHTTPX_Route("GET", "/", home_index);
    uHTTPX_Route("POST", "/user/create", create_user);

    uHTTPX_Listen();
}