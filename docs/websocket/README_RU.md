# WebSocket API — experimental

> **Статус: experimental / beta. Текущую реализацию нельзя считать production-ready.**

В репозитории есть `websocket.h` и `websocket.c`, но функционал пока неполный.

Текущие ограничения:

- implementation прямо помечена как beta;
- handshake пока не вычисляет SHA-1 + Base64 для `Sec-WebSocket-Accept`;
- frame parsing/send/recv реализованы не полностью;
- WebSocket API не входит в обычный umbrella public header.

Существующие declarations:

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

Перед production use нужно завершить handshake validation, frame parser, masking, fragmentation, control frames, close lifecycle и tests.
