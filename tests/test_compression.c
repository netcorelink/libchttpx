#include "libchttpx.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#define RESPONSE_CAPACITY 65536
#define LARGE_BODY_SIZE 4096

typedef struct
{
    unsigned char bytes[RESPONSE_CAPACITY];
    size_t size;
    size_t header_size;
    char headers[8192];
} test_response_t;

static unsigned char large_text[LARGE_BODY_SIZE];
static unsigned char binary_body[LARGE_BODY_SIZE];

static void fill_bodies(void)
{
    for (size_t i = 0; i < LARGE_BODY_SIZE; i++)
    {
        large_text[i] = (unsigned char)('A' + (i % 4));
        binary_body[i] = (unsigned char)(i & 0xff);
    }
}

static void large_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResBinary(cHTTPX_StatusOK, "text/plain; charset=utf-8", large_text, sizeof(large_text));
}

static void small_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResMessage(cHTTPX_StatusOK, "small");
}

static void binary_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResBinary(cHTTPX_StatusOK, "image/png", binary_body, sizeof(binary_body));
}

static void response_disabled_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResBinary(cHTTPX_StatusOK, "text/plain", large_text, sizeof(large_text));
    cHTTPX_ResponseCompression(res, false);
}

static void already_encoded_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResBinary(cHTTPX_StatusOK, "text/plain", large_text, sizeof(large_text));
    assert(cHTTPX_HeaderAdd(res, "Content-Encoding", "br") == 0);
}

static void no_transform_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResBinary(cHTTPX_StatusOK, "text/plain", large_text, sizeof(large_text));
    assert(cHTTPX_HeaderAdd(res, "Cache-Control", "public, no-transform") == 0);
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

static const unsigned char* find_bytes(const unsigned char* haystack, size_t haystack_size, const char* needle)
{
    size_t needle_size = strlen(needle);
    if (!needle_size || needle_size > haystack_size)
        return NULL;

    for (size_t i = 0; i + needle_size <= haystack_size; i++)
        if (memcmp(haystack + i, needle, needle_size) == 0)
            return haystack + i;

    return NULL;
}

static test_response_t exchange(uint16_t port, const char* request)
{
    test_response_t response;
    memset(&response, 0, sizeof(response));

    chttpx_socket_t socket_fd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef CHTTPX_PLATFORM_WINDOWS
    assert(socket_fd != INVALID_SOCKET);
#else
    assert(socket_fd >= 0);
#endif

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    assert(connect(socket_fd, (struct sockaddr*)&address, sizeof(address)) == 0);
    assert(cHTTPX_SendAll(socket_fd, request, strlen(request)) == cHTTPX_OK);

#ifdef CHTTPX_PLATFORM_WINDOWS
    shutdown(socket_fd, SD_SEND);
#else
    shutdown(socket_fd, SHUT_WR);
#endif

    while (response.size < sizeof(response.bytes))
    {
        int received = recv(socket_fd, response.bytes + response.size, sizeof(response.bytes) - response.size, 0);
        if (received <= 0)
            break;
        response.size += (size_t)received;
    }

    chttpx_close(socket_fd);

    const unsigned char* delimiter = find_bytes(response.bytes, response.size, "\r\n\r\n");
    assert(delimiter);

    response.header_size = (size_t)(delimiter - response.bytes) + 4;
    assert(response.header_size < sizeof(response.headers));
    memcpy(response.headers, response.bytes, response.header_size);
    response.headers[response.header_size] = '\0';
    return response;
}

static size_t response_body_size(const test_response_t* response)
{
    assert(response && response->size >= response->header_size);
    return response->size - response->header_size;
}

static const unsigned char* response_body(const test_response_t* response)
{
    return response->bytes + response->header_size;
}

static void assert_header(const test_response_t* response, const char* text)
{
    assert(strstr(response->headers, text) != NULL);
}

static void assert_no_header(const test_response_t* response, const char* text)
{
    assert(strstr(response->headers, text) == NULL);
}

static void assert_identity_large(const test_response_t* response)
{
    assert_header(response, "HTTP/1.1 200 OK");
    assert_no_header(response, "Content-Encoding:");
    assert(response_body_size(response) == sizeof(large_text));
    assert(memcmp(response_body(response), large_text, sizeof(large_text)) == 0);
}

