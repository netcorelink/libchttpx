/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software.
 */

#include "cHTTPX_websocket.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct chttpx_wsocket_route_entry
{
    char* path;
    chttpx_wsocket_route_t handler;
    struct chttpx_wsocket_route_entry* next;
} chttpx_wsocket_route_entry_t;

typedef struct
{
    chttpx_wsocket_route_entry_t* routes;
} chttpx_wsocket_server_state_t;

typedef struct
{
    chttpx_serv_t* server;
    chttpx_wsocket_transport_send_fn transport_send;
    void* transport_context;
    chttpx_wsocket_handler_t message_handler;
    chttpx_wsocket_close_handler_t close_handler;
    unsigned char* input;
    size_t input_size;
    size_t input_capacity;
    unsigned char* message;
    size_t message_size;
    size_t message_capacity;
    int fragment_opcode;
    bool close_sent;
    bool close_received;
} chttpx_wsocket_internal_t;

static chttpx_wsocket_internal_t* websocket_internal(chttpx_wsocket_t* wsocket)
{
    return wsocket ? (chttpx_wsocket_internal_t*)wsocket->_internal : NULL;
}

static bool websocket_valid_utf8(const unsigned char* data, size_t len)
{
    size_t i = 0;
    while (i < len)
    {
        unsigned char ch = data[i++];
        if (ch <= 0x7F)
            continue;

        size_t continuation = 0;
        uint32_t codepoint = 0;
        if ((ch & 0xE0) == 0xC0)
        {
            continuation = 1;
            codepoint = ch & 0x1F;
            if (codepoint < 2)
                return false;
        }
        else if ((ch & 0xF0) == 0xE0)
        {
            continuation = 2;
            codepoint = ch & 0x0F;
        }
        else if ((ch & 0xF8) == 0xF0)
        {
            continuation = 3;
            codepoint = ch & 0x07;
        }
        else
            return false;

        if (continuation > len - i)
            return false;

        for (size_t j = 0; j < continuation; j++)
        {
            unsigned char next = data[i++];
            if ((next & 0xC0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (uint32_t)(next & 0x3F);
        }

        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            return false;
    }
    return true;
}

static int websocket_ensure(unsigned char** buffer, size_t* capacity, size_t required, size_t limit)
{
    if (required > limit)
        return cHTTPX_ERR_LIMIT;
    if (required <= *capacity)
        return cHTTPX_OK;

    size_t next = *capacity ? *capacity : 4096;
    while (next < required)
    {
        if (next > limit / 2)
        {
            next = limit;
            break;
        }
        next *= 2;
    }

    unsigned char* resized = realloc(*buffer, next);
    if (!resized)
        return cHTTPX_ERR_MEMORY;
    *buffer = resized;
    *capacity = next;
    return cHTTPX_OK;
}

static int websocket_send_frame(chttpx_wsocket_t* wsocket, int opcode, const unsigned char* data, size_t len, bool end_stream)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (!internal || !internal->transport_send || (!data && len))
        return cHTTPX_ERR_INVALID_ARGUMENT;
    if (!wsocket->connected && opcode != CHTTPX_WSOCKET_OPCODE_CLOSE)
        return cHTTPX_ERR_STATE;
    if ((opcode & 0x08) && len > 125)
        return cHTTPX_ERR_LIMIT;

    size_t header_size = len <= 125 ? 2 : (len <= 65535 ? 4 : 10);
    if (len > SIZE_MAX - header_size)
        return cHTTPX_ERR_LIMIT;

    size_t frame_size = header_size + len;
    unsigned char* frame = malloc(frame_size ? frame_size : 1);
    if (!frame)
        return cHTTPX_ERR_MEMORY;

    frame[0] = (unsigned char)(0x80 | (opcode & 0x0F));
    size_t offset = 2;
    if (len <= 125)
        frame[1] = (unsigned char)len;
    else if (len <= 65535)
    {
        frame[1] = 126;
        frame[2] = (unsigned char)((len >> 8) & 0xFF);
        frame[3] = (unsigned char)(len & 0xFF);
        offset = 4;
    }
    else
    {
        frame[1] = 127;
        uint64_t value = (uint64_t)len;
        for (int i = 0; i < 8; i++)
            frame[2 + i] = (unsigned char)(value >> (56 - i * 8));
        offset = 10;
    }

    if (len)
        memcpy(frame + offset, data, len);

    int result = internal->transport_send(internal->transport_context, frame, frame_size, end_stream);
    free(frame);
    return result;
}

static bool websocket_close_code_valid(uint16_t code)
{
    if (code < 1000 || code >= 5000)
        return false;
    if (code == 1004 || code == 1005 || code == 1006 || code == 1015)
        return false;
    if (code >= 1016 && code <= 2999)
        return false;
    return true;
}

static int websocket_send_close_payload(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (!internal)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    if (internal->close_sent)
        return cHTTPX_OK;

    int result = websocket_send_frame(wsocket, CHTTPX_WSOCKET_OPCODE_CLOSE, data, len, true);
    if (result == cHTTPX_OK)
    {
        internal->close_sent = true;
        wsocket->connected = 0;
    }
    return result;
}

static int websocket_protocol_close(chttpx_wsocket_t* wsocket, uint16_t code)
{
    unsigned char payload[2] = {(unsigned char)(code >> 8), (unsigned char)(code & 0xFF)};
    (void)websocket_send_close_payload(wsocket, payload, sizeof(payload));
    return cHTTPX_ERR_PROTOCOL;
}

static int websocket_append_message(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (!internal)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    size_t limit = internal->server && internal->server->max_body_size ? internal->server->max_body_size : MAX_BUFFER_BODY;
    if (len > limit - internal->message_size)
        return cHTTPX_ERR_LIMIT;

    size_t required = internal->message_size + len;
    int result = websocket_ensure(&internal->message, &internal->message_capacity, required ? required : 1, limit ? limit : 1);
    if (result != cHTTPX_OK)
        return result;
    if (len)
        memcpy(internal->message + internal->message_size, data, len);
    internal->message_size += len;
    return cHTTPX_OK;
}

static void websocket_emit_message(chttpx_wsocket_t* wsocket, int opcode, const unsigned char* data, size_t len)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (!internal || !internal->message_handler)
        return;
    wsocket->opcode = opcode;
    internal->message_handler(wsocket, data, len);
}

