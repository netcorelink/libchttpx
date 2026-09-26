#include "cHTTPX_websocket.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    unsigned char data[512];
    size_t len;
    int calls;
    bool end_stream;
} capture_t;

static int capture_send(void* context, const unsigned char* data, size_t len, bool end_stream)
{
    capture_t* capture = context;
    assert(capture);
    assert(len <= sizeof(capture->data));
    memcpy(capture->data, data, len);
    capture->len = len;
    capture->end_stream = end_stream;
    capture->calls++;
    return cHTTPX_OK;
}

static void echo_message(chttpx_wsocket_t* websocket, const unsigned char* data, size_t len)
{
    assert(websocket->opcode == CHTTPX_WSOCKET_OPCODE_TEXT);
    assert(cHTTPX_WSocketSend(websocket, data, len) == cHTTPX_OK);
}

static void mask_frame(unsigned char* output, size_t* output_len, bool fin, int opcode, const unsigned char* payload, size_t payload_len)
{
    static const unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
    assert(payload_len <= 125);

    output[0] = (unsigned char)((fin ? 0x80 : 0) | opcode);
    output[1] = (unsigned char)(0x80 | payload_len);
    memcpy(output + 2, mask, sizeof(mask));
    for (size_t i = 0; i < payload_len; i++)
        output[6 + i] = payload[i] ^ mask[i & 3];
    *output_len = 6 + payload_len;
}

int main(void)
{
    chttpx_serv_t server;
    memset(&server, 0, sizeof(server));
    server.max_body_size = 1024;

    chttpx_request_t request;
    memset(&request, 0, sizeof(request));

    capture_t capture;
    memset(&capture, 0, sizeof(capture));

    chttpx_wsocket_t* websocket = _chttpx_websocket_create(&server, 1, &request, capture_send, &capture);
    assert(websocket);
    _chttpx_websocket_mark_connected(websocket);
    cHTTPX_WSocketOnMessage(websocket, echo_message);

    unsigned char frame[256];
    size_t frame_len = 0;

    const unsigned char first[] = {'h', 'e'};
    mask_frame(frame, &frame_len, false, CHTTPX_WSOCKET_OPCODE_TEXT, first, sizeof(first));
    assert(_chttpx_websocket_feed(websocket, frame, frame_len) == cHTTPX_OK);
    assert(capture.calls == 0);

    const unsigned char second[] = {'l', 'l', 'o'};
    mask_frame(frame, &frame_len, true, CHTTPX_WSOCKET_OPCODE_CONTINUATION, second, sizeof(second));
    assert(_chttpx_websocket_feed(websocket, frame, frame_len) == cHTTPX_OK);
    assert(capture.calls == 1);
    assert(capture.len == 7);
    assert(capture.data[0] == 0x81);
    assert(capture.data[1] == 5);
    assert(memcmp(capture.data + 2, "hello", 5) == 0);
    assert(!capture.end_stream);

    const unsigned char ping[] = {'o', 'k'};
    mask_frame(frame, &frame_len, true, CHTTPX_WSOCKET_OPCODE_PING, ping, sizeof(ping));
    assert(_chttpx_websocket_feed(websocket, frame, frame_len) == cHTTPX_OK);
    assert(capture.calls == 2);
    assert(capture.len == 4);
    assert(capture.data[0] == 0x8A);
    assert(capture.data[1] == 2);
    assert(memcmp(capture.data + 2, "ok", 2) == 0);

    const unsigned char close_payload[] = {0x03, 0xE8};
    mask_frame(frame, &frame_len, true, CHTTPX_WSOCKET_OPCODE_CLOSE, close_payload, sizeof(close_payload));
    assert(_chttpx_websocket_feed(websocket, frame, frame_len) == cHTTPX_OK);
    assert(capture.calls == 3);
    assert(capture.data[0] == 0x88);
    assert(capture.end_stream);
    assert(!websocket->connected);

    _chttpx_websocket_destroy(websocket);
    puts("websocket tests passed");
    return 0;
}
