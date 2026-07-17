/*
 * Copyright (c) 2026 netcorelink
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "websocket.h"

int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key)
{
    char accept_key[128] = {0};
    char buffer[256];

    char key_concat[128];
    snprintf(key_concat, sizeof(key_concat), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", sec_wsocket_key);

    // unsigned char hash[20];
    // sha1((unsigned char*)key_concat, strlen(key_concat), hash);

    // base64_encode(hash, 20, accept_key);

    int n = snprintf(buffer, sizeof(buffer),
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n",
                     accept_key);
    send(client_socket, buffer, n, 0);

    return 0;
}

// static wsocket_read_frame(chttpx_socket_t client_fd, wsocket_frame_t* out)
// {
//     unsigned char hdr[2];

//     if (recv(client_fd, hdr, 2, MSG_WAITALL) <= 0) return -1;

//     int opcode = hdr[0] & 0x0F;
//     int masked = hdr[1] & 0x80;
//     int len = hdr[1] & 0x7F;
// }