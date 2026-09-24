#include "libchttpx.h"
#include "cHTTPX_http2.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int handler_calls;
static int options_handler_calls;
static char observed_body[64];
static char observed_language[16];
static char observed_request_id[65];
static char observed_client_ip[46];
static char internal_observed_body[64];
static uint16_t public_port;
static uint16_t internal_port;
static int last_remote_call_result;

static void request_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    __atomic_fetch_add(&handler_calls, 1, __ATOMIC_SEQ_CST);
    snprintf(observed_body, sizeof(observed_body), "%.*s", (int)req->body_size, req->body ? (const char*)req->body : "");
    snprintf(observed_language, sizeof(observed_language), "%s", req->language);
    snprintf(observed_request_id, sizeof(observed_request_id), "%s", req->request_id);
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "done");
}

static void ip_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    snprintf(observed_client_ip, sizeof(observed_client_ip), "%s", req->client_ip);
    *res = cHTTPX_ResNoContent();
}

static void options_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    __atomic_fetch_add(&options_handler_calls, 1, __ATOMIC_SEQ_CST);
    *res = cHTTPX_ResNoContent();
}

static void empty_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    (void)res;
}

static void internal_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    snprintf(internal_observed_body, sizeof(internal_observed_body), "%.*s", (int)req->body_size,
             req->body ? (const char*)req->body : "");
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"server\":\"internal\",\"accepted\":true}");
}

static void proxy_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    if (cHTTPX_Call(req, "internal", cHTTPX_MethodPost, "/process", res) != cHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "internal server call failed");
}

static void remote_proxy_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    last_remote_call_result = cHTTPX_Call(req, "remote-payments", cHTTPX_MethodPost, "/process", res);
    if (last_remote_call_result != cHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusBadGateway, "remote server call failed");
}

static void remote_error_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "remote error");
}

static void remote_error_proxy_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    last_remote_call_result = cHTTPX_Call(req, "remote-payments", cHTTPX_MethodPost, "/error", res);
    if (last_remote_call_result != cHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusBadGateway, "remote server call failed");
}

static void unavailable_proxy_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    last_remote_call_result = cHTTPX_Call(req, "unavailable", cHTTPX_MethodPost, "/process", res);
    if (last_remote_call_result == cHTTPX_ERR_UNAVAILABLE)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusServiceUnavailable, "remote server unavailable");
        return;
    }

    *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "unexpected call result");
}

static void custom_proxy_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    static const char custom_body[] = "{\"custom\":true}";
    chttpx_call_options_t options = {
        .body = custom_body,
        .body_size = sizeof(custom_body) - 1,
        .content_type = cHTTPX_CTYPE_JSON,
    };

    if (cHTTPX_CallEx(req, "internal", cHTTPX_MethodPost, "/process", &options, res) != cHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "custom internal call failed");
}

