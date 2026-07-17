#include <stdio.h>

int g_tests_run = 0;
int g_tests_failed = 0;

void run_params_tests(void);
void run_response_tests(void);
void run_server_tests(void);
void run_websocket_tests(void);

int main(void)
{
    run_params_tests();
    run_response_tests();
    run_server_tests();
    run_websocket_tests();

    printf("\n%d tests, %d failed\n", g_tests_run, g_tests_failed);

    return g_tests_failed == 0 ? 0 : 1;
}
