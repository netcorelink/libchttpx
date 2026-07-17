#include "test_framework.h"

#include "libchttpx.h"

static void ws_open(chttpx_wsocket_t* ws, void* userdata)
{
    (void)ws;
    (void)userdata;
}

static void ws_message(chttpx_wsocket_t* ws, const unsigned char* data, size_t len, int opcode, void* userdata)
{
    (void)ws;
    (void)data;
    (void)len;
    (void)opcode;
    (void)userdata;
}

static void ws_close(chttpx_wsocket_t* ws, void* userdata)
{
    (void)ws;
    (void)userdata;
}

TEST(test_register_websocket_route)
{
    chttpx_serv_t serv = {0};
    static chttpx_wsocket_callbacks_t callbacks = {
        .on_open = ws_open,
        .on_message = ws_message,
        .on_close = ws_close,
    };

    ASSERT_EQ(0, cHTTPX_Init(&serv, 18083, NULL));

    chttpx_router_t api = cHTTPX_RoutePathPrefix("/api/v1");
    cHTTPX_WSocketRegisterRoute(&api, "/ws/chat/{room_id}", &callbacks);

    ASSERT_EQ(1, (long long)serv.ws_routes_count);
    ASSERT(serv.ws_routes[0].path != NULL);
    ASSERT_STREQ("/api/v1/ws/chat/{room_id}", serv.ws_routes[0].path);
    ASSERT(serv.ws_routes[0].on_open == ws_open);
    ASSERT(serv.ws_routes[0].on_message == ws_message);
    ASSERT(serv.ws_routes[0].on_close == ws_close);

    cHTTPX_Shutdown();
}

TEST(test_websocket_shutdown_without_connections)
{
    chttpx_serv_t serv = {0};
    static chttpx_wsocket_callbacks_t callbacks = {
        .on_open = ws_open,
        .on_message = ws_message,
        .on_close = ws_close,
    };

    ASSERT_EQ(0, cHTTPX_Init(&serv, 18084, NULL));

    chttpx_router_t api = cHTTPX_RoutePathPrefix("/api/v1");
    cHTTPX_WSocketRegisterRoute(&api, "/ws/chat/{room_id}", &callbacks);

    cHTTPX_Shutdown();
}

void run_websocket_tests(void)
{
    printf("websocket\n");
    RUN_TEST(test_register_websocket_route);
    RUN_TEST(test_websocket_shutdown_without_connections);
}
