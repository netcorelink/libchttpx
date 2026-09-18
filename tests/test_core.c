#include "libchttpx.h"
#include "body.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int cleanup_calls;

static void count_cleanup(void* value)
{
    cleanup_calls += *(int*)value;
    free(value);
}

static bool username_validator(const void* value, char* error, size_t error_size)
{
    if (strcmp(value, "valid_user") == 0)
        return true;
    snprintf(error, error_size, "invalid username");
    return false;
}

static chttpx_middleware_result_t middleware(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    (void)res;
    return next;
}

static void handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResNoContent();
}

static void test_request_lifecycle(void)
{
    chttpx_request_t req = {0};
    char* text = cHTTPX_Strdup(&req, "owned");
    assert(text && strcmp(text, "owned") == 0);

    int* resource = malloc(sizeof(*resource));
    *resource = 2;
    assert(cHTTPX_Defer(&req, resource, count_cleanup) == 0);

    int* context = malloc(sizeof(*context));
    *context = 3;
    assert(cHTTPX_ContextSet(&req, "auth", context, count_cleanup) == 0);
    assert(cHTTPX_ContextGet(&req, "auth") == context);

    cHTTPX_RequestCleanup(&req);
    assert(cleanup_calls == 5);
}

static void test_typed_values(void)
{
    chttpx_request_t req = {0};
    strcpy(req.params[0].name, "id");
    strcpy(req.params[0].value, "18446744073709551615");
    req.params_count = 1;
    uint64_t id;
    assert(cHTTPX_ParamU64(&req, "id", &id));
    assert(id == UINT64_MAX);
    strcpy(req.params[0].value, "-1");
    assert(!cHTTPX_ParamU64(&req, "id", &id));

    char query[] = "name=Artem%20Vlasov&enabled=true&offset=42&path=%2Fapi%2Fv2";
    _parse_req_query(&req, query);
    assert(strcmp(cHTTPX_Query(&req, "name"), "Artem Vlasov") == 0);
    assert(strcmp(cHTTPX_Query(&req, "path"), "/api/v2") == 0);
    bool enabled;
    assert(cHTTPX_QueryBool(&req, "enabled", &enabled) && enabled);
    assert(cHTTPX_QueryU64(&req, "offset", &id) && id == 42);
    for (size_t i = 0; i < req.query_count; i++)
    {
        free(req.query[i].name);
        free(req.query[i].value);
    }
    free(req.query);
}

static void test_bind_and_json(void)
{
    chttpx_request_t req = {0};
    const char body[] = "{\"username\":\"  VALID_USER  \"}";
    req.body = (unsigned char*)body;
    req.body_size = strlen(body);
    strcpy(req.language, "en");
    char* username = NULL;
    chttpx_validation_t fields[] = {cHTTPX_StringField("username", &username, true, 3, 32, CHTTPX_TRIM | CHTTPX_LOWERCASE, username_validator)};
    chttpx_response_t response = {0};
    assert(cHTTPX_BindJSON(&req, &response, fields, CHTTPX_ARRAY_LEN(fields)));
    assert(strcmp(username, "valid_user") == 0);

    chttpx_json_t* object = cHTTPX_JsonObject(&req);
    assert(object);
    assert(cHTTPX_JsonString(object, "message", "quote: \"") == 0);
    assert(cHTTPX_JsonNumber(object, "id", 42) == 0);
    response = cHTTPX_ResJsonObject(cHTTPX_StatusOK, object);
    assert(response.body_ownership == CHTTPX_BODY_OWNED);
    assert(strstr((const char*)response.body, "\\\"") != NULL);
    cHTTPX_ResponseCleanup(&response);
    cHTTPX_RequestCleanup(&req);
}

static void test_multipart(void)
{
    chttpx_request_t req = {0};
    const char body[] = "--AaB03x\r\nContent-Disposition: form-data; name=\"title\"\r\n\r\nhello\r\n"
                        "--AaB03x\r\nContent-Disposition: form-data; name=\"file\"; filename=\"a.txt\"\r\n"
                        "Content-Type: text/plain\r\n\r\ndata\r\n--AaB03x--\r\n";
    strcpy(req.content_type, "multipart/form-data; boundary=AaB03x");
    req.body = (unsigned char*)body;
    req.body_size = strlen(body);
    req.content_length = req.body_size;
    _parse_media(&req, NULL, 0);
    assert(req._parse_status == 0);
    assert(strcmp(cHTTPX_FormValue(&req, "title"), "hello") == 0);
    const chttpx_file_t* file = cHTTPX_FormFile(&req, "file");
    assert(file && file->size == 4 && strcmp(file->original_name, "a.txt") == 0);
    char path[512];
    snprintf(path, sizeof(path), "%s", file->path);
    FILE* uploaded = fopen(path, "rb");
    assert(uploaded);
    fclose(uploaded);
    cHTTPX_RequestCleanup(&req);
    assert(fopen(path, "rb") == NULL);
}


