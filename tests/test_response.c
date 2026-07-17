#include "test_framework.h"

#include "libchttpx.h"

#include <stdlib.h>
#include <string.h>

TEST(test_res_json_body)
{
    chttpx_response_t res = cHTTPX_ResJson(cHTTPX_StatusOK, "{\"msg\":\"hi\"}");

    ASSERT_EQ(cHTTPX_StatusOK, res.status);
    ASSERT(res.body != NULL);
    ASSERT_STREQ("application/json", res.content_type);
    ASSERT_STREQ("{\"msg\":\"hi\"}", (const char*)res.body);
    ASSERT_EQ(12, (long long)res.body_size);

    free(res.body);
}

TEST(test_res_html_body)
{
    chttpx_response_t res = cHTTPX_ResHtml(cHTTPX_StatusOK, "<p>%s</p>", "test");

    ASSERT_EQ(cHTTPX_StatusOK, res.status);
    ASSERT(res.body != NULL);
    ASSERT_STREQ("text/html", res.content_type);
    ASSERT_STREQ("<p>test</p>", (const char*)res.body);

    free(res.body);
}

TEST(test_res_json_not_found)
{
    chttpx_response_t res = cHTTPX_ResJson(cHTTPX_StatusNotFound, "{\"error\":\"missing\"}");

    ASSERT_EQ(cHTTPX_StatusNotFound, res.status);
    ASSERT(res.body != NULL);
    free(res.body);
}

void run_response_tests(void)
{
    printf("response\n");
    RUN_TEST(test_res_json_body);
    RUN_TEST(test_res_html_body);
    RUN_TEST(test_res_json_not_found);
}
