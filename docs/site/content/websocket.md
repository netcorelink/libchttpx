# WebSocket API — experimental

> **Status: experimental / beta. Do not treat the current implementation as production-ready.**

The repository contains `include/websocket.h` and `src/websocket.c`, but the current implementation is incomplete.

Current limitations:

- `src/websocket.c` is explicitly marked beta;
- the handshake does not currently calculate SHA-1 + Base64 for `Sec-WebSocket-Accept`;
- frame parsing/send/receive functionality is incomplete;
- `websocket.h` is not included by the umbrella `libchttpx.h` header.

This page documents the current surface only so users do not mistake it for stable functionality.

## Current types

`wsocket_frame_t` contains FIN, opcode, mask information, payload length, mask bytes, and payload.

`chttpx_wsocket_t` currently stores socket/connection state.

## Current declarations

```c
void cHTTPX_WSocketRegisterRoute(
    chttpx_router_t* r,
    const char* path,
    chttpx_wsocket_route_t handler
);

int cHTTPX_WSocketUpgrade(
    int client_socket,
    const char* sec_wsocket_key
);

int cHTTPX_WSocketSend(
    chttpx_wsocket_t* wsocket,
    const unsigned char* data,
    size_t len
);

int cHTTPX_WSocketRecv(
    chttpx_wsocket_t* wsocket,
    unsigned char* buffer,
    size_t len
);
```

Opcode constants include continuation, text, binary, close, ping, and pong.

## Recommendation

Use normal HTTP routes for production. WebSocket should be considered unfinished until handshake validation, frame parsing, masking, fragmentation, control frames, close lifecycle, tests, and public-header integration are completed.