static void exchange_family(uint16_t port, int family, const char* request, char* response, size_t response_size)
{
    assert(request && response && response_size > 0);

    const char* line_end = strstr(request, "\r\n");
    const char* header_end = strstr(request, "\r\n\r\n");
    assert(line_end && header_end);

    char first_line[CHTTPX_MAX_PATH + 64];
    size_t first_line_size = (size_t)(line_end - request);
    assert(first_line_size < sizeof(first_line));
    memcpy(first_line, request, first_line_size);
    first_line[first_line_size] = '\0';

    char method[16];
    char path[CHTTPX_MAX_PATH];
    char protocol[16];
    assert(sscanf(first_line, "%15s %4095s %15s", method, path, protocol) == 3);
    assert(strcmp(protocol, "HTTP/2") == 0);

    chttpx_request_t source = {0};
    const char* cursor = line_end + 2;
    while (cursor < header_end)
    {
        const char* next = strstr(cursor, "\r\n");
        assert(next && next <= header_end);
        if (next == cursor)
            break;

        const char* colon = memchr(cursor, ':', (size_t)(next - cursor));
        assert(colon);
        size_t name_size = (size_t)(colon - cursor);
        const char* value = colon + 1;
        while (value < next && (*value == ' ' || *value == '\t'))
            value++;
        size_t value_size = (size_t)(next - value);
        assert(source.headers_count < MAX_HEADERS);
        assert(name_size < MAX_HEADER_NAME && value_size < MAX_HEADER_VALUE);

        chttpx_header_t* header = &source.headers[source.headers_count++];
        memcpy(header->name, cursor, name_size);
        header->name[name_size] = '\0';
        memcpy(header->value, value, value_size);
        header->value[value_size] = '\0';

        if (strcasecmp(header->name, "Content-Type") == 0)
            snprintf(source.content_type, sizeof(source.content_type), "%s", header->value);
        cursor = next + 2;
    }

    const unsigned char* body = (const unsigned char*)header_end + 4;
    size_t body_size = strlen((const char*)body);
    char base_url[160];
    if (family == AF_INET6)
        snprintf(base_url, sizeof(base_url), "http://[::1]:%u", port);
    else
        snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", port);

    chttpx_response_t result = {0};
    assert(_chttpx_http2_call(&source, base_url, NULL, method, path, body, body_size,
                              source.content_type[0] ? source.content_type : NULL, &result) == cHTTPX_OK);

    size_t used = 0;
    int written = snprintf(response, response_size, "HTTP/2 %d %s\r\n", result.status, cHTTPX_StatusReason((uint16_t)result.status));
    assert(written > 0 && (size_t)written < response_size);
    used = (size_t)written;

    for (size_t i = 0; i < result.headers_count; i++)
    {
        written = snprintf(response + used, response_size - used, "%s: %s\r\n", result.headers[i].name, result.headers[i].value);
        assert(written >= 0 && (size_t)written < response_size - used);
        used += (size_t)written;
    }

    assert(used + 2 + result.body_size < response_size);
    memcpy(response + used, "\r\n", 2);
    used += 2;
    if (result.body_size)
        memcpy(response + used, result.body, result.body_size);
    used += result.body_size;
    response[used] = '\0';
    cHTTPX_ResponseCleanup(&result);
}

static void exchange(uint16_t port, const char* request, char* response, size_t response_size)
{
    exchange_family(port, AF_INET, request, response, response_size);
}

static void exchange_ipv6(uint16_t port, const char* request, char* response, size_t response_size)
{
    exchange_family(port, AF_INET6, request, response, response_size);
}

static chttpx_socket_t open_idle_connection(uint16_t port)
{
    chttpx_socket_t socket_fd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef CHTTPX_PLATFORM_WINDOWS
    assert(socket_fd != INVALID_SOCKET);
#else
    assert(socket_fd >= 0);
#endif
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(socket_fd, (struct sockaddr*)&address, sizeof(address)) == 0);
    return socket_fd;
}


static void wait_until_listening(chttpx_serv_t* server)
{
    for (int i = 0; i < 5000 && !__atomic_load_n(&server->listening, __ATOMIC_ACQUIRE); i++)
    {
#ifdef CHTTPX_PLATFORM_WINDOWS
        Sleep(1);
#else
        usleep(1000);
#endif
    }

    assert(__atomic_load_n(&server->listening, __ATOMIC_ACQUIRE));
}

