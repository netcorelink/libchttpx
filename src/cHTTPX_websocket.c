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

#include "cHTTPX_websocket.h"

/* Beta version */

/**
 * Perform the HTTP Upgrade handshake for WebSocket (legacy API).
 *
 * The legacy raw-socket upgrade API cannot identify an HTTP/2 stream. The
 * symbol is kept for source compatibility and fails explicitly instead of
 * emitting an invalid connection-level upgrade response.
 *
 * @param client_socket Connected client socket.
 * @param sec_wsocket_key Sec-WebSocket-Key header value.
 * @return cHTTPX_ERR_UNAVAILABLE.
 */
int cHTTPX_WSocketUpgrade(int client_socket, const char* sec_wsocket_key)
{
    (void)client_socket;
    (void)sec_wsocket_key;
    return cHTTPX_ERR_UNAVAILABLE;
}

// static wsocket_read_frame(chttpx_socket_t client_fd, wsocket_frame_t* out)
// {
//     unsigned char hdr[2];

//     if (recv(client_fd, hdr, 2, MSG_WAITALL) <= 0) return -1;

//     int opcode = hdr[0] & 0x0F;
//     int masked = hdr[1] & 0x80;
//     int len = hdr[1] & 0x7F;
// }
