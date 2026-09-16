#include "libchttpx.h"
#include "utils.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static volatile int handler_calls;
static char observed_body[64];
static char observed_language[16];
static char observed_request_id[65];
static uint16_t test_port;

static void request_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    handler_calls++;
    snprintf(observed_body, sizeof(observed_body), "%.*s", (int)req->body_size, req->body ? (const char*)req->body : "");
    snprintf(observed_language, sizeof(observed_language), "%s", req->language);
    snprintf(observed_request_id, sizeof(observed_request_id), "%s", req->request_id);
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "done");
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
    assert(socket_fd != INVALID_SOCKET);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(test_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(connect(socket_fd, (struct sockaddr*)&address, sizeof(address)) == 0);
    assert(cHTTPX_SendAll(socket_fd, request, strlen(request)) == 0);
    shutdown(socket_fd, SD_SEND);

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
    const char* languages[] = {"en", "ru"};
    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 0;
    config.max_body_size = 16;
    config.languages = languages;
    config.languages_count = CHTTPX_ARRAY_LEN(languages);
    config.default_language = "en";
    assert(cHTTPX_InitWithConfig(&server, &config) == CHTTPX_OK);
    test_port = server.port;

    chttpx_router_t router = cHTTPX_RoutePathPrefix("");
    cHTTPX_Post(&router, "/body", request_handler);

    thread_t thread;
    assert(_thread_create(&thread, listen_thread, NULL) == 0);
    while (!server.listening)
        Sleep(1);

    char response[4096];
    exchange("OPTIONS /body HTTP/1.1\r\nHost: localhost\r\nOrigin: https://example.com\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 204 No Content") != NULL);
    assert(handler_calls == 0);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n"
             "Accept-Language: en;q=0.2, ru-RU;q=0.9\r\nX-Request-ID: integration-123\r\n\r\n5\r\nhello\r\n0\r\n\r\n",
             response, sizeof(response));
    if (!strstr(response, "HTTP/1.1 200 OK"))
        fprintf(stderr, "unexpected chunked response: %s\n", response);
    assert(strstr(response, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response, "X-Request-ID: integration-123") != NULL);
    assert(handler_calls == 1);
    assert(strcmp(observed_body, "hello") == 0);
    assert(strcmp(observed_language, "ru") == 0);
    assert(strcmp(observed_request_id, "integration-123") == 0);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: 100\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 413 Payload Too Large") != NULL);
    assert(handler_calls == 1);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\nbad\r\n0\r\n\r\n",
             response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(handler_calls == 1);

    exchange("POST /body HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 12x\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(handler_calls == 1);

    exchange("POST /body?value=%GG HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: 0\r\n\r\n", response, sizeof(response));
    assert(strstr(response, "HTTP/1.1 400 Bad Request") != NULL);
    assert(handler_calls == 1);

    cHTTPX_Shutdown();
    _thread_join(thread);
    puts("server integration tests passed");
    return 0;
}