static int websocket_process_frame(chttpx_wsocket_t* wsocket, bool fin, int opcode, unsigned char* payload, size_t payload_len)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (!internal)
        return cHTTPX_ERR_INVALID_ARGUMENT;

    if (opcode == CHTTPX_WSOCKET_OPCODE_CLOSE)
    {
        if (!fin || payload_len == 1)
            return websocket_protocol_close(wsocket, 1002);

        uint16_t code = 1005;
        const unsigned char* reason = NULL;
        size_t reason_len = 0;
        if (payload_len >= 2)
        {
            code = (uint16_t)(((uint16_t)payload[0] << 8) | payload[1]);
            reason = payload + 2;
            reason_len = payload_len - 2;
            if (!websocket_close_code_valid(code))
                return websocket_protocol_close(wsocket, 1002);
            if (!websocket_valid_utf8(reason, reason_len))
                return websocket_protocol_close(wsocket, 1007);
        }

        internal->close_received = true;
        wsocket->connected = 0;
        if (internal->close_handler)
            internal->close_handler(wsocket, code, reason, reason_len);
        if (!internal->close_sent)
            (void)websocket_send_close_payload(wsocket, payload, payload_len);
        return cHTTPX_OK;
    }

    if (opcode == CHTTPX_WSOCKET_OPCODE_PING)
    {
        if (!fin || payload_len > 125)
            return websocket_protocol_close(wsocket, 1002);
        return websocket_send_frame(wsocket, CHTTPX_WSOCKET_OPCODE_PONG, payload, payload_len, false);
    }

    if (opcode == CHTTPX_WSOCKET_OPCODE_PONG)
        return fin && payload_len <= 125 ? cHTTPX_OK : websocket_protocol_close(wsocket, 1002);

    if (opcode == CHTTPX_WSOCKET_OPCODE_CONTINUATION)
    {
        if (!internal->fragment_opcode)
            return websocket_protocol_close(wsocket, 1002);
        int result = websocket_append_message(wsocket, payload, payload_len);
        if (result != cHTTPX_OK)
            return websocket_protocol_close(wsocket, result == cHTTPX_ERR_LIMIT ? 1009 : 1011);

        if (fin)
        {
            int message_opcode = internal->fragment_opcode;
            if (message_opcode == CHTTPX_WSOCKET_OPCODE_TEXT && !websocket_valid_utf8(internal->message, internal->message_size))
                return websocket_protocol_close(wsocket, 1007);
            internal->fragment_opcode = 0;
            websocket_emit_message(wsocket, message_opcode, internal->message, internal->message_size);
            internal->message_size = 0;
        }
        return cHTTPX_OK;
    }

    if (opcode != CHTTPX_WSOCKET_OPCODE_TEXT && opcode != CHTTPX_WSOCKET_OPCODE_BINARY)
        return websocket_protocol_close(wsocket, 1002);
    if (internal->fragment_opcode)
        return websocket_protocol_close(wsocket, 1002);

    if (fin)
    {
        if (opcode == CHTTPX_WSOCKET_OPCODE_TEXT && !websocket_valid_utf8(payload, payload_len))
            return websocket_protocol_close(wsocket, 1007);
        websocket_emit_message(wsocket, opcode, payload, payload_len);
        return cHTTPX_OK;
    }

    internal->fragment_opcode = opcode;
    internal->message_size = 0;
    int result = websocket_append_message(wsocket, payload, payload_len);
    if (result != cHTTPX_OK)
        return websocket_protocol_close(wsocket, result == cHTTPX_ERR_LIMIT ? 1009 : 1011);
    return cHTTPX_OK;
}

