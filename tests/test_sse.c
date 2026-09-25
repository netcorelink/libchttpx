#include "libchttpx.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    char output[4096];
    size_t size;
    bool opened;
    bool closed;
    bool connected;
} sse_mock_t;

static const char* response_header(const chttpx_response_t* res, const char* name)
{
    for (size_t i = 0; i < res->headers_count; i++)
        if (strcasecmp(res->headers[i].name, name) == 0)
            return res->headers[i].value;
    return NULL;
}

static int mock_open(void* context, const struct chttpx_response* response)
{
    sse_mock_t* mock = context;
    assert(mock);
    assert(response);
    assert(response->status == cHTTPX_StatusOK);
    assert(strcmp(response->content_type, cHTTPX_CTYPE_SSE) == 0);
    assert(strcmp(response_header(response, "Cache-Control"), "no-cache") == 0);
    assert(strcmp(response_header(response, "X-Accel-Buffering"), "no") == 0);
    assert(response->compression_disabled);
    mock->opened = true;
    mock->connected = true;
    return cHTTPX_OK;
}

static int mock_write(void* context, const void* data, size_t size)
{
    sse_mock_t* mock = context;
    if (!mock || !mock->connected || !data || size > sizeof(mock->output) - mock->size)
        return cHTTPX_ERR_IO;
    memcpy(mock->output + mock->size, data, size);
    mock->size += size;
    return cHTTPX_OK;
}

static int mock_close(void* context)
{
    sse_mock_t* mock = context;
    if (!mock)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    mock->closed = true;
    mock->connected = false;
    return cHTTPX_OK;
}

static bool mock_connected(void* context)
{
    const sse_mock_t* mock = context;
    return mock && mock->connected;
}

int main(void)
{
    sse_mock_t mock = {0};
    chttpx_request_t req = {0};
    chttpx_response_t res = {0};

    snprintf(req.request_id, sizeof(req.request_id), "%s", "sse-unit-test");
    req._stream_transport = (chttpx_stream_transport_t){
        .context = &mock,
        .open = mock_open,
        .write = mock_write,
        .close = mock_close,
        .connected = mock_connected,
    };

    chttpx_sse_t* sse = cHTTPX_SSEOpen(&req, &res);
    assert(sse);
    assert(mock.opened);
    assert(cHTTPX_SSEConnected(sse));
    assert(strcmp(response_header(&res, "X-Request-ID"), "sse-unit-test") == 0);

    assert(cHTTPX_SSESend(sse, "progress", "42", "first\nsecond\r\nthird\r") == cHTTPX_OK);
    assert(cHTTPX_SSERetry(sse, 5000) == cHTTPX_OK);
    assert(cHTTPX_SSEComment(sse, "keep\nalive") == cHTTPX_OK);
    assert(cHTTPX_SSEHeartbeat(sse) == cHTTPX_OK);

    static const char expected[] =
        "event: progress\n"
        "id: 42\n"
        "data: first\n"
        "data: second\n"
        "data: third\n"
        "data: \n"
        "\n"
        "retry: 5000\n\n"
        ": keep\n"
        ": alive\n"
        "\n"
        ":\n\n";

    assert(mock.size == sizeof(expected) - 1);
    assert(memcmp(mock.output, expected, sizeof(expected) - 1) == 0);

    assert(cHTTPX_SSESend(sse, "bad\nevent", NULL, "data") == cHTTPX_ERR_INVALID_ARGUMENT);
    assert(cHTTPX_SSESend(sse, NULL, "bad\nid", "data") == cHTTPX_ERR_INVALID_ARGUMENT);

    assert(cHTTPX_SSEClose(sse) == cHTTPX_OK);
    assert(cHTTPX_SSEClose(sse) == cHTTPX_OK);
    assert(mock.closed);
    assert(!cHTTPX_SSEConnected(sse));

    cHTTPX_ResponseCleanup(&res);
    cHTTPX_RequestCleanup(&req);

    puts("SSE formatting tests passed");
    return 0;
}
