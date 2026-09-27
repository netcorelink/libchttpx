# Server-Sent Events (SSE)

В libchttpx есть first-class API для Server-Sent Events — односторонней передачи событий сервером клиенту поверх HTTP/2. SSE подходит для уведомлений, прогресса задач, телеметрии, dashboard/event feed и потоковой выдачи данных, когда двусторонний WebSocket не нужен.

## Сервер

SSE открывается прямо из обычного route handler:

```c
static void events(chttpx_request_t* req, chttpx_response_t* res)
{
    chttpx_sse_t* sse = cHTTPX_SSEOpen(req, res);
    if (!sse)
    {
        *res = cHTTPX_ResError(cHTTPX_StatusInternalServerError, "failed to open SSE stream");
        return;
    }

    cHTTPX_SSERetry(sse, 3000);

    for (unsigned int i = 1; i <= 10 && cHTTPX_SSEConnected(sse); i++)
    {
        char id[32];
        char data[128];
        snprintf(id, sizeof(id), "%u", i);
        snprintf(data, sizeof(data), "{\"progress\":%u}", i * 10);

        if (cHTTPX_SSESend(sse, "progress", id, data) != cHTTPX_OK)
            break;
    }

    cHTTPX_SSEClose(sse);
}
```

`cHTTPX_SSEOpen()` сразу начинает streaming response и автоматически задаёт:

- `Content-Type: text/event-stream`
- `Cache-Control: no-cache`
- `X-Accel-Buffering: no`
- текущий `X-Request-ID`, если request ID включён

В HTTP/2 connection-specific headers вроде `Connection: keep-alive` запрещены, поэтому libchttpx их не отправляет.

## События

```c
cHTTPX_SSESend(sse, "message", "42", "{\"status\":\"ready\"}");
```

Аргументы соответствуют полям SSE:

- `event` — необязательный тип события
- `id` — необязательный id события
- `data` — данные; переносы CR/LF автоматически превращаются в несколько строк `data:`

`event` и `id` должны быть однострочными. `data` может быть многострочным.

Задать задержку повторного подключения браузера:

```c
cHTTPX_SSERetry(sse, 5000);
```

## Heartbeat и отключение клиента

Комментарий без создания application event:

```c
cHTTPX_SSEComment(sse, "still alive");
```

Минимальный keep-alive heartbeat:

```c
cHTTPX_SSEHeartbeat(sse);
```

Долгоживущий handler должен проверять `cHTTPX_SSEConnected()` и завершать цикл, когда функция возвращает `false`. Ошибка `cHTTPX_SSESend()` или heartbeat также показывает, что запись в клиент больше невозможна. При graceful shutdown сервера connected-state становится `false`, поэтому handler может корректно выйти и закрыть поток.

## Browser EventSource

```js
const source = new EventSource("http://localhost:8080/events");

source.addEventListener("progress", (event) => {
    console.log(event.lastEventId, event.data);
});

source.onerror = () => {
    console.log("connection interrupted; EventSource will retry");
};
```

В репозитории есть готовая пара:

- `example/sse.c` — SSE-сервер на libchttpx
- `example/sse_client.html` — браузерный EventSource-клиент

Сервер собирается через `make examples` и запускается как `.build/example-sse`. Если HTML открыт не с того же origin, настройте CORS.

## Жизненный цикл

`cHTTPX_SSEClose()` идемпотентен. Если handler вернулся с открытым SSE response, libchttpx также завершит HTTP/2 stream. Сжатие SSE отключается автоматически.

Каждый вызов `cHTTPX_SSESend()`, retry, comment или heartbeat отправляется как streaming HTTP/2 DATA, а не накапливается целиком в памяти до завершения handler.

## API

| Функция | Назначение |
| --- | --- |
| `cHTTPX_SSEOpen(req, res)` | открыть SSE response |
| `cHTTPX_SSESend(sse, event, id, data)` | отправить событие |
| `cHTTPX_SSERetry(sse, milliseconds)` | задать reconnect delay |
| `cHTTPX_SSEComment(sse, comment)` | отправить комментарий |
| `cHTTPX_SSEHeartbeat(sse)` | отправить keep-alive |
| `cHTTPX_SSEConnected(sse)` | проверить доступность потока |
| `cHTTPX_SSEClose(sse)` | завершить поток |
