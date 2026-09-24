#include "libchttpx.h"
#include "cHTTPX_http2.h"

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
        cursor = next + 2;
    }

    char base_url[128];
    snprintf(base_url, sizeof(base_url), "http://127.0.0.1:%u", port);
    chttpx_response_t result = {0};
    assert(_chttpx_http2_call(&source, base_url, NULL, method, path, NULL, 0, NULL, &result) == cHTTPX_OK);

    size_t used = 0;
    int written = snprintf(response.headers, sizeof(response.headers), "HTTP/2 %d %s\r\n",
                           result.status, cHTTPX_StatusReason((uint16_t)result.status));
    assert(written > 0 && (size_t)written < sizeof(response.headers));
    used = (size_t)written;

    for (size_t i = 0; i < result.headers_count; i++)
    {
        written = snprintf(response.headers + used, sizeof(response.headers) - used, "%s: %s\r\n",
                           result.headers[i].name, result.headers[i].value);
        assert(written >= 0 && (size_t)written < sizeof(response.headers) - used);
        used += (size_t)written;
    }
    assert(used + 2 < sizeof(response.headers));
    memcpy(response.headers + used, "\r\n", 3);
    used += 2;

    response.header_size = used;
    assert(response.header_size + result.body_size <= sizeof(response.bytes));
    memcpy(response.bytes, response.headers, response.header_size);
    if (result.body_size)
        memcpy(response.bytes + response.header_size, result.body, result.body_size);
    response.size = response.header_size + result.body_size;
    cHTTPX_ResponseCleanup(&result);
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
    assert_header(response, "HTTP/2 200 OK");
    assert_no_header(response, "content-encoding:");
    assert(response_body_size(response) == sizeof(large_text));
    assert(memcmp(response_body(response), large_text, sizeof(large_text)) == 0);
}

static void assert_gzip_large(const test_response_t* response)
{
    assert_header(response, "HTTP/2 200 OK");
    assert_header(response, "content-encoding: gzip");
    assert_header(response, "vary: Accept-Encoding");
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
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip;q=0.5, identity;q=1\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: br;q=1, gzip;q=0.8, identity;q=0.2\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: GZIP ; q=1.0, identity;q=0\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: *;q=1, identity;q=0\r\n\r\n");
    assert_gzip_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip;q=0\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip;q=0, identity;q=0\r\n\r\n");
    assert_header(&response, "HTTP/2 406 Not Acceptable");
    assert(response_body_size(&response) == 0);

    response = exchange(
        server->port,
        "GET /small HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "HTTP/2 200 OK");
    assert_no_header(&response, "content-encoding:");

    response = exchange(
        server->port,
        "GET /binary HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "HTTP/2 200 OK");
    assert_no_header(&response, "content-encoding:");
    assert(response_body_size(&response) == sizeof(binary_body));

    response = exchange(
        server->port,
        "GET /response-disabled HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /route-disabled HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_identity_large(&response);

    response = exchange(
        server->port,
        "GET /encoded HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "content-encoding: br");
    assert_no_header(&response, "content-encoding: gzip");

    response = exchange(
        server->port,
        "GET /no-transform HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip\r\n\r\n");
    assert_header(&response, "cache-control: public, no-transform");
    assert_no_header(&response, "content-encoding:");

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
        "GET /large HTTP/2\r\nHost: localhost\r\nAccept-Encoding: gzip;q=1, identity;q=0.5\r\n\r\n");
    assert_identity_large(&response);

    cHTTPX_AppShutdown(&app);
    puts("compression tests passed");
    return 0;
}
