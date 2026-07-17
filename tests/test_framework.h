#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>

extern int g_tests_run;
extern int g_tests_failed;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                           \
    do                                           \
    {                                            \
        int _failed_before = g_tests_failed;     \
        printf("  %s ... ", #name);              \
        fflush(stdout);                          \
        g_tests_run++;                           \
        name();                                  \
        if (g_tests_failed == _failed_before)    \
            printf("ok\n");                      \
    } while (0)

#define ASSERT(cond)                                           \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            printf("FAIL\n  assertion: %s (%s:%d)\n", #cond, \
                   __FILE__, __LINE__);                        \
            g_tests_failed++;                                  \
            return;                                            \
        }                                                      \
    } while (0)

#define ASSERT_STREQ(expected, actual)                                      \
    do                                                                      \
    {                                                                       \
        const char* _exp = (expected);                                      \
        const char* _act = (actual);                                        \
        if (!_exp || !_act || strcmp(_exp, _act) != 0)                      \
        {                                                                   \
            printf("FAIL\n  expected '%s', got '%s' (%s:%d)\n", _exp, _act, \
                   __FILE__, __LINE__);                                     \
            g_tests_failed++;                                               \
            return;                                                         \
        }                                                                   \
    } while (0)

#define ASSERT_EQ(expected, actual)                                           \
    do                                                                        \
    {                                                                         \
        long long _exp = (long long)(expected);                               \
        long long _act = (long long)(actual);                                 \
        if (_exp != _act)                                                     \
        {                                                                     \
            printf("FAIL\n  expected %lld, got %lld (%s:%d)\n", _exp, _act, \
                   __FILE__, __LINE__);                                       \
            g_tests_failed++;                                                 \
            return;                                                           \
        }                                                                     \
    } while (0)

#endif
