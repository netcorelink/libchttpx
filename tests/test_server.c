#include "libchttpx.h"
#include "utils.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int handler_calls;
static int options_handler_calls;
static char observed_body[64];
static char observed_language[16];
static char observed_request_id[65];
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

static void* listen_thread(void* value)
{
    (void)value;
    cHTTPX_Listen();
    return NULL;
}

static void exchange(const char* request, char* response, size_t response_size)
{
    chttpx_socket_t socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(socket_fd != (chttpx_socket_t)-1);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(test_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(socket_fd, (struct sockaddr*)&address, sizeof(address)) == 0);
    assert(cHTTPX_SendAll(socket_fd, request, strlen(request)) == 0);
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

int main(void)
{
    chttpx_serv_t server;
    char language_en[] = "en";
    char language_ru[] = "ru";
    char fallback[] = "en";
    const char* languages[] = {language_en, language_ru};
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 0;
    config.max_body_size = 16;
    config.languages = languages;
    config.languages_count = CHTTPX_ARRAY_LEN(languages);
    config.default_language = fallback;
    assert(cHTTPX_InitWithConfig(&server, &config) == CHTTPX_OK);
    test_port = server.port;

    /* The server must own its language configuration after Init returns. */
    strcpy(language_ru, "xx");
    strcpy(fallback, "xx");

    chttpx_router_t router = cHTTPX_RoutePathPrefix("");
    cHTTPX_Post(&router, "/body", request_handler);
    cHTTPX_Options(&router, "/body", options_handler);
    cHTTPX_Get(&router, "/empty", empty_handler);

    const char* cors_origins[] = {"https://example.com"};
    cHTTPX_Cors(cors_origins, CHTTPX_ARRAY_LEN(cors_origins), NULL, NULL);

    thread_t thread;
    assert(_thread_create(&thread, listen_thread, NULL) == 0);
    while (!__atomic_load_n(&server.listening, __ATOMIC_ACQUIRE))
#ifdef CHTTPX_PLATFORM_WINDOWS
        Sleep(1);
#else
        usleep(1000);
#endif

    char response[4096];
    exchange("OPTIONS /body HTTP/1.1\r\nHost: localhost\r\nOrigin: https://example.com\r\n"
             "Access-Control-Request-Method: POST\r\n\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 204 No Content") != NULL);
    assert(strstr(response, "Access-Control-Allow-Origin: https://example.com") != NULL);
    assert(__atomic_load_n(&options_handler_calls, __ATOMIC_SEQ_CST) == 0);

    exchange("OPTIONS /body HTTP/1.1\r\nHost: localhost\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 204 No Content") != NULL);
    assert(__atomic_load_n(&options_handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange("GET /empty HTTP/1.1\r\nHost: localhost\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 500 Internal Server Error") != NULL);
    assert(strstr(response, "Connection: close") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 0);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n"
             "Accept-Language: en;q=0.2, ru-RU;q=0.9\r\nX-Request-ID: integration-123\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
             response, sizeof(response));
    if (!strstr(response, "HTTP/1.1 200 OK"))
        fprintf(stderr, "unexpected chunked response: %s\n", response);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "X-Request-ID: integration-123") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);
    assert(strcmp(observed_body, "hello") == 0);
    assert(strcmp(observed_language, "ru") == 0);
    assert(strcmp(observed_request_id, "integration-123") == 0);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 413 Payload Too Large") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\nbad\r\n0\r\n\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 12x\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange("POST /body?value=%GG HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\n"
             "Content-Length: 5\r\nContent-Length: 6\r\n\r\nhello",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: gzip\r\n\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(__atomic_load_n(&handler_calls, __ATOMIC_SEQ_CST) == 1);

    cHTTPX_Shutdown();
    _thread_join(thread);
    puts("server integration tests passed");
    return 0;
}
