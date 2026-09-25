/**
 * Copyright (c) 2026 netcorelink
 *
 * Server-Sent Events helpers.
 */

#include "cHTTPX_sse.h"

#include "cHTTPX_headers.h"
#include "cHTTPX_http.h"
#include "cHTTPX_serv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct chttpx_sse
{
    chttpx_request_t* request;
    chttpx_response_t* response;
    bool closed;
};

typedef struct
{
    char* data;
    size_t size;
    size_t capacity;
} chttpx_sse_buffer_t;

static int sse_buffer_reserve(chttpx_sse_buffer_t* buffer, size_t extra)
{
    if (!buffer || extra > SIZE_MAX - buffer->size)
        return cHTTPX_ERR_LIMIT;

    size_t required = buffer->size + extra;
    if (required <= buffer->capacity)
        return cHTTPX_OK;

    size_t capacity = buffer->capacity ? buffer->capacity : 128;
    while (capacity < required)
    {
        if (capacity > SIZE_MAX / 2)
        {
            capacity = required;
            break;
        }
        capacity *= 2;
    }

    char* resized = realloc(buffer->data, capacity);
    if (!resized)
        return cHTTPX_ERR_MEMORY;
    buffer->data = resized;
    buffer->capacity = capacity;
    return cHTTPX_OK;
}

static int sse_buffer_append(chttpx_sse_buffer_t* buffer, const void* data, size_t size)
{
    if (!size)
        return cHTTPX_OK;
    int result = sse_buffer_reserve(buffer, size);
    if (result != cHTTPX_OK)
        return result;
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return cHTTPX_OK;
}

static int sse_buffer_text(chttpx_sse_buffer_t* buffer, const char* text)
{
    return sse_buffer_append(buffer, text, strlen(text));
}

static bool sse_single_line(const char* value)
{
    if (!value)
        return true;
    return strchr(value, '\r') == NULL && strchr(value, '\n') == NULL;
}

static int sse_append_multiline(chttpx_sse_buffer_t* buffer, const char* prefix, const char* value)
{
    const char* cursor = value ? value : "";
    for (;;)
    {
        const char* end = cursor;
        while (*end && *end != '\r' && *end != '\n')
            end++;

        int result = sse_buffer_text(buffer, prefix);
        if (result == cHTTPX_OK)
            result = sse_buffer_append(buffer, cursor, (size_t)(end - cursor));
        if (result == cHTTPX_OK)
            result = sse_buffer_text(buffer, "\n");
        if (result != cHTTPX_OK)
            return result;

        if (!*end)
            break;

        if (*end == '\r' && end[1] == '\n')
            end++;
        cursor = end + 1;
        if (!*cursor)
        {
            result = sse_buffer_text(buffer, prefix);
            if (result == cHTTPX_OK)
                result = sse_buffer_text(buffer, "\n");
            return result;
        }
    }

    return cHTTPX_OK;
}

static int sse_write(chttpx_sse_t* sse, const void* data, size_t size)
{
    if (!sse || sse->closed || !data || !size)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    chttpx_stream_transport_t* transport = &sse->request->_stream_transport;
    if (!transport->write || !transport->connected || !transport->connected(transport->context))
        return cHTTPX_ERR_STATE;
    return transport->write(transport->context, data, size);
}

static const char* sse_allowed_origin(chttpx_serv_t* server, const char* request_origin)
{
    if (!server || !server->cors.enabled || !request_origin)
        return NULL;

    size_t left = 0;
    size_t right = server->cors.origins_count;
    while (left < right)
    {
        size_t middle = left + (right - left) / 2;
        int order = strcmp(server->cors.origins[middle], request_origin);
        if (order == 0)
            return server->cors.origins[middle];
        if (order < 0)
            left = middle + 1;
        else
            right = middle;
    }
    return NULL;
}

static int sse_response_header(chttpx_response_t* response, const char* name, const char* value)
{
    for (size_t i = 0; i < response->headers_count; i++)
    {
        if (strcasecmp(response->headers[i].name, name) == 0)
        {
            snprintf(response->headers[i].value, sizeof(response->headers[i].value), "%s", value);
            return cHTTPX_OK;
        }
    }
    return cHTTPX_HeaderAdd(response, name, value) == 0 ? cHTTPX_OK : cHTTPX_ERR_LIMIT;
}

