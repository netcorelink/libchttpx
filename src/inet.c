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

#include "inet.h"

#include "crosspltm.h"

/**
 * Get client IP address from the underlying socket connection.
 *
 * This function retrieves the real network-level IP address of the client
 * using the TCP socket (`getpeername`). It supports both IPv4 and IPv6.
 *
 * The returned value is a pointer to a static buffer, so it will be
 * overwritten on subsequent calls and is NOT thread-safe.
 *
 * This IP cannot be spoofed by HTTP headers, but if the server is behind
 * a reverse proxy (Nginx, CDN, load balancer), the returned address will
 * be the proxy’s IP instead of the original client.
 *
 * @param client_fd Connected client socket file descriptor.
 * @return Pointer to a string with the client IP address,
 *         or "-" if the address cannot be determined.
 */
const char* cHTTPX_ClientInetIP(chttpx_socket_t client_fd)
{
    static char ip[INET6_ADDRSTRLEN];
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    if (getpeername(client_fd, (struct sockaddr*)&addr, &len) == -1)
        return "-";

    if (addr.ss_family == AF_INET)
    {
        struct sockaddr_in* s = (struct sockaddr_in*)&addr;
        if (!inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip)))
        {
            return "-";
        }
    }
    else if (addr.ss_family == AF_INET6)
    {
        struct sockaddr_in6* s = (struct sockaddr_in6*)&addr;
        if (!inet_ntop(AF_INET6, &s->sin6_addr, ip, sizeof(ip)))
        {
            return "-";
        }
    }
    else
    {
        strncpy(ip, "-", sizeof(ip));
    }

    return ip;
}