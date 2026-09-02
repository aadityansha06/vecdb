#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>

/* Minimal, dependency-free test harness for vecdb's C regression suite.
 * Each TEST_CASE is a plain static function. Assertions `return;` early
 * on failure (so keep test bodies free of manual cleanup that MUST run
 * even on failure -- if you need guaranteed cleanup, do it before the
 * risky assertion, or accept the small leak in a failing test run). */

static int g_tests_run    = 0;
static int g_tests_failed = 0;
static int g_current_failed = 0;
static const char *g_current_test = "";

#define TEST_CASE(name) static void name(void)

#define RUN_TEST(fn) do {                              \
    g_current_test = #fn;                               \
    g_current_failed = 0;                                \
    g_tests_run++;                                        \
    fn();                                                   \
    if (g_current_failed) {                                 \
        g_tests_failed++;                                    \
        printf("[FAIL] %s\n", #fn);                           \
    } else {                                                   \
        printf("[PASS] %s\n", #fn);                             \
    }                                                            \
} while (0)

#define ASSERT_TRUE(cond) do {                                            \
    if (!(cond)) {                                                          \
        g_current_failed = 1;                                                \
        printf("       assertion failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
        return;                                                                \
    }                                                                           \
} while (0)

#define ASSERT_EQ_U64(a, b) do {                                          \
    unsigned long long _a = (unsigned long long)(a);                        \
    unsigned long long _b = (unsigned long long)(b);                         \
    if (_a != _b) {                                                           \
        g_current_failed = 1;                                                  \
        printf("       assertion failed: %s == %s  (%llu != %llu)  (%s:%d)\n", \
               #a, #b, _a, _b, __FILE__, __LINE__);                             \
        return;                                                                  \
    }                                                                             \
} while (0)

#define ASSERT_NEAR(a, b, eps) do {                                        \
    double _a = (double)(a), _b = (double)(b);                              \
    double _d = _a - _b; if (_d < 0) _d = -_d;                               \
    if (_d > (eps)) {                                                         \
        g_current_failed = 1;                                                  \
        printf("       assertion failed: %s ~= %s  (%f vs %f)  (%s:%d)\n",      \
               #a, #b, _a, _b, __FILE__, __LINE__);                              \
        return;                                                                   \
    }                                                                              \
} while (0)

#define ASSERT_NOT_NULL(p) do {                                            \
    if ((p) == NULL) {                                                       \
        g_current_failed = 1;                                                 \
        printf("       assertion failed: %s != NULL  (%s:%d)\n",              \
               #p, __FILE__, __LINE__);                                         \
        return;                                                                  \
    }                                                                             \
} while (0)

#define TEST_SUMMARY() \
    printf("\n===== %d run, %d passed, %d failed =====\n", \
           g_tests_run, g_tests_run - g_tests_failed, g_tests_failed)

#endif