void cHTTPX_WSocketRegisterRoute(chttpx_router_t* router, const char* path, chttpx_wsocket_route_t handler)
{
    if (!router || !router->serv || !router->serv->initialized || !path || !handler)
        return;

    char full_path[CHTTPX_MAX_PATH];
    int written = snprintf(full_path, sizeof(full_path), "%s%s", router->prefix, path);
    if (written < 0 || (size_t)written >= sizeof(full_path))
        return;

    chttpx_wsocket_server_state_t* state = router->serv->websocket_state;
    if (!state)
    {
        state = calloc(1, sizeof(*state));
        if (!state)
            return;
        router->serv->websocket_state = state;
    }

    for (chttpx_wsocket_route_entry_t* current = state->routes; current; current = current->next)
    {
        if (strcmp(current->path, full_path) == 0)
        {
            current->handler = handler;
            return;
        }
    }

    chttpx_wsocket_route_entry_t* entry = calloc(1, sizeof(*entry));
    if (!entry)
        return;
    entry->path = strdup(full_path);
    if (!entry->path)
    {
        free(entry);
        return;
    }
    entry->handler = handler;
    entry->next = state->routes;
    state->routes = entry;
}

chttpx_wsocket_route_t _chttpx_websocket_find_route(chttpx_serv_t* server, const char* path)
{
    chttpx_wsocket_server_state_t* state = server ? server->websocket_state : NULL;
    if (!state || !path)
        return NULL;

    size_t path_len = strcspn(path, "?");
    for (chttpx_wsocket_route_entry_t* current = state->routes; current; current = current->next)
    {
        size_t registered_len = strlen(current->path);
        if (registered_len == path_len && memcmp(current->path, path, path_len) == 0)
            return current->handler;
    }
    return NULL;
}

void cHTTPX_WSocketOnMessage(chttpx_wsocket_t* wsocket, chttpx_wsocket_handler_t handler)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (internal)
        internal->message_handler = handler;
}

