/**
 * InventixDB Unit Test Framework
 * 
 * Lightweight testing framework for unit and integration tests.
 * Features:
 * - Test registration and discovery
 * - Assertions with detailed failure messages
 * - Test fixtures (setup/teardown)
 * - Test filtering and grouping
 * - Colored output
 * - Timing information
 * - Memory leak detection integration
 */

#ifndef INVENTIX_TEST_FRAMEWORK_H
#define INVENTIX_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// ============================================================================
// CONFIGURATION
// ============================================================================

#define MAX_TESTS 1000
#define MAX_TEST_NAME 128
#define MAX_TEST_GROUPS 50
#define MAX_GROUP_NAME 64
#define MAX_ASSERTION_MSG 512

// ============================================================================
// ANSI COLORS (Windows compatible)
// ============================================================================

#ifdef _WIN32
#include <windows.h>
#define ENABLE_COLORS() do { \
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE); \
    DWORD dwMode = 0; \
    GetConsoleMode(hOut, &dwMode); \
    SetConsoleMode(hOut, dwMode | 0x0004); \
} while(0)
#else
#define ENABLE_COLORS() ((void)0)
#endif

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"

// ============================================================================
// TEST RESULT TYPES
// ============================================================================

typedef enum {
    TEST_PASS,
    TEST_FAIL,
    TEST_SKIP,
    TEST_ERROR
} TestResult;

typedef struct {
    const char *file;
    int line;
    char message[MAX_ASSERTION_MSG];
} FailureInfo;

typedef struct {
    char name[MAX_TEST_NAME];
    char group[MAX_GROUP_NAME];
    void (*test_func)(void);
    void (*setup)(void);
    void (*teardown)(void);
    TestResult result;
    double elapsed_ms;
    FailureInfo failure;
    bool skip;
    const char *skip_reason;
} TestCase;

typedef struct {
    char name[MAX_GROUP_NAME];
    void (*setup)(void);
    void (*teardown)(void);
} TestGroup;

typedef struct {
    TestCase tests[MAX_TESTS];
    int test_count;
    TestGroup groups[MAX_TEST_GROUPS];
    int group_count;
    
    int passed;
    int failed;
    int skipped;
    int errors;
    double total_time_ms;
    
    bool verbose;
    bool stop_on_failure;
    const char *filter_group;
    const char *filter_test;
    bool check_memory_leaks;
} TestRunner;

// ============================================================================
// GLOBAL TEST RUNNER
// ============================================================================

extern TestRunner g_test_runner;
extern TestCase *g_current_test;

// ============================================================================
// TEST REGISTRATION MACROS
// ============================================================================

#define TEST(name) \
    static void test_##name(void); \
    __attribute__((constructor)) static void register_test_##name(void) { \
        test_register(#name, "", test_##name, NULL, NULL); \
    } \
    static void test_##name(void)

#define TEST_GROUP(group, name) \
    static void test_##group##_##name(void); \
    __attribute__((constructor)) static void register_test_##group##_##name(void) { \
        test_register(#name, #group, test_##group##_##name, NULL, NULL); \
    } \
    static void test_##group##_##name(void)

#define TEST_F(fixture, name) \
    static void test_##fixture##_##name(void); \
    __attribute__((constructor)) static void register_test_##fixture##_##name(void) { \
        test_register(#name, #fixture, test_##fixture##_##name, \
                      fixture##_setup, fixture##_teardown); \
    } \
    static void test_##fixture##_##name(void)

#define SKIP_TEST(reason) do { \
    g_current_test->skip = true; \
    g_current_test->skip_reason = (reason); \
    return; \
} while(0)

// ============================================================================
// ASSERTION MACROS
// ============================================================================

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        test_fail(__FILE__, __LINE__, "ASSERT_TRUE failed: %s", #cond); \
        return; \
    } \
} while(0)

#define ASSERT_FALSE(cond) do { \
    if (cond) { \
        test_fail(__FILE__, __LINE__, "ASSERT_FALSE failed: %s", #cond); \
        return; \
    } \
} while(0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        test_fail(__FILE__, __LINE__, "ASSERT_NULL failed: %s is not NULL", #ptr); \
        return; \
    } \
} while(0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        test_fail(__FILE__, __LINE__, "ASSERT_NOT_NULL failed: %s is NULL", #ptr); \
        return; \
    } \
} while(0)

