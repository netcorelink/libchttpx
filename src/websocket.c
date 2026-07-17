/*
 * Copyright (c) 2026 netcorelink
 *
 * WebSocket engine: one poll thread handles all connections (scalable for chat).
 */

#include "websocket.h"

#include "headers.h"
#include "params.h"
#include "utils.h"

#ifndef CHTTPX_PLATFORM_WINDOWS
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <sys/select.h>
#else
#include <winsock2.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHTTPX_WSOCKET_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define CHTTPX_WSOCKET_MAX_PAYLOAD (64 * 1024)
#define CHTTPX_WSOCKET_READ_BUF (CHTTPX_WSOCKET_MAX_PAYLOAD + 16)

typedef struct
{
    chttpx_wsocket_t public_ws;
    chttpx_wsocket_on_open_t on_open;
    chttpx_wsocket_on_message_t on_message;
    chttpx_wsocket_on_close_t on_close;
    void* route_userdata;

    unsigned char read_buf[CHTTPX_WSOCKET_READ_BUF];
    size_t read_len;
    size_t frame_offset;
} ws_connection_t;

typedef struct
{
    ws_connection_t* items;
    size_t count;
    size_t capacity;
    thread_t poll_thread;
    int engine_running;
    int shutdown_requested;
#if defined(_WIN32) || defined(_WIN64)
    CRITICAL_SECTION lock;
#else
    pthread_mutex_t lock;
#endif
} ws_engine_t;

static ws_engine_t ws_engine = {0};

static void ws_lock(void)
{
#if defined(_WIN32) || defined(_WIN64)
    EnterCriticalSection(&ws_engine.lock);
#else
    pthread_mutex_lock(&ws_engine.lock);
#endif
}

static void ws_unlock(void)
{
#if defined(_WIN32) || defined(_WIN64)
    LeaveCriticalSection(&ws_engine.lock);
#else
    pthread_mutex_unlock(&ws_engine.lock);
#endif
}

/* --- SHA-1 + Base64 (handshake) --- */

typedef struct
{
    uint32_t state[5];
    uint32_t count[2];
    unsigned char buffer[64];
} sha1_ctx_t;