void cHTTPX_WSocketOnClose(chttpx_wsocket_t* wsocket, chttpx_wsocket_close_handler_t handler)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (internal)
        internal->close_handler = handler;
}

void cHTTPX_WSocketSetData(chttpx_wsocket_t* wsocket, void* user_data)
{
    if (wsocket)
        wsocket->user_data = user_data;
}

void* cHTTPX_WSocketData(chttpx_wsocket_t* wsocket)
{
    return wsocket ? wsocket->user_data : NULL;
}

int cHTTPX_WSocketSend(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len)
{
    return websocket_send_frame(wsocket, CHTTPX_WSOCKET_OPCODE_TEXT, data, len, false);
}

int cHTTPX_WSocketSendBinary(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len)
{
    return websocket_send_frame(wsocket, CHTTPX_WSOCKET_OPCODE_BINARY, data, len, false);
}

int cHTTPX_WSocketPing(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len)
{
    return websocket_send_frame(wsocket, CHTTPX_WSOCKET_OPCODE_PING, data, len, false);
}

int cHTTPX_WSocketClose(chttpx_wsocket_t* wsocket, uint16_t code, const char* reason)
{
    if (!wsocket)
        return cHTTPX_ERR_INVALID_ARGUMENT;
    if (!code)
        code = 1000;
    if (!websocket_close_code_valid(code))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    size_t reason_len = reason ? strlen(reason) : 0;
    if (reason_len > 123 || (reason_len && !websocket_valid_utf8((const unsigned char*)reason, reason_len)))
        return cHTTPX_ERR_INVALID_ARGUMENT;

    unsigned char payload[125];
    payload[0] = (unsigned char)(code >> 8);
    payload[1] = (unsigned char)(code & 0xFF);
    if (reason_len)
        memcpy(payload + 2, reason, reason_len);
    return websocket_send_close_payload(wsocket, payload, reason_len + 2);
}

int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key)
{
    (void)client_socket;
    (void)sec_wsocket_key;
    return cHTTPX_ERR_UNAVAILABLE;
}

int cHTTPX_WSocketRecv(chttpx_wsocket_t* wsocket, unsigned char* buffer, size_t len)
{
    (void)wsocket;
    (void)buffer;
    (void)len;
    return cHTTPX_ERR_UNAVAILABLE;
}

chttpx_wsocket_t* _chttpx_websocket_create(chttpx_serv_t* server, chttpx_socket_t socket, chttpx_request_t* request,
                                           chttpx_wsocket_transport_send_fn transport_send, void* transport_context)
{
    if (!server || !request || !transport_send)
        return NULL;

    chttpx_wsocket_t* wsocket = calloc(1, sizeof(*wsocket));
    chttpx_wsocket_internal_t* internal = calloc(1, sizeof(*internal));
    if (!wsocket || !internal)
    {
        free(wsocket);
        free(internal);
        return NULL;
    }

    internal->server = server;
    internal->transport_send = transport_send;
    internal->transport_context = transport_context;
    wsocket->socket = socket;
    wsocket->request = request;
    wsocket->_internal = internal;
    return wsocket;
}

void _chttpx_websocket_mark_connected(chttpx_wsocket_t* wsocket)
{
    if (wsocket)
        wsocket->connected = 1;
}

