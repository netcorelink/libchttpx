# WebSocket

libchttpx supports WebSocket over HTTP/2 using the Extended CONNECT mechanism from RFC 8441. The server advertises `SETTINGS_ENABLE_CONNECT_PROTOCOL=1`, accepts `:method = CONNECT` with `:protocol = websocket`, and then carries normal RFC 6455 WebSocket frames inside HTTP/2 DATA frames.

There is intentionally no HTTP/1.1 `Upgrade: websocket` fallback in the HTTP/2-only server.

## Server route

```c
#include <libchttpx.h>

static void on_message(chttpx_wsocket_t* ws, const unsigned char* data, size_t len)
{
    if (ws->opcode == CHTTPX_WSOCKET_OPCODE_TEXT)
        cHTTPX_WSocketSend(ws, data, len);
    else
        cHTTPX_WSocketSendBinary(ws, data, len);
}

static void on_close(chttpx_wsocket_t* ws, uint16_t code, const unsigned char* reason, size_t len)
{
    (void)ws;
    (void)code;
    (void)reason;
    (void)len;
}

static void chat_socket(chttpx_wsocket_t* ws)
{
    /* ws->request contains the HTTP/2 handshake headers. */
    cHTTPX_WSocketOnMessage(ws, on_message);
    cHTTPX_WSocketOnClose(ws, on_close);
}

chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "/api");
cHTTPX_WSocketRegisterRoute(&router, "/chat", chat_socket);
```

The resulting endpoint is `/api/chat`. Query parameters are ignored only for route matching; the complete path remains available through `ws->request->path`.

## API

- `cHTTPX_WSocketRegisterRoute()` registers a WebSocket endpoint.
- `cHTTPX_WSocketOnMessage()` installs the text/binary message callback.
- `cHTTPX_WSocketOnClose()` installs the close callback.
- `cHTTPX_WSocketSend()` sends one text message.
- `cHTTPX_WSocketSendBinary()` sends one binary message.
- `cHTTPX_WSocketPing()` sends a ping control frame.
- `cHTTPX_WSocketClose()` starts an orderly close and ends the HTTP/2 stream.
- `cHTTPX_WSocketSetData()` / `cHTTPX_WSocketData()` attach application state to a connection.

Inside the message callback, `ws->opcode` is `CHTTPX_WSOCKET_OPCODE_TEXT` or `CHTTPX_WSOCKET_OPCODE_BINARY`.

## Protocol handling

The implementation supports masked client frames, 16-bit and 64-bit payload lengths, fragmented text/binary messages, automatic PONG replies, close frames, UTF-8 validation for text messages and close reasons, and configured message-size limits. Invalid protocol input is closed with the appropriate WebSocket close code.

A WebSocket handshake must use `Sec-WebSocket-Version: 13`. The HTTP/1.1-oriented `Sec-WebSocket-Key` / `Sec-WebSocket-Accept` exchange is not used by RFC 8441 Extended CONNECT.

## Legacy functions

`cHTTPX_WSocketUpgrade()` and synchronous `cHTTPX_WSocketRecv()` are retained for source compatibility but return `cHTTPX_ERR_UNAVAILABLE`. WebSocket handling is event-driven so one open socket does not block other multiplexed HTTP/2 streams.