static void sha1_transform(uint32_t state[5], const unsigned char buffer[64])
{
    uint32_t w[80];
    uint32_t a, b, c, d, e, temp;
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)buffer[i * 4] << 24) | ((uint32_t)buffer[i * 4 + 1] << 16) |
               ((uint32_t)buffer[i * 4 + 2] << 8) | (uint32_t)buffer[i * 4 + 3];
    for (i = 16; i < 80; i++)
        w[i] = ((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]) << 1) |
               ((w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]) >> 31);

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    for (i = 0; i < 80; i++)
    {
        if (i < 20)
            temp = ((b & c) | ((~b) & d)) + 0x5A827999;
        else if (i < 40)
            temp = (b ^ c ^ d) + 0x6ED9EBA1;
        else if (i < 60)
            temp = ((b & c) | (b & d) | (c & d)) + 0x8F1BBCDC;
        else
            temp = (b ^ c ^ d) + 0xCA62C1D6;
        temp += ((a << 5) | (a >> 27)) + e + w[i];
        e = d;
        d = c;
        c = ((b << 30) | (b >> 2));
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1_init(sha1_ctx_t* ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

static void sha1_update(sha1_ctx_t* ctx, const unsigned char* data, size_t len)
{
    size_t i = 0;
    size_t j = (ctx->count[0] >> 3) & 63;

    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < (uint32_t)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    if (j + len > 63)
    {
        memcpy(&ctx->buffer[j], data, 64 - j);
        sha1_transform(ctx->state, ctx->buffer);
        i = 64 - j;
        while (i + 63 < len)
        {
            sha1_transform(ctx->state, &data[i]);
            i += 64;
        }
        j = 0;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void sha1_final(sha1_ctx_t* ctx, unsigned char digest[20])
{
    unsigned char finalcount[8];
    int i;

    for (i = 0; i < 8; i++)
        finalcount[i] = (unsigned char)((ctx->count[(i >= 4) ? 1 : 0] >> ((3 - (i & 3)) * 8)) & 255);

    sha1_update(ctx, (unsigned char*)"\200", 1);
    while ((ctx->count[0] & 504) != 448)
        sha1_update(ctx, (unsigned char*)"\0", 1);
    sha1_update(ctx, finalcount, 8);

    for (i = 0; i < 20; i++)
        digest[i] = (unsigned char)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
}

static void base64_encode(const unsigned char* input, size_t input_len, char* output)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, j = 0;

    while (i < input_len)
    {
        uint32_t a = i < input_len ? input[i++] : 0;
        uint32_t b = i < input_len ? input[i++] : 0;
        uint32_t c = i < input_len ? input[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        output[j++] = table[(triple >> 18) & 0x3F];
        output[j++] = table[(triple >> 12) & 0x3F];
        output[j++] = table[(triple >> 6) & 0x3F];
        output[j++] = table[triple & 0x3F];
    }
    output[j] = '\0';
    if (input_len % 3 == 1)
    {
        output[j - 2] = '=';
        output[j - 1] = '=';
    }
    else if (input_len % 3 == 2)
        output[j - 1] = '=';
}

static void ws_compute_accept_key(const char* sec_key, char* accept_out, size_t accept_size)
{
    char combined[256];
    unsigned char hash[20];
    sha1_ctx_t ctx;

    snprintf(combined, sizeof(combined), "%s%s", sec_key, CHTTPX_WSOCKET_GUID);
    sha1_init(&ctx);
    sha1_update(&ctx, (unsigned char*)combined, strlen(combined));
    sha1_final(&ctx, hash);
    if (accept_size >= 29)
        base64_encode(hash, 20, accept_out);
}

static int ws_set_nonblocking(chttpx_socket_t fd)
{
#ifdef CHTTPX_PLATFORM_WINDOWS
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

static ssize_t ws_send_all(chttpx_socket_t fd, const unsigned char* buf, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t n = send(fd, (const char*)buf + total, (int)(len - total), 0);
        if (n <= 0)
            return n;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static int ws_send_frame(chttpx_socket_t fd, int opcode, const unsigned char* data, size_t len, int fin)
{
    unsigned char header[10];
    size_t header_len = 2;

    header[0] = (unsigned char)((fin ? 0x80 : 0) | (opcode & 0x0F));
    if (len <= 125)
        header[1] = (unsigned char)len;
    else if (len <= 65535)
    {
        header[1] = 126;
        header[2] = (unsigned char)((len >> 8) & 0xFF);
        header[3] = (unsigned char)(len & 0xFF);
        header_len = 4;
    }
    else
        return -1;

    if (ws_send_all(fd, header, header_len) < 0)
        return -1;
    if (len > 0 && data && ws_send_all(fd, data, len) < 0)
        return -1;
    return (int)len;
}

int cHTTPX_WSocketSend(chttpx_wsocket_t* ws, const char* text)
{
    if (!ws || !ws->connected || !text)
        return -1;
    return ws_send_frame(ws->socket, CHTTPX_WSOCKET_OPCODE_TEXT, (const unsigned char*)text, strlen(text), 1);
}

int cHTTPX_WSocketSendBinary(chttpx_wsocket_t* ws, const unsigned char* data, size_t len)
{
    if (!ws || !ws->connected)
        return -1;
    return ws_send_frame(ws->socket, CHTTPX_WSOCKET_OPCODE_BINARY, data, len, 1);
}

static int ws_do_upgrade(chttpx_socket_t client_socket, const char* sec_websocket_key)
{
    char accept_key[64] = {0};
    char buffer[512];

    ws_compute_accept_key(sec_websocket_key, accept_key, sizeof(accept_key));
    int n = snprintf(buffer, sizeof(buffer),
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n",
                     accept_key);
    if (n <= 0 || ws_send_all(client_socket, (unsigned char*)buffer, (size_t)n) < 0)
        return -1;
    return 0;
}

/* --- Routes --- */

static void ws_register_route(const char* path, const chttpx_wsocket_callbacks_t* callbacks)
{
    if (!serv || !path || !callbacks)
        return;

    if (serv->ws_routes_count == serv->ws_routes_capacity)
    {
        size_t new_cap = serv->ws_routes_capacity == 0 ? 4 : serv->ws_routes_capacity * 2;
        chttpx_wsocket_route_entry_t* routes =
            realloc(serv->ws_routes, sizeof(chttpx_wsocket_route_entry_t) * new_cap);
        if (!routes)
            return;
        serv->ws_routes = routes;
        serv->ws_routes_capacity = new_cap;
    }

    serv->ws_routes[serv->ws_routes_count].path = strdup(path);
    serv->ws_routes[serv->ws_routes_count].on_open = callbacks->on_open;
    serv->ws_routes[serv->ws_routes_count].on_message = callbacks->on_message;
    serv->ws_routes[serv->ws_routes_count].on_close = callbacks->on_close;
    serv->ws_routes[serv->ws_routes_count].userdata = callbacks->userdata;
    serv->ws_routes_count++;
}

void cHTTPX_WSocketRegisterRoute(chttpx_router_t* r, const char* path, const chttpx_wsocket_callbacks_t* callbacks)
{
    if (!r || !path || !callbacks)
        return;

    char fpath[MAX_PATH];
    if (snprintf(fpath, sizeof(fpath), "%s%s", r->prefix ? r->prefix : "", path) >= (int)sizeof(fpath))
        return;

    ws_register_route(fpath, callbacks);
}

static chttpx_wsocket_route_entry_t* find_ws_route(chttpx_request_t* req)
{
    if (!serv || !req || !req->path)
        return NULL;

    for (size_t i = 0; i < serv->ws_routes_count; i++)
    {
        int count = 0;
        if (cHTTPX_MatchPath(serv->ws_routes[i].path, req->path, req->params, &count))
        {
            req->params_count = count;
            return &serv->ws_routes[i];
        }
    }
    return NULL;
}

static int is_websocket_upgrade(chttpx_request_t* req)
{
    if (!req || !req->method || strcmp(req->method, "GET") != 0)
        return 0;

    const char* upgrade = cHTTPX_HeaderGet(req, "Upgrade");
    const char* connection = cHTTPX_HeaderGet(req, "Connection");
    const char* ws_key = cHTTPX_HeaderGet(req, "Sec-WebSocket-Key");
    const char* ws_version = cHTTPX_HeaderGet(req, "Sec-WebSocket-Version");

    if (!upgrade || !connection || !ws_key || !ws_version)
        return 0;
    if (strcasecmp(upgrade, "websocket") != 0)
        return 0;
    if (!strstr(connection, "pgrade") && !strstr(connection, "pGRADE"))
        return 0;
    if (strcmp(ws_version, "13") != 0)
        return 0;
    return 1;
}

/* --- Connection pool --- */

static void ws_connection_close(ws_connection_t* conn)
{
    if (!conn->public_ws.connected)
        return;

    ws_send_frame(conn->public_ws.socket, CHTTPX_WSOCKET_OPCODE_CLOSE, NULL, 0, 1);
    conn->public_ws.connected = 0;

    if (conn->on_close)
        conn->on_close(&conn->public_ws, conn->route_userdata);

    chttpx_close(conn->public_ws.socket);
    conn->public_ws.socket = (chttpx_socket_t)-1;
}

static void ws_remove_connection(size_t index)
{
    ws_connection_close(&ws_engine.items[index]);
    ws_engine.items[index] = ws_engine.items[ws_engine.count - 1];
    ws_engine.count--;
}

static int ws_add_connection(chttpx_socket_t fd, chttpx_wsocket_route_entry_t* route, chttpx_request_t* req)
{
    if (ws_engine.count == ws_engine.capacity)
    {
        size_t new_cap = ws_engine.capacity == 0 ? 64 : ws_engine.capacity * 2;
        ws_connection_t* items = realloc(ws_engine.items, sizeof(ws_connection_t) * new_cap);
        if (!items)
            return -1;
        ws_engine.items = items;
        ws_engine.capacity = new_cap;
    }

    ws_connection_t* conn = &ws_engine.items[ws_engine.count];
    memset(conn, 0, sizeof(*conn));
    conn->public_ws.socket = fd;
    conn->public_ws.connected = 1;
    if (req && req->path)
        strncpy(conn->public_ws.path, req->path, sizeof(conn->public_ws.path) - 1);
    if (req && req->params_count > 0)
    {
        conn->public_ws.params_count = req->params_count;
        memcpy(conn->public_ws.params, req->params, sizeof(chttpx_param_t) * req->params_count);
    }
    conn->public_ws.userdata = route->userdata;
    conn->on_open = route->on_open;
    conn->on_message = route->on_message;
    conn->on_close = route->on_close;
    conn->route_userdata = route->userdata;

    ws_set_nonblocking(fd);
    ws_engine.count++;

    if (conn->on_open)
        conn->on_open(&conn->public_ws, conn->route_userdata);

    return 0;
}

const char* cHTTPX_WSocketParam(chttpx_wsocket_t* ws, const char* name)
{
    if (!ws || !name || ws->params_count == 0)
        return NULL;

    for (size_t i = 0; i < ws->params_count; i++)
    {
        if (strcmp(ws->params[i].name, name) == 0)
            return ws->params[i].value;
    }
    return NULL;
}

static int ws_broadcast_match(int (*match)(ws_connection_t* conn, const void* ctx), const void* ctx, const char* text)
{
    int sent = 0;
    ws_lock();
    for (size_t i = 0; i < ws_engine.count; i++)
    {
        ws_connection_t* conn = &ws_engine.items[i];
        if (!conn->public_ws.connected)
            continue;
        if (!match(conn, ctx))
            continue;
        if (cHTTPX_WSocketSend(&conn->public_ws, text) >= 0)
            sent++;
    }
    ws_unlock();
    return sent;
}

static int ws_match_path(ws_connection_t* conn, const void* ctx)
{
    const char* path = (const char*)ctx;
    return path && strcmp(conn->public_ws.path, path) == 0;
}

static int ws_match_peers(ws_connection_t* conn, const void* ctx)
{
    const chttpx_wsocket_t* ws = (const chttpx_wsocket_t*)ctx;
    return ws && strcmp(conn->public_ws.path, ws->path) == 0;
}

typedef struct
{
    const char* param_name;
    const char* param_value;
} ws_room_match_t;

static int ws_match_room(ws_connection_t* conn, const void* ctx)
{
    const ws_room_match_t* room = (const ws_room_match_t*)ctx;
    if (!room || !room->param_name || !room->param_value)
        return 0;

    for (size_t i = 0; i < conn->public_ws.params_count; i++)
    {
        if (strcmp(conn->public_ws.params[i].name, room->param_name) == 0 &&
            strcmp(conn->public_ws.params[i].value, room->param_value) == 0)
            return 1;
    }
    return 0;
}

int cHTTPX_WSocketBroadcast(const char* path, const char* text)
{
    if (!path || !text)
        return -1;
    return ws_broadcast_match(ws_match_path, path, text);
}

int cHTTPX_WSocketBroadcastPeers(chttpx_wsocket_t* ws, const char* text)
{
    if (!ws || !text)
        return -1;
    return ws_broadcast_match(ws_match_peers, ws, text);
}

int cHTTPX_WSocketBroadcastRoom(const char* param_name, const char* param_value, const char* text)
{
    if (!param_name || !param_value || !text)
        return -1;

    ws_room_match_t room = {param_name, param_value};
    return ws_broadcast_match(ws_match_room, &room, text);
}

/* --- Frame parsing (incremental, non-blocking) --- */

static int ws_try_process_one_frame(ws_connection_t* conn)
{
    size_t off = conn->frame_offset;
    size_t avail = conn->read_len - off;

    if (avail < 2)
        return 0;

    unsigned char* b = conn->read_buf + off;
    int opcode = b[0] & 0x0F;
    int masked = (b[1] & 0x80) != 0;
    uint64_t plen = b[1] & 0x7F;
    size_t hsize = 2;

    if (plen == 126)
    {
        if (avail < 4)
            return 0;
        plen = ((uint64_t)b[2] << 8) | b[3];
        hsize = 4;
    }
    else if (plen == 127)
        return -1;

    if (plen > CHTTPX_WSOCKET_MAX_PAYLOAD)
        return -1;

    size_t total = hsize + (masked ? 4 : 0) + (size_t)plen;
    if (avail < total)
        return 0;

    unsigned char* payload = b + hsize + (masked ? 4 : 0);
    unsigned char mask[4];

    if (masked)
    {
        memcpy(mask, b + hsize, 4);
        for (uint64_t i = 0; i < plen; i++)
            payload[i] ^= mask[i % 4];
    }

    if (opcode == CHTTPX_WSOCKET_OPCODE_CLOSE)
    {
        conn->public_ws.connected = 0;
    }
    else if (opcode == CHTTPX_WSOCKET_OPCODE_PING)
    {
        ws_send_frame(conn->public_ws.socket, CHTTPX_WSOCKET_OPCODE_PONG, payload, (size_t)plen, 1);
    }
    else if (opcode == CHTTPX_WSOCKET_OPCODE_TEXT || opcode == CHTTPX_WSOCKET_OPCODE_BINARY)
    {
        if (conn->on_message)
            conn->on_message(&conn->public_ws, payload, (size_t)plen, opcode, conn->route_userdata);
    }

    conn->frame_offset += total;
    if (conn->frame_offset >= conn->read_len)
    {
        conn->read_len = 0;
        conn->frame_offset = 0;
    }
    else if (conn->frame_offset > 0)
    {
        memmove(conn->read_buf, conn->read_buf + conn->frame_offset, conn->read_len - conn->frame_offset);
        conn->read_len -= conn->frame_offset;
        conn->frame_offset = 0;
    }

    return 1;
}

static void ws_read_and_parse(ws_connection_t* conn)
{
    while (conn->public_ws.connected)
    {
        if (conn->read_len >= CHTTPX_WSOCKET_READ_BUF)
        {
            conn->public_ws.connected = 0;
            return;
        }

        ssize_t n = recv(conn->public_ws.socket, (char*)conn->read_buf + conn->read_len,
                         (int)(CHTTPX_WSOCKET_READ_BUF - conn->read_len), 0);
        if (n < 0)
        {
#ifdef CHTTPX_PLATFORM_WINDOWS
            if (WSAGetLastError() == WSAEWOULDBLOCK)
                return;
#else
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                return;
#endif
            conn->public_ws.connected = 0;
            return;
        }
        if (n == 0)
        {
            conn->public_ws.connected = 0;
            return;
        }

        conn->read_len += (size_t)n;

        int processed;
        do
        {
            processed = ws_try_process_one_frame(conn);
            if (processed < 0)
            {
                conn->public_ws.connected = 0;
                return;
            }
        } while (processed == 1 && conn->public_ws.connected);
        return;
    }
}

/* --- Poll thread (select: works on Linux and Windows/MinGW) --- */

static void ws_poll_process_readable(ws_connection_t* conn)
{
    ws_read_and_parse(conn);
}

static void* ws_poll_loop(void* arg)
{
    (void)arg;

    while (ws_engine.engine_running && !ws_engine.shutdown_requested)
    {
        ws_lock();
        size_t n = ws_engine.count;

        chttpx_socket_t* fds = NULL;
        if (n > 0)
        {
            fds = malloc(sizeof(chttpx_socket_t) * n);
            if (fds)
            {
                for (size_t i = 0; i < n; i++)
                    fds[i] = ws_engine.items[i].public_ws.socket;
            }
            else
                n = 0;
        }
        ws_unlock();

        if (n == 0)
        {
            free(fds);
#if defined(_WIN32) || defined(_WIN64)
            Sleep(10);
#else
            usleep(10000);
#endif
            continue;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        chttpx_socket_t maxfd = 0;
        for (size_t i = 0; i < n; i++)
        {
            FD_SET(fds[i], &readfds);
            if (fds[i] > maxfd)
                maxfd = fds[i];
        }

        struct timeval tv = {.tv_sec = 0, .tv_usec = 50000};
        int ready = select((int)(maxfd + 1), &readfds, NULL, NULL, &tv);

        if (ready <= 0)
        {
            free(fds);
            continue;
        }

        ws_lock();
        for (size_t i = 0; i < ws_engine.count;)
        {
            ws_connection_t* conn = &ws_engine.items[i];
            if (!conn->public_ws.connected)
            {
                ws_remove_connection(i);
                continue;
            }
            if (FD_ISSET(conn->public_ws.socket, &readfds))
                ws_poll_process_readable(conn);
            if (!conn->public_ws.connected)
            {
                ws_remove_connection(i);
                continue;
            }
            i++;
        }
        ws_unlock();
        free(fds);
    }

    return NULL;
}

static void ws_engine_start(void)
{
    if (ws_engine.engine_running)
        return;

#if defined(_WIN32) || defined(_WIN64)
    InitializeCriticalSection(&ws_engine.lock);
#else
    pthread_mutex_init(&ws_engine.lock, NULL);
#endif

    ws_engine.engine_running = 1;
    ws_engine.shutdown_requested = 0;
    _thread_create(&ws_engine.poll_thread, ws_poll_loop, NULL);
}

void cHTTPX_WSocketShutdown(void)
{
    if (!ws_engine.engine_running)
        return;

    ws_engine.shutdown_requested = 1;
    _thread_join(ws_engine.poll_thread);

    ws_lock();
    while (ws_engine.count > 0)
        ws_remove_connection(0);
    free(ws_engine.items);
    ws_engine.items = NULL;
    ws_engine.count = 0;
    ws_engine.capacity = 0;
    ws_unlock();

#if defined(_WIN32) || defined(_WIN64)
    DeleteCriticalSection(&ws_engine.lock);
#else
    pthread_mutex_destroy(&ws_engine.lock);
#endif

    ws_engine.engine_running = 0;
}

int cHTTPX_WSocketTryHandle(chttpx_request_t* req)
{
    if (!is_websocket_upgrade(req))
        return 0;

    chttpx_wsocket_route_entry_t* route = find_ws_route(req);
    if (!route)
        return 0;

    const char* ws_key = cHTTPX_HeaderGet(req, "Sec-WebSocket-Key");
    if (ws_do_upgrade(req->client_fd, ws_key) != 0)
        return -1;

    ws_engine_start();

    ws_lock();
    if (ws_add_connection(req->client_fd, route, req) != 0)
    {
        ws_unlock();
        chttpx_close(req->client_fd);
        return -1;
    }
    ws_unlock();

    return 1;
}
