#include <libchttpx.h>

#include <stdio.h>

static void on_message(chttpx_wsocket_t* websocket, const unsigned char* data, size_t len)
{
    if (websocket->opcode == CHTTPX_WSOCKET_OPCODE_TEXT)
        cHTTPX_WSocketSend(websocket, data, len);
    else
        cHTTPX_WSocketSendBinary(websocket, data, len);
}

static void on_close(chttpx_wsocket_t* websocket, uint16_t code, const unsigned char* reason, size_t len)
{
    (void)websocket;
    (void)reason;
    (void)len;
    printf("websocket closed: %u\n", code);
}

static void websocket_route(chttpx_wsocket_t* websocket)
{
    cHTTPX_WSocketOnMessage(websocket, on_message);
    cHTTPX_WSocketOnClose(websocket, on_close);
}

int main(void)
{
    chttpx_app_t app;
    cHTTPX_AppInit(&app);

    chttpx_config_t config = cHTTPX_DefaultConfig();
    config.port = 8080;

    chttpx_serv_t* server = cHTTPX_AppServer(&app, "main", &config);
    if (!server)
        return 1;

    chttpx_router_t router = cHTTPX_RoutePathPrefix(server, "");
    cHTTPX_WSocketRegisterRoute(&router, "/ws", websocket_route);

    printf("WebSocket endpoint: /ws\n");
    cHTTPX_AppRun(&app);
    cHTTPX_AppFree(&app);
    return 0;
}