#define ASSERT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_EQ failed: %s == %s (expected %lld, got %lld)", \
            #expected, #actual, _e, _a); \
        return; \
    } \
} while(0)

#define ASSERT_NE(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e == _a) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_NE failed: %s != %s (both are %lld)", \
            #expected, #actual, _e); \
        return; \
    } \
} while(0)

#define ASSERT_LT(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (!(_a < _b)) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_LT failed: %s < %s (%lld >= %lld)", #a, #b, _a, _b); \
        return; \
    } \
} while(0)

#define ASSERT_LE(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (!(_a <= _b)) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_LE failed: %s <= %s (%lld > %lld)", #a, #b, _a, _b); \
        return; \
    } \
} while(0)

#define ASSERT_GT(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (!(_a > _b)) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_GT failed: %s > %s (%lld <= %lld)", #a, #b, _a, _b); \
        return; \
    } \
} while(0)

#define ASSERT_GE(a, b) do { \
    long long _a = (long long)(a); \
    long long _b = (long long)(b); \
    if (!(_a >= _b)) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_GE failed: %s >= %s (%lld < %lld)", #a, #b, _a, _b); \
        return; \
    } \
} while(0)

#define ASSERT_STR_EQ(expected, actual) do { \
    const char *_e = (expected); \
    const char *_a = (actual); \
    if (_e == NULL && _a == NULL) break; \
    if (_e == NULL || _a == NULL || strcmp(_e, _a) != 0) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_STR_EQ failed: \"%s\" != \"%s\"", \
            _e ? _e : "(null)", _a ? _a : "(null)"); \
        return; \
    } \
} while(0)

#define ASSERT_STR_NE(expected, actual) do { \
    const char *_e = (expected); \
    const char *_a = (actual); \
    if (_e != NULL && _a != NULL && strcmp(_e, _a) == 0) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_STR_NE failed: both are \"%s\"", _e); \
        return; \
    } \
} while(0)

#define ASSERT_STR_CONTAINS(haystack, needle) do { \
    const char *_h = (haystack); \
    const char *_n = (needle); \
    if (_h == NULL || _n == NULL || strstr(_h, _n) == NULL) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_STR_CONTAINS failed: \"%s\" not in \"%s\"", \
            _n ? _n : "(null)", _h ? _h : "(null)"); \
        return; \
    } \
} while(0)

#define ASSERT_MEM_EQ(expected, actual, size) do { \
    if (memcmp((expected), (actual), (size)) != 0) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_MEM_EQ failed: memory blocks differ"); \
        return; \
    } \
} while(0)

#define ASSERT_DOUBLE_EQ(expected, actual, epsilon) do { \
    double _e = (expected); \
    double _a = (actual); \
    double _diff = _e > _a ? _e - _a : _a - _e; \
    if (_diff > (epsilon)) { \
        test_fail(__FILE__, __LINE__, \
            "ASSERT_DOUBLE_EQ failed: %f != %f (diff=%f, epsilon=%f)", \
            _e, _a, _diff, (double)(epsilon)); \
        return; \
    } \
} while(0)

// Non-fatal assertions (continue after failure)
#define EXPECT_TRUE(cond) do { \
    if (!(cond)) { \
        test_fail(__FILE__, __LINE__, "EXPECT_TRUE failed: %s", #cond); \
    } \
} while(0)

#define EXPECT_EQ(expected, actual) do { \
    long long _e = (long long)(expected); \
    long long _a = (long long)(actual); \
    if (_e != _a) { \
        test_fail(__FILE__, __LINE__, \
            "EXPECT_EQ failed: %s == %s (expected %lld, got %lld)", \
            #expected, #actual, _e, _a); \
    } \
} while(0)

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void test_runner_init(void);
void test_runner_shutdown(void);

void test_register(const char *name, const char *group, 
                   void (*func)(void),
                   void (*setup)(void),
                   void (*teardown)(void));

void test_fail(const char *file, int line, const char *fmt, ...);

int test_run_all(void);
int test_run_group(const char *group);
int test_run_single(const char *name);

void test_set_verbose(bool verbose);
void test_set_stop_on_failure(bool stop);
void test_set_memory_leak_check(bool check);
void test_set_filter(const char *group, const char *test);

void test_print_summary(void);

// Timer utilities
double test_get_time_ms(void);

#endif // INVENTIX_TEST_FRAMEWORK_H
