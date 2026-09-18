#include "libchttpx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int handler_calls;
static int options_handler_calls;
static char observed_body[64];
static char observed_language[16];
static char observed_request_id[65];
static char payment_observed_body[64];
static uint16_t test_port;

static void request_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    __atomic_fetch_add(&handler_calls, 1, __ATOMIC_SEQ_CST);
    snprintf(observed_body, sizeof(observed_body), "%.*s", (int)req->body_size, req->body ? (const char*)req->body : "");
    snprintf(observed_language, sizeof(observed_language), "%s", req->language);
    snprintf(observed_request_id, sizeof(observed_request_id), "%s", req->request_id);
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "done");
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

static void payment_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    snprintf(payment_observed_body, sizeof(payment_observed_body), "%.*s", (int)req->body_size,
             req->body ? (const char*)req->body : "");
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"service\":\"payments\",\"accepted\":true}");
}

static void local_buy_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    if (cHTTPX_Call(req, "payments", cHTTPX_MethodPost, "/payments/create", res) != CHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "local payment call failed");
}

static void remote_buy_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    if (cHTTPX_Call(req, "remote-payments", cHTTPX_MethodPost, "/payments/create", res) != CHTTPX_OK)
        *res = cHTTPX_ResError(cHTTPX_StatusBadGateway, "remote payment call failed");
}

static void exchange(const char* request, char* response, size_t response_size)
{
    chttpx_socket_t socket_fd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef CHTTPX_PLATFORM_WINDOWS
    assert(socket_fd != INVALID_SOCKET);
#else
    assert(socket_fd >= 0);
#endif

    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(test_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(socket_fd, (struct sockaddr*)&address, sizeof(address)) == 0);
    assert(cHTTPX_SendAll(socket_fd, request, strlen(request)) == CHTTPX_OK);
#ifdef CHTTPX_PLATFORM_WINDOWS
    shutdown(socket_fd, SD_SEND);
#else
    shutdown(socket_fd, SHUT_WR);
#endif

    size_t total = 0;
    while (total + 1 < response_size)
    {
        int received = recv(socket_fd, response + total, response_size - total - 1, 0);
        if (received <= 0)
            break;
        total += (size_t)received;
    }
    response[total] = '\0';
    chttpx_close(socket_fd);
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
    assert(cHTTPX_AppInit(&remote_app) == CHTTPX_OK);

    chttpx_config_t remote_config = cHTTPX_DefaultConfig();
    remote_config.port = 0;
    chttpx_serv_t* remote_server = cHTTPX_AppMicroserverWithConfig(&remote_app, "payments-remote-host", &remote_config);
    assert(remote_server);

    chttpx_router_t remote_router = cHTTPX_RoutePathPrefix(remote_server, "/payments");
    assert(cHTTPX_Post(&remote_router, "/create", payment_handler));
    assert(cHTTPX_AppStart(&remote_app) == CHTTPX_OK);
    wait_until_listening(remote_server);

    chttpx_app_t app;
    assert(cHTTPX_AppInit(&app) == CHTTPX_OK);

    char language_en[] = "en";
    char language_ru[] = "ru";
    char fallback[] = "en";
    const char* languages[] = {language_en, language_ru};

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 0;
    config.max_body_size = 32;
    config.languages = languages;
    config.languages_count = CHTTPX_ARRAY_LEN(languages);
    config.default_language = fallback;

    chttpx_serv_t* server = cHTTPX_AppMicroserverWithConfig(&app, "main", &config);
    assert(server);
    test_port = server->port;

    chttpx_serv_t* payments = cHTTPX_AppMicroservice(&app, "payments");
    assert(payments);

    strcpy(language_ru, "xx");
    strcpy(fallback, "xx");

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    cHTTPX_Post(&router, "/body", request_handler);
    cHTTPX_Options(&router, "/body", options_handler);
    cHTTPX_Get(&router, "/empty", empty_handler);
    cHTTPX_Post(&router, "/buy", local_buy_handler);
    cHTTPX_Post(&router, "/buy-remote", remote_buy_handler);

    chttpx_router_t payment_router = cHTTPX_RoutePathPrefix(payments, "/payments");
    cHTTPX_Post(&payment_router, "/create", payment_handler);

    char remote_url[128];
    snprintf(remote_url, sizeof(remote_url), "http://127.0.0.1:%u", remote_server->port);
    assert(cHTTPX_AppRemote(&app, "remote-payments", remote_url) == CHTTPX_OK);

    const char* cors_origins[] = {"https://example.com"};
    cHTTPX_Cors(server, cors_origins, CHTTPX_ARRAY_LEN(cors_origins), NULL, NULL);

    assert(cHTTPX_AppStart(&app) == CHTTPX_OK);
    wait_until_listening(server);

    char response[4096];

    exchange("OPTIONS /body HTTP/1.1\r
Host: localhost\r
Origin: https://example.com\r
"
             "Access-Control-Request-Method: POST\r
\r
",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 204 No Content") != NULL);
    assert(strstr(response, "Access-Control-Allow-Origin: https://example.com") != NULL);
    assert(__atomic_load_n(&options_handler_calls, __ATOMIC_SEQ_CST) == 0);

    exchange("OPTIONS /body HTTP/1.1\r
Host: localhost\r
\r
", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 204 No Content") != NULL);
    assert(__atomic_load_n(&options_handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange("GET /empty HTTP/1.1\r
Host: localhost\r
\r
", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 500 Internal Server Error") != NULL);
    assert(strstr(response, "Connection: close") != NULL);

    exchange("POST /body HTTP/1.1\r
Host: localhost\r
Content-Type: text/plain\r
Transfer-Encoding: chunked\r
"
             "Accept-Language: en;q=0.2, ru-RU;q=0.9\r
X-Request-ID: integration-123\r
\r
5\r
hello\r
0\r
\r
",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "X-Request-ID: integration-123") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);
    assert(strcmp(observed_body, "hello") == 0);
    assert(strcmp(observed_language, "ru") == 0);
    assert(strcmp(observed_request_id, "integration-123") == 0);

    exchange("POST /buy HTTP/1.1\r
Host: localhost\r
Content-Type: application/json\r
Content-Length: 18\r
"
             "X-Request-ID: local-call\r
\r
{\"plan\":\"premium\"}",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "\"service\":\"payments\"") != NULL);
    assert(strcmp(payment_observed_body, "{\"plan\":\"premium\"}") == 0);

    exchange("POST /buy-remote HTTP/1.1\r
Host: localhost\r
Content-Type: application/json\r
Content-Length: 18\r
"
             "X-Request-ID: remote-call\r
\r
{\"plan\":\"premium\"}",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "\"service\":\"payments\"") != NULL);

    exchange("POST /body HTTP/1.1\r
Host: localhost\r
Content-Type: application/json\r
Content-Length: 100\r
\r
", response,
             sizeof(response));
    assert(strstr(response, "HTTP/1.1 413 Payload Too Large") != NULL);

    exchange("POST /body HTTP/1.1\r
Host: localhost\r
Content-Type: text/plain\r
"
             "Content-Length: 5\r
Content-Length: 6\r
\r
hello",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);

    exchange("POST /body HTTP/1.1\r
Host: localhost\r
Transfer-Encoding: gzip\r
\r
", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);

    cHTTPX_AppShutdown(&app);
    cHTTPX_AppShutdown(&remote_app);

    puts("server/app integration tests passed");
    return 0;
}