int _chttpx_websocket_feed(chttpx_wsocket_t* wsocket, const unsigned char* data, size_t len)
{
    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (!internal || (!data && len))
        return cHTTPX_ERR_INVALID_ARGUMENT;
    if (!wsocket->connected)
        return cHTTPX_ERR_STATE;

    size_t limit = internal->server && internal->server->max_body_size ? internal->server->max_body_size : MAX_BUFFER_BODY;
    if (limit > SIZE_MAX - 14)
        limit = SIZE_MAX - 14;
    size_t input_limit = limit + 14;

    if (len > input_limit - internal->input_size)
        return websocket_protocol_close(wsocket, 1009);

    size_t required = internal->input_size + len;
    int result = websocket_ensure(&internal->input, &internal->input_capacity, required ? required : 1, input_limit ? input_limit : 1);
    if (result != cHTTPX_OK)
        return websocket_protocol_close(wsocket, result == cHTTPX_ERR_LIMIT ? 1009 : 1011);

    if (len)
        memcpy(internal->input + internal->input_size, data, len);
    internal->input_size += len;

    while (internal->input_size >= 2 && wsocket->connected)
    {
        unsigned char* frame = internal->input;
        bool fin = (frame[0] & 0x80) != 0;
        unsigned int rsv = frame[0] & 0x70;
        int opcode = frame[0] & 0x0F;
        bool masked = (frame[1] & 0x80) != 0;
        uint64_t payload_len = frame[1] & 0x7F;
        size_t header_size = 2;

        if (rsv)
            return websocket_protocol_close(wsocket, 1002);

        if (payload_len == 126)
        {
            if (internal->input_size < 4)
                break;
            payload_len = ((uint64_t)frame[2] << 8) | frame[3];
            if (payload_len < 126)
                return websocket_protocol_close(wsocket, 1002);
            header_size = 4;
        }
        else if (payload_len == 127)
        {
            if (internal->input_size < 10)
                break;
            if (frame[2] & 0x80)
                return websocket_protocol_close(wsocket, 1002);
            payload_len = 0;
            for (int i = 0; i < 8; i++)
                payload_len = (payload_len << 8) | frame[2 + i];
            if (payload_len <= 65535)
                return websocket_protocol_close(wsocket, 1002);
            header_size = 10;
        }

        if (!masked)
            return websocket_protocol_close(wsocket, 1002);
        if ((opcode & 0x08) && (!fin || payload_len > 125))
            return websocket_protocol_close(wsocket, 1002);
        if (payload_len > limit || payload_len > SIZE_MAX)
            return websocket_protocol_close(wsocket, 1009);

        if (header_size > SIZE_MAX - 4 || (size_t)payload_len > SIZE_MAX - header_size - 4)
            return websocket_protocol_close(wsocket, 1009);
        size_t frame_size = header_size + 4 + (size_t)payload_len;
        if (internal->input_size < frame_size)
            break;

        unsigned char mask[4];
        memcpy(mask, frame + header_size, sizeof(mask));
        unsigned char* payload = frame + header_size + 4;
        for (size_t i = 0; i < (size_t)payload_len; i++)
            payload[i] ^= mask[i & 3];

        result = websocket_process_frame(wsocket, fin, opcode, payload, (size_t)payload_len);
        if (result != cHTTPX_OK && result != cHTTPX_ERR_PROTOCOL)
            return result;

        size_t remaining = internal->input_size - frame_size;
        if (remaining)
            memmove(internal->input, internal->input + frame_size, remaining);
        internal->input_size = remaining;

        if (result == cHTTPX_ERR_PROTOCOL)
            return result;
    }

    return cHTTPX_OK;
}

void _chttpx_websocket_destroy(chttpx_wsocket_t* wsocket)
{
    if (!wsocket)
        return;

    chttpx_wsocket_internal_t* internal = websocket_internal(wsocket);
    if (internal)
    {
        bool was_connected = wsocket->connected != 0;
        wsocket->connected = 0;
        if (was_connected && !internal->close_received && internal->close_handler)
            internal->close_handler(wsocket, 1006, NULL, 0);
        free(internal->input);
        free(internal->message);
        free(internal);
    }

    wsocket->_internal = NULL;
    wsocket->connected = 0;
    free(wsocket);
}

void _chttpx_websocket_server_cleanup(chttpx_serv_t* server)
{
    chttpx_wsocket_server_state_t* state = server ? server->websocket_state : NULL;
    if (!state)
        return;

    chttpx_wsocket_route_entry_t* current = state->routes;
    while (current)
    {
        chttpx_wsocket_route_entry_t* next = current->next;
        free(current->path);
        free(current);
        current = next;
    }

    free(state);
    server->websocket_state = NULL;
}