int main(void)
{
    chttpx_app_t remote_app;
    assert(cHTTPX_AppInit(&remote_app) == cHTTPX_OK);

    chttpx_config_t remote_config = cHTTPX_DefaultConfig();
    assert(remote_config.network_mode == cHTTPX_NETWORK_DUAL);
    remote_config.port = 0;

    chttpx_serv_t* remote_server = cHTTPX_AppServer(&remote_app, "payments", &remote_config);
    assert(remote_server);

    chttpx_router_t remote_router = cHTTPX_RoutePathPrefix(remote_server, "");
    assert(cHTTPX_Post(&remote_router, "/process", internal_handler));
    assert(cHTTPX_Post(&remote_router, "/error", remote_error_handler));

    assert(cHTTPX_AppStart(&remote_app) == cHTTPX_OK);
    wait_until_listening(remote_server);

    chttpx_app_t app;
    assert(cHTTPX_AppInit(&app) == cHTTPX_OK);

    char language_en[] = "en";
    char language_ru[] = "ru";
    char fallback[] = "en";
    const char* languages[] = {language_en, language_ru};

    chttpx_config_t public_config = cHTTPX_DefaultConfig();
    public_config.port = 0;
    public_config.max_body_size = 32;
    public_config.languages = languages;
    public_config.languages_count = CHTTPX_ARRAY_LEN(languages);
    public_config.default_language = fallback;

    chttpx_serv_t* public_api = cHTTPX_AppServer(&app, "public", &public_config);
    assert(public_api);
    public_port = public_api->port;

    chttpx_config_t internal_config = cHTTPX_DefaultConfig();
    internal_config.port = 0;

    chttpx_serv_t* internal_api = cHTTPX_AppServer(&app, "internal", &internal_config);
    assert(internal_api);
    internal_port = internal_api->port;

    strcpy(language_ru, "xx");
    strcpy(fallback, "xx");

    chttpx_router_t public_router = cHTTPX_RoutePathPrefix(public_api, "");
    assert(cHTTPX_Post(&public_router, "/body", request_handler));
    assert(cHTTPX_Get(&public_router, "/ip", ip_handler));
    assert(cHTTPX_Options(&public_router, "/body", options_handler));
    assert(cHTTPX_Get(&public_router, "/empty", empty_handler));
    assert(cHTTPX_Post(&public_router, "/proxy", proxy_handler));
    assert(cHTTPX_Post(&public_router, "/proxy-remote", remote_proxy_handler));
    assert(cHTTPX_Post(&public_router, "/proxy-remote-error", remote_error_proxy_handler));
    assert(cHTTPX_Post(&public_router, "/proxy-unavailable", unavailable_proxy_handler));
    assert(cHTTPX_Post(&public_router, "/proxy-custom", custom_proxy_handler));

    chttpx_router_t internal_router = cHTTPX_RoutePathPrefix(internal_api, "");
    assert(cHTTPX_Post(&internal_router, "/process", internal_handler));

    char remote_url[128];
    snprintf(remote_url, sizeof(remote_url), "http://127.0.0.1:%u", remote_server->port);
    assert(cHTTPX_AppRemote(&app, "remote-payments", remote_url) == cHTTPX_OK);
    assert(cHTTPX_AppRemote(&app, "unavailable", "http://127.0.0.1:1") == cHTTPX_OK);

    const char* cors_origins[] = {
        "https://z.example.com",
        "https://example.com",
        "https://a.example.com",
    };
    cHTTPX_Cors(public_api, cors_origins, CHTTPX_ARRAY_LEN(cors_origins), NULL, NULL);

    assert(cHTTPX_AppStart(&app) == cHTTPX_OK);
    wait_until_listening(public_api);
    wait_until_listening(internal_api);

    char response[4096];

    chttpx_socket_t idle_connections[40];
    for (size_t i = 0; i < CHTTPX_ARRAY_LEN(idle_connections); i++)
        idle_connections[i] = open_idle_connection(public_port);

    struct timespec concurrency_start;
    struct timespec concurrency_end;
    clock_gettime(CLOCK_MONOTONIC, &concurrency_start);
    exchange(public_port, "GET /ip HTTP/2\r\nHost: localhost\r\n\r\n", response, sizeof(response));
    clock_gettime(CLOCK_MONOTONIC, &concurrency_end);
    double concurrency_seconds = (double)(concurrency_end.tv_sec - concurrency_start.tv_sec) + (double)(concurrency_end.tv_nsec - concurrency_start.tv_nsec) / 1000000000.0;
    assert(strstr(response, "HTTP/2 204 No Content") != NULL);
    assert(concurrency_seconds < 1.0);

    for (size_t i = 0; i < CHTTPX_ARRAY_LEN(idle_connections); i++)
        chttpx_close(idle_connections[i]);

    exchange_ipv6(public_port,
                  "GET /ip HTTP/2\r\n"
                  "Host: localhost\r\n"
                  "\r\n",
                  response, sizeof(response));
    assert(strstr(response, "HTTP/2 204 No Content") != NULL);
    assert(strcmp(observed_client_ip, "::1") == 0);

    exchange(public_port,
             "GET /ip HTTP/2\r\n"
             "Host: localhost\r\n"
             "\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 204 No Content") != NULL);
    assert(strcmp(observed_client_ip, "127.0.0.1") == 0);

    exchange(public_port,
             "OPTIONS /body HTTP/2\r\n"
             "Host: localhost\r\n"
             "Origin: https://example.com\r\n"
             "Access-Control-Request-Method: POST\r\n"
             "\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 204 No Content") != NULL);
    assert(strstr(response, "access-control-allow-origin: https://example.com") != NULL);
    assert(__atomic_load_n(&options_handler_calls, __ATOMIC_SEQ_CST) == 0);

    exchange(public_port,
             "OPTIONS /body HTTP/2\r\n"
             "Host: localhost\r\n"
             "\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 204 No Content") != NULL);
    assert(__atomic_load_n(&options_handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange(public_port,
             "GET /empty HTTP/2\r\n"
             "Host: localhost\r\n"
             "\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 500 Internal Server Error") != NULL);
    assert(strstr(response, "connection:") == NULL);

    exchange(public_port,
             "POST /body HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: 5\r\n"
             "Accept-Language: en;q=0.2, ru-RU;q=0.9\r\n"
             "X-Request-ID: integration-123\r\n"
             "\r\n"
             "hello",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 200 OK") != NULL);
    assert(strcmp(observed_body, "hello") == 0);
    assert(strcmp(observed_language, "ru") == 0);
    assert(strcmp(observed_request_id, "integration-123") == 0);

    exchange(public_port,
             "POST /proxy HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: 18\r\n"
             "\r\n"
             "{\"plan\":\"premium\"}",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 200 OK") != NULL);
    assert(strstr(response, "\"server\":\"internal\"") != NULL);
    assert(strcmp(internal_observed_body, "{\"plan\":\"premium\"}") == 0);

    exchange(public_port,
             "POST /proxy-remote HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: 18\r\n"
             "\r\n"
             "{\"plan\":\"premium\"}",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 200 OK") != NULL);
    assert(strstr(response, "\"server\":\"internal\"") != NULL);
    assert(last_remote_call_result == cHTTPX_OK);

    exchange(public_port,
             "POST /proxy-remote-error HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Length: 0\r\n"
             "\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 500 Internal Server Error") != NULL);
    assert(last_remote_call_result == cHTTPX_OK);

    exchange(public_port,
             "POST /proxy-unavailable HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Length: 0\r\n"
             "\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 503 Service Unavailable") != NULL);
    assert(last_remote_call_result == cHTTPX_ERR_UNAVAILABLE);

    exchange(public_port,
             "POST /proxy-custom HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: 18\r\n"
             "\r\n"
             "{\"plan\":\"premium\"}",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 200 OK") != NULL);
    assert(strcmp(internal_observed_body, "{\"custom\":true}") == 0);

    exchange(internal_port,
             "POST /process HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: 4\r\n"
             "\r\n"
             "ping",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 200 OK") != NULL);
    assert(strcmp(internal_observed_body, "ping") == 0);

    exchange(public_port,
             "POST /body HTTP/2\r\n"
             "Host: localhost\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: 40\r\n"
             "\r\n"
             "0123456789012345678901234567890123456789",
             response, sizeof(response));
    assert(strstr(response, "HTTP/2 413 Payload Too Large") != NULL);

    cHTTPX_AppShutdown(&app);
    cHTTPX_AppShutdown(&remote_app);

    puts("multi-server App integration tests passed");
    return 0;
}
