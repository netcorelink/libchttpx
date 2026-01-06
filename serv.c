#include "libuhttpx/libuhttpx.h"
#include <stdio.h>

http_response_t index(http_request_t *req) {
    return (http_response_t){StatusOK(), CT_HTML, "<h1>This is home page!</h1>"};
}

http_response_t status(http_request_t *req) {
    return (http_response_t){StatusOK(), CT_JSON, "{\"ok\":true}"};
}

int main() {
    http_init(8080, 16); // 16 - is a base

    http_route("GET", "/", index);
    http_route("GET", "/status", status);

    printf("Server running on port 8080...\n");
    http_serve();
}