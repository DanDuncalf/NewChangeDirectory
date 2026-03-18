/* test_framework.h -- Minimal unit testing framework for NCD */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Test statistics */
extern int tests_run;
extern int tests_passed;
extern int tests_failed;
extern int asserts_total;
extern int asserts_failed;

/* Assertion macros */
#define ASSERT_TRUE(cond) do { \
    asserts_total++; \
    if (!(cond)) { \
        asserts_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: Assertion failed: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ_INT(expected, actual) do { \
    asserts_total++; \
    if ((expected) != (actual)) { \
        asserts_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: Expected %d, got %d\n", \
                __FILE__, __LINE__, (int)(expected), (int)(actual)); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_STR(expected, actual) do { \
    asserts_total++; \
    if (strcmp((expected), (actual)) != 0) { \
        asserts_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: Expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, (expected), (actual)); \
        return 1; \
    } \
} while(0)

#define ASSERT_NULL(ptr) ASSERT_TRUE((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT_TRUE((ptr) != NULL)

#define ASSERT_EQ_MEM(expected, actual, len) do { \
    asserts_total++; \
    if (memcmp((expected), (actual), (len)) != 0) { \
        asserts_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: Memory contents differ\n", \
                __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

/* Test function signature */
typedef int (*test_func_t)(void);

/* Test registration */
#define TEST(name) static int test_##name(void)

#define RUN_TEST(name) do { \
    printf("  Running %s...\n", #name); \
    tests_run++; \
    if (test_##name() == 0) { \
        tests_passed++; \
        printf("    PASSED\n"); \
    } else { \
        tests_failed++; \
        printf("    FAILED\n"); \
    } \
} while(0)

/* Suite runner */
#define RUN_SUITE(suite_name) do { \
    printf("\n=== %s ===\n", #suite_name); \
    suite_##suite_name(); \
} while(0)

/* Main test runner */
#define TEST_MAIN(...) int main(void) { \
    printf("Starting test run...\n"); \
    __VA_ARGS__ \
    printf("\n========================================\n"); \
    printf("Tests: %d run, %d passed, %d failed\n", \
           tests_run, tests_passed, tests_failed); \
    printf("Assertions: %d total, %d failed\n", \
           asserts_total, asserts_failed); \
    return tests_failed > 0 ? 1 : 0; \
}

#endif
