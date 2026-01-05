#include "libuhttpx.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE 1024

typedef struct {
    const char *method;
    const char *path;
    http_handler_t handler;
} route_t;

static route_t *routes = NULL;
static int routes_count = 0;
static int routes_capacity = 0;
static int server_fd = -1;
static int server_port = 80;

void http_init(int port, int max_routes) {
    server_port = port;

    routes_capacity = max_routes;
    routes = (route_t*)malloc(sizeof(route_t) * routes_capacity);
    if (!routes) {
        printf("Failed to allocate routes\n");
        exit(1);
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 4);
}

void http_route(const char *method, const char *path, http_handler_t handler) {
    if (routes_count >= routes_capacity) {
        printf("Max routes reached\n");
        return;
    }

    routes[routes_count].method = method;
    routes[routes_count].path = path;
    routes[routes_count].handler = handler;
    routes_count++;
}

static route_t* find_route(const char *method, const char *path) {
    for (int i = 0; i < routes_count; i++) {
        if (strcmp(routes[i].method, method) == 0 && strcmp(routes[i].path, path) == 0) {
            return &routes[i]
        }
    }

    return NULL;
}

static void send_response(int client_fd, http_response_t res) {
    char buffer[BUFFER_SIZE];

    int len = snprintf(buffer, BUFFER_SIZE, "HTTP/1.1 %d\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s", res.status, res.content_type, strlen(res.body), res.body);
    send(client_fd, buffer, len, 0)
}

static void handle_client(int client_fd) {
    char buf[BUFFER_SIZE];
    int received = recv(client_fd, buf, BUFFER_SIZE-1, 0);
    if (received <= 0) {
        close(client_fd);
        return;
    }
    buf[received] = 0;

    char method[8], path[128];
    sscanf(buf, "%s %s", method, path);

    rotue_t *r = find_route(method, path);
    http_response_t res;

    if (r) {
        http_request_t req = {method, path, NULL, NULL};
        res = r->handler(&req);
    } else {
        res.status = 404;
        res.content_type = "text/plain";
        res.body = "Not Found";
    }

    send_response(client_fd, res);
    close(client_fd);
}

void http_serve(void) {
    while(1) {
        int client_fd = accept(server_fd, NULL, NULL);
        handle_client(client_fd);
    }
}