static void test_streamed_multipart(void)
{
    chttpx_serv_t server = {0};
    server.max_body_size = 1024 * 1024;
    server.max_upload_size = 8 * 1024 * 1024;
    serv = &server;

    chttpx_request_t req = {0};
    const char body[] = "--StreamBoundary\r\n"
                        "Content-Disposition: form-data; name=\"title\"\r\n\r\nhello\r\n"
                        "--StreamBoundary\r\n"
                        "Content-Disposition: form-data; name=\"file\"; filename=\"photo.jpg\"\r\n"
                        "Content-Type: image/jpeg\r\n\r\nJPEGDATA\r\n"
                        "--StreamBoundary--\r\n";
    char request[2048];
    int body_size = (int)strlen(body);
    int request_size = snprintf(request, sizeof(request),
                                "POST /upload HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=StreamBoundary\r\n"
                                "Content-Length: %d\r\n\r\n%s",
                                body_size, body);
    assert(request_size > 0 && (size_t)request_size < sizeof(request));

    strcpy(req.content_type, "multipart/form-data; boundary=StreamBoundary");
    strcpy(req.headers[0].name, "Content-Type");
    strcpy(req.headers[0].value, req.content_type);
    strcpy(req.headers[1].name, "Content-Length");
    snprintf(req.headers[1].value, sizeof(req.headers[1].value), "%d", body_size);
    req.headers_count = 2;

    _parse_req_body(&req, 0, request, (size_t)request_size);
    assert(req._parse_status == 0);
    assert(req.body == NULL);
    assert(req._multipart_stream != NULL);

    _parse_media(&req, request, (size_t)request_size);
    assert(req._parse_status == 0);
    assert(strcmp(cHTTPX_FormValue(&req, "title"), "hello") == 0);
    const chttpx_file_t* file = cHTTPX_FormFile(&req, "file");
    assert(file && file->size == 8);
    assert(strcmp(file->original_name, "photo.jpg") == 0);

    cHTTPX_RequestCleanup(&req);
    serv = NULL;
}

static void test_routing_api(void)
{
    chttpx_serv_t server = {0};
    serv = &server;
    chttpx_router_t api = cHTTPX_RoutePathPrefix("/api/v2");
    chttpx_router_t private_routes = cHTTPX_RouteGroup(&api, "");
    assert(cHTTPX_RouterUse(&private_routes, middleware) == CHTTPX_OK);
    chttpx_route_t* route = cHTTPX_Get(&private_routes, "/users/me", handler);
    assert(route && strcmp(route->path, "/api/v2/users/me") == 0);
    assert(route->middleware_count == 1);
    assert(cHTTPX_RouteUseAfter(route, middleware) == CHTTPX_OK);
    assert(route->after_middleware_count == 1);
    free((void*)route->method);
    free((void*)route->path);
    free(server.routes);
    serv = NULL;
}

static void test_helpers(void)
{
    assert(strcmp(cHTTPX_StatusReason(404), "Not Found") == 0);
    assert(cHTTPX_MimeIsImage("image/png"));
    assert(cHTTPX_MimeMatch("image/png; charset=binary", "image/*"));
    chttpx_request_t req = {0};
    strcpy(req.headers[0].name, "Authorization");
    strcpy(req.headers[0].value, "bEaReR token");
    req.headers_count = 1;
    assert(strcmp(cHTTPX_BearerToken(&req), "token") == 0);
    chttpx_response_t response = cHTTPX_ResMessage(200, "a \"quoted\" message");
    assert(strstr((const char*)response.body, "\\\"quoted\\\"") != NULL);
    cHTTPX_ResponseCleanup(&response);
}

int main(void)
{
    test_request_lifecycle();
    test_typed_values();
    test_bind_and_json();
    test_multipart();
    test_streamed_multipart();
    test_routing_api();
    test_helpers();
    puts("all tests passed");
    return 0;
}
