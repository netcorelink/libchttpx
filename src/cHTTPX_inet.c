/*
 * Copyright (c) 2026 netcorelink
 *
 * Distributed under the BSD 3-Clause License. See LICENSE for details.
 */

#include "cHTTPX_inet.h"

#include "cHTTPX_crosspltm.h"

/**
 * Get client IP address from the underlying socket connection.
 *
 * This function retrieves the real network-level IP address of the client
 * using the TCP socket (`getpeername`). It supports both IPv4 and IPv6.
 *
 * The returned value is stored in a thread-local buffer and remains valid
 * until the next call on the same thread.
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
    static _Thread_local char ip[INET6_ADDRSTRLEN];
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);

    if (getpeername(client_fd, (struct sockaddr*)&addr, &len) == -1)
        return "-";

    if (addr.ss_family == AF_INET)
    {
        struct sockaddr_in* s = (struct sockaddr_in*)&addr;
        if (!inet_ntop(AF_INET, &s->sin_addr, ip, sizeof(ip)))
            return "-";
        return ip;
    }

    if (addr.ss_family == AF_INET6)
    {
        struct sockaddr_in6* s = (struct sockaddr_in6*)&addr;

        if (IN6_IS_ADDR_V4MAPPED(&s->sin6_addr))
        {
            struct in_addr ipv4;
            const unsigned char* raw = (const unsigned char*)&s->sin6_addr;
            memcpy(&ipv4, raw + 12, sizeof(ipv4));

            if (!inet_ntop(AF_INET, &ipv4, ip, sizeof(ip)))
                return "-";
            return ip;
        }

        if (!inet_ntop(AF_INET6, &s->sin6_addr, ip, sizeof(ip)))
            return "-";
        return ip;
    }

    return "-";
}