chttpx_sse_t* cHTTPX_SSEOpen(chttpx_request_t* req, chttpx_response_t* res)
{
    if (!req || !res)
        return NULL;

    chttpx_stream_transport_t* transport = &req->_stream_transport;
    if (!transport->context || !transport->open || !transport->write || !transport->close || !transport->connected)
        return NULL;

    cHTTPX_ResponseCleanup(res);
    res->status = cHTTPX_StatusOK;
    res->content_type = cHTTPX_CTYPE_SSE;
    res->compression_disabled = true;
    res->_streaming_response = true;

    if (sse_response_header(res, "Cache-Control", "no-cache") != cHTTPX_OK ||
        sse_response_header(res, "X-Accel-Buffering", "no") != cHTTPX_OK)
    {
        res->_streaming_response = false;
        return NULL;
    }

    if (req->request_id[0] && sse_response_header(res, "X-Request-ID", req->request_id) != cHTTPX_OK)
    {
        res->_streaming_response = false;
        return NULL;
    }

    chttpx_serv_t* server = req->_server;
    const char* allowed_origin = sse_allowed_origin(server, cHTTPX_HeaderGet(req, "Origin"));
    if (allowed_origin &&
        (sse_response_header(res, "Access-Control-Allow-Origin", allowed_origin) != cHTTPX_OK ||
         sse_response_header(res, "Access-Control-Allow-Methods", server->cors.methods) != cHTTPX_OK ||
         sse_response_header(res, "Access-Control-Allow-Headers", server->cors.headers) != cHTTPX_OK ||
         sse_response_header(res, "Access-Control-Allow-Credentials", "true") != cHTTPX_OK))
    {
        res->_streaming_response = false;
        return NULL;
    }

    chttpx_sse_t* sse = cHTTPX_Alloc(req, sizeof(*sse));
    if (!sse)
    {
        res->_streaming_response = false;
        return NULL;
    }

    sse->request = req;
    sse->response = res;
    if (transport->open(transport->context, res) != cHTTPX_OK)
    {
        res->_streaming_response = false;
        sse->closed = true;
        return NULL;
    }
    return sse;
}

int cHTTPX_SSESend(chttpx_sse_t* sse, const char* event, const char* id, const char* data)
{
    if (!sse || sse->closed || !sse_single_line(event) || !sse_single_line(id))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_sse_buffer_t buffer = {0};
    int result = cHTTPX_OK;

    if (event && *event)
    {
        result = sse_buffer_text(&buffer, "event: ");
        if (result == cHTTPX_OK)
            result = sse_buffer_text(&buffer, event);
        if (result == cHTTPX_OK)
            result = sse_buffer_text(&buffer, "\n");
    }

    if (result == cHTTPX_OK && id)
    {
        result = sse_buffer_text(&buffer, "id: ");
        if (result == cHTTPX_OK)
            result = sse_buffer_text(&buffer, id);
        if (result == cHTTPX_OK)
            result = sse_buffer_text(&buffer, "\n");
    }

    if (result == cHTTPX_OK)
        result = sse_append_multiline(&buffer, "data: ", data ? data : "");
    if (result == cHTTPX_OK)
        result = sse_buffer_text(&buffer, "\n");
    if (result == cHTTPX_OK)
        result = sse_write(sse, buffer.data, buffer.size);

    free(buffer.data);
    return result;
}

int cHTTPX_SSERetry(chttpx_sse_t* sse, uint64_t milliseconds)
{
    if (!sse || sse->closed)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    char buffer[64];
    int written = snprintf(buffer, sizeof(buffer), "retry: %llu\n\n", (unsigned long long)milliseconds);
    if (written <= 0 || (size_t)written >= sizeof(buffer))
        return cHTTPX_ERR_LIMIT;
    return sse_write(sse, buffer, (size_t)written);
}

int cHTTPX_SSEComment(chttpx_sse_t* sse, const char* comment)
{
    if (!sse || sse->closed)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    chttpx_sse_buffer_t buffer = {0};
    int result = sse_append_multiline(&buffer, ": ", comment ? comment : "");
    if (result == cHTTPX_OK)
        result = sse_buffer_text(&buffer, "\n");
    if (result == cHTTPX_OK)
        result = sse_write(sse, buffer.data, buffer.size);
    free(buffer.data);
    return result;
}

int cHTTPX_SSEHeartbeat(chttpx_sse_t* sse)
{
    static const char heartbeat[] = ":\n\n";
    return sse_write(sse, heartbeat, sizeof(heartbeat) - 1);
}

bool cHTTPX_SSEConnected(const chttpx_sse_t* sse)
{
    if (!sse || sse->closed || !sse->request)
        return false;
    const chttpx_stream_transport_t* transport = &sse->request->_stream_transport;
    return transport->connected && transport->connected(transport->context);
}

int cHTTPX_SSEClose(chttpx_sse_t* sse)
{
    if (!sse)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    if (sse->closed)
        return cHTTPX_OK;

    sse->closed = true;
    chttpx_stream_transport_t* transport = &sse->request->_stream_transport;
    return transport->close ? transport->close(transport->context) : cHTTPX_ERR_STATE;
}
