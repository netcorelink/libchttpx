#include "test_framework.h"

#include "libchttpx.h"

TEST(test_match_static_path)
{
    chttpx_param_t params[MAX_PARAMS] = {0};
    int count = -1;

    ASSERT(cHTTPX_MatchPath("/api/v1/", "/api/v1/", params, &count));
    ASSERT_EQ(0, count);
}

TEST(test_match_single_param)
{
    chttpx_param_t params[MAX_PARAMS] = {0};
    int count = 0;

    ASSERT(cHTTPX_MatchPath("/users/{uuid}", "/users/abc-123", params, &count));
    ASSERT_EQ(1, count);
    ASSERT_STREQ("uuid", params[0].name);
    ASSERT_STREQ("abc-123", params[0].value);
}

TEST(test_match_ws_room_path)
{
    chttpx_param_t params[MAX_PARAMS] = {0};
    int count = 0;

    ASSERT(cHTTPX_MatchPath("/api/v1/ws/chat/{room_id}", "/api/v1/ws/chat/lobby", params, &count));
    ASSERT_EQ(1, count);
    ASSERT_STREQ("room_id", params[0].name);
    ASSERT_STREQ("lobby", params[0].value);
}

TEST(test_match_rejects_extra_segments)
{
    chttpx_param_t params[MAX_PARAMS] = {0};
    int count = 0;

    ASSERT(!cHTTPX_MatchPath("/users/{uuid}", "/users/abc/extra", params, &count));
}

TEST(test_match_rejects_missing_segments)
{
    chttpx_param_t params[MAX_PARAMS] = {0};
    int count = 0;

    ASSERT(!cHTTPX_MatchPath("/users/{uuid}/profile", "/users/abc", params, &count));
}

void run_params_tests(void)
{
    printf("params\n");
    RUN_TEST(test_match_static_path);
    RUN_TEST(test_match_single_param);
    RUN_TEST(test_match_ws_room_path);
    RUN_TEST(test_match_rejects_extra_segments);
    RUN_TEST(test_match_rejects_missing_segments);
}
