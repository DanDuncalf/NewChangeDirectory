/* test_framework.h -- Minimal unit testing framework for NCD */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* Test statistics */
extern int tests_run;
extern int tests_passed;
extern int tests_failed;
extern int tests_skipped;
extern int asserts_total;
extern int asserts_failed;

/* SKIP sentinel value — distinct from 0 (pass) and 1 (fail) */
#define TEST_SKIP 77

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

#define ASSERT_STR_CONTAINS(haystack, needle) do { \
    asserts_total++; \
    if (strstr((haystack), (needle)) == NULL) { \
        asserts_failed++; \
        fprintf(stderr, "  FAIL: %s:%d: Expected \"%s\" to contain \"%s\"\n", \
                __FILE__, __LINE__, (haystack), (needle)); \
        return 1; \
    } \
} while(0)

/* Test function signature */
typedef int (*test_func_t)(void);

/* Test registration */
#define TEST(name) static int test_##name(void)

/* 
 * SKIP_TEST(reason) — marks a test as skipped with a structured reason.
 * Prints a machine-parseable marker that the Python executor can detect.
 */
#define SKIP_TEST(reason) do { \
    printf("=== SKIP %s ===\n", reason); \
    return TEST_SKIP; \
} while(0)

#define RUN_TEST(name) do { \
    printf("  Running %s...\n", #name); \
    tests_run++; \
    int _ret_##name = test_##name(); \
    if (_ret_##name == 0) { \
        tests_passed++; \
        printf("    PASSED\n"); \
    } else if (_ret_##name == TEST_SKIP) { \
        tests_skipped++; \
        printf("    SKIPPED\n"); \
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
    printf("Tests: %d run, %d passed, %d failed, %d skipped\n", \
           tests_run, tests_passed, tests_failed, tests_skipped); \
    printf("Assertions: %d total, %d failed\n", \
           asserts_total, asserts_failed); \
    return tests_failed > 0 ? 1 : 0; \
}

#endif
