#include "test_framework.h"

#include "libchttpx.h"

#include <string.h>

static void ping_handler(chttpx_request_t* req, chttpx_response_t* res)
{
    (void)req;
    *res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"pong\":true}");
}

TEST(test_init_and_shutdown)
{
    chttpx_serv_t serv = {0};

    ASSERT_EQ(0, cHTTPX_Init(&serv, 18080, NULL));
    ASSERT_EQ(18080, serv.port);
    ASSERT(serv.routes == NULL);
    ASSERT_EQ(0, serv.routes_count);

    cHTTPX_Shutdown();
}

TEST(test_register_http_routes)
{
    chttpx_serv_t serv = {0};

    ASSERT_EQ(0, cHTTPX_Init(&serv, 18081, NULL));

    chttpx_router_t api = cHTTPX_RoutePathPrefix("/api/v1");
    cHTTPX_RegisterRoute(&api, "GET", "/ping", ping_handler);
    cHTTPX_RegisterRoute(&api, "POST", "/users", ping_handler);

    ASSERT_EQ(2, (long long)serv.routes_count);
    ASSERT_STREQ("GET", serv.routes[0].method);
    ASSERT_STREQ("/api/v1/ping", serv.routes[0].path);
    ASSERT_STREQ("POST", serv.routes[1].method);
    ASSERT_STREQ("/api/v1/users", serv.routes[1].path);

    cHTTPX_Shutdown();
}

TEST(test_middleware_registration)
{
    chttpx_serv_t serv = {0};

    ASSERT_EQ(0, cHTTPX_Init(&serv, 18082, NULL));

    cHTTPX_MiddlewareRecovery();
    cHTTPX_MiddlewareRateLimiter(5, 1);

    ASSERT(serv.middleware.middleware_count >= 2);

    cHTTPX_Shutdown();
}

void run_server_tests(void)
{
    printf("server\n");
    RUN_TEST(test_init_and_shutdown);
    RUN_TEST(test_register_http_routes);
    RUN_TEST(test_middleware_registration);
}
