#include "libuhttpx.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

void send_response(int client_fd, http_response_t res) {
    char buffer[BUFFER_SIZE];

    int len = snprintf(buffer, BUFFER_SIZE, "HTTP/1.1 %d\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s", res.status, res.content_type, strlen(res.body), res.body);
    send(client_fd, buffer, len, 0);
}