# WebSocket

libchttpx поддерживает WebSocket поверх HTTP/2 через Extended CONNECT из RFC 8441. Сервер объявляет `SETTINGS_ENABLE_CONNECT_PROTOCOL=1`, принимает `:method = CONNECT` вместе с `:protocol = websocket`, после чего обычные WebSocket-фреймы RFC 6455 передаются внутри HTTP/2 DATA.

Fallback на HTTP/1.1 `Upgrade: websocket` специально не добавляется, потому что сервер в `develop` работает как HTTP/2-only.

## WebSocket-маршрут

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
    /* В ws->request доступны заголовки HTTP/2 handshake. */
    cHTTPX_WSocketOnMessage(ws, on_message);
    cHTTPX_WSocketOnClose(ws, on_close);
}

chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "/api");
cHTTPX_WSocketRegisterRoute(&router, "/chat", chat_socket);
```

Итоговый endpoint — `/api/chat`. Query string не участвует только в поиске маршрута; полный путь остаётся доступен через `ws->request->path`.

## API

- `cHTTPX_WSocketRegisterRoute()` — регистрация WebSocket endpoint.
- `cHTTPX_WSocketOnMessage()` — callback для text/binary сообщений.
- `cHTTPX_WSocketOnClose()` — callback закрытия.
- `cHTTPX_WSocketSend()` — отправка text message.
- `cHTTPX_WSocketSendBinary()` — отправка binary message.
- `cHTTPX_WSocketPing()` — отправка PING.
- `cHTTPX_WSocketClose()` — корректное закрытие WebSocket и HTTP/2 stream.
- `cHTTPX_WSocketSetData()` / `cHTTPX_WSocketData()` — пользовательский context соединения.

В `OnMessage` поле `ws->opcode` содержит `CHTTPX_WSOCKET_OPCODE_TEXT` или `CHTTPX_WSOCKET_OPCODE_BINARY`.

## Что обрабатывает библиотека

Реализованы client masking, payload lengths 16/64 bit, fragmentation, автоматический PONG, close frames, UTF-8 validation для text/close reason и ограничение размера сообщения через настройки сервера. При нарушении протокола библиотека закрывает WebSocket соответствующим close code.

Handshake должен передавать `Sec-WebSocket-Version: 13`. HTTP/1.1-механизм с `Sec-WebSocket-Key` / `Sec-WebSocket-Accept` в RFC 8441 Extended CONNECT не используется.

## Старый API

`cHTTPX_WSocketUpgrade()` и синхронный `cHTTPX_WSocketRecv()` оставлены для source compatibility, но возвращают `cHTTPX_ERR_UNAVAILABLE`. Основной API событийный, чтобы одно WebSocket-соединение не блокировало остальные HTTP/2 streams.
