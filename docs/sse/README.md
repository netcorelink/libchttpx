# Server-Sent Events (SSE)

libchttpx provides first-class Server-Sent Events for one-way real-time updates over HTTP/2. SSE is useful for notifications, progress updates, telemetry, dashboards, event feeds, and token-style streaming when the client does not need a bidirectional WebSocket.

## Server

Open the stream from a normal route handler:

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

`cHTTPX_SSEOpen()` immediately starts a streaming response. It automatically uses:

- `Content-Type: text/event-stream`
- `Cache-Control: no-cache`
- `X-Accel-Buffering: no`
- the current `X-Request-ID`, when request IDs are enabled

HTTP/2 forbids connection-specific headers such as `Connection: keep-alive`, so libchttpx does not emit them.

## Events

```c
cHTTPX_SSESend(sse, "message", "42", "{\"status\":\"ready\"}");
```

The arguments map to the standard SSE fields:

- `event` — optional event type
- `id` — optional event id
- `data` — event payload; embedded CR/LF line breaks are encoded as multiple `data:` lines

The event and id values must be single-line strings. Data may be multiline.

To change the browser reconnect delay:

```c
cHTTPX_SSERetry(sse, 5000);
```

## Heartbeats and disconnects

A comment can carry diagnostic text without dispatching an application event:

```c
cHTTPX_SSEComment(sse, "still alive");
```

For a minimal keep-alive heartbeat:

```c
cHTTPX_SSEHeartbeat(sse);
```

Long-running handlers should check `cHTTPX_SSEConnected()` and stop when it becomes false. A failed event or heartbeat write also reports the disconnect through its return code. During graceful server shutdown the connected state becomes false so the handler can exit and the stream can finish cleanly.

## Browser client

```js
const source = new EventSource("http://localhost:8080/events");

source.addEventListener("progress", (event) => {
    console.log(event.lastEventId, event.data);
});

source.onerror = () => {
    console.log("connection interrupted; EventSource will retry");
};
```

The repository contains a complete pair:

- `example/sse.c` — libchttpx SSE server
- `example/sse_client.html` — browser EventSource client

Build the server with `make examples`, run `.build/example-sse`, then serve/open the HTML client from an origin allowed by your CORS configuration if it is not same-origin.

## Lifecycle

`cHTTPX_SSEClose()` is idempotent. Returning from a handler with an open SSE response also causes libchttpx to finish the HTTP/2 stream. SSE response compression is disabled automatically.

Each `cHTTPX_SSESend()`, retry, comment, or heartbeat is submitted as streaming HTTP/2 DATA rather than accumulating the complete response in memory.

## API

| Function | Purpose |
| --- | --- |
| `cHTTPX_SSEOpen(req, res)` | start an SSE response |
| `cHTTPX_SSESend(sse, event, id, data)` | send an event |
| `cHTTPX_SSERetry(sse, milliseconds)` | set reconnect delay |
| `cHTTPX_SSEComment(sse, comment)` | send a comment |
| `cHTTPX_SSEHeartbeat(sse)` | send a keep-alive comment |
| `cHTTPX_SSEConnected(sse)` | test whether the stream is still writable |
| `cHTTPX_SSEClose(sse)` | finish the stream |