static void assert_gzip_large(const test_response_t* response)
{
    assert_header(response, "HTTP/1.1 200 OK");
    assert_header(response, "Content-Encoding: gzip");
    assert_header(response, "Vary: Accept-Encoding");
    assert(response_body_size(response) < sizeof(large_text));

    unsigned char decompressed[LARGE_BODY_SIZE + 64];
    z_stream stream;
    memset(&stream, 0, sizeof(stream));
    assert(inflateInit2(&stream, 15 + 16) == Z_OK);

    stream.next_in = (Bytef*)response_body(response);
    stream.avail_in = (uInt)response_body_size(response);
    stream.next_out = decompressed;
    stream.avail_out = sizeof(decompressed);

    int result = inflate(&stream, Z_FINISH);
    assert(result == Z_STREAM_END);
    size_t decompressed_size = sizeof(decompressed) - stream.avail_out;
    inflateEnd(&stream);

    assert(decompressed_size == sizeof(large_text));
    assert(memcmp(decompressed, large_text, sizeof(large_text)) == 0);
}

static int failing_provider(const unsigned char* input,
                            size_t input_size,
                            int level,
                            unsigned char** output,
                            size_t* output_size,
                            void* user_data)
{
    (void)input;
    (void)input_size;
    (void)level;
    (void)user_data;
    *output = NULL;
    *output_size = 0;
    return cHTTPX_ERR_COMPRESSION;
}

int main(void)
{
    fill_bodies();
    chttpx_app_t app;
    assert(cHTTPX_AppInit(&app) == cHTTPX_OK);

    chttpx_config_t server_config = cHTTPX_DefaultConfig();
    server_config.port = 0;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "compression", &server_config);
    assert(server);

    chttpx_compression_config_t compression = cHTTPX_CompressionDefault();
    compression.min_size = 128;
    compression.level = 5;
    assert(cHTTPX_CompressionUse(server, &compression) == cHTTPX_OK);

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    assert(cHTTPX_Get(&router, "/large", large_handler));
    assert(cHTTPX_Get(&router, "/small", small_handler));
    assert(cHTTPX_Get(&router, "/binary", binary_handler));
    assert(cHTTPX_Get(&router, "/response-disabled", response_disabled_handler));
    assert(cHTTPX_Get(&router, "/encoded", already_encoded_handler));
    assert(cHTTPX_Get(&router, "/no-transform", no_transform_handler));

    chttpx_route_t* route_disabled = cHTTPX_Get(&router, "/route-disabled", large_handler);
    assert(route_disabled);
    assert(cHTTPX_RouteCompression(route_disabled, false) == cHTTPX_OK);

    assert(cHTTPX_AppStart(&app) == cHTTPX_OK);
    wait_until_listening(server);

    test_response_t response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip;q=0.5, identity;q=1\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: br;q=1, gzip;q=0.8, identity;q=0.2\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: GZIP ; q=1.0, identity;q=0\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: *;q=1, identity;q=0\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip;q=0\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip;q=0, identity;q=0\r\n\r\n");
    assert_header(&response, "HTTP/1.1 406 Not Acceptable");
    assert(response_body_size(&response) == 0);

    response = exchange(
        server->port,
        "GET /small HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "HTTP/1.1 200 OK");
    assert_no_header(&response, "Content-Encoding:");

    response = exchange(
        server->port,
        "GET /binary HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "HTTP/1.1 200 OK");
    assert_no_header(&response, "Content-Encoding:");
    assert(response_body_size(&response) == sizeof(binary_body));

    response = exchange(
        server->port,
        "GET /response-disabled HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /route-disabled HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /encoded HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "Content-Encoding: br");
    assert_no_header(&response, "Content-Encoding: gzip");

    response = exchange(
        server->port,
        "GET /no-transform HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "Cache-Control: public, no-transform");
    assert_no_header(&response, "Content-Encoding:");

    chttpx_compression_provider_t failing = {
        .encoding = "gzip",
        .encode_buffer = failing_provider,
        .user_data = NULL,
    };
    chttpx_compression_config_t failing_config = cHTTPX_CompressionDefault();
    failing_config.min_size = 128;
    failing_config.providers = &failing;
    failing_config.providers_count = 1;
    assert(cHTTPX_CompressionUse(server, &failing_config) == cHTTPX_OK);

    response = exchange(
        server->port,
        "GET /large HTTP/1.1\r\nHost: localhost\r\nAccept-Encoding: gzip;q=1, identity;q=0.5\r\n\r\n");
    assert_identity_large(&response);

    cHTTPX_AppShutdown(&app);
    puts("compression tests passed");
    return 0;
}
