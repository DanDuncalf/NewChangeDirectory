/* test_common.c -- Tests for common/memory allocation module */
#include "test_framework.h"
#include "../../shared/common.h"
#include <string.h>

TEST(malloc_returns_non_null) {
    void *ptr = ncd_malloc(100);
    ASSERT_NOT_NULL(ptr);
    free(ptr);
    return 0;
}

TEST(calloc_returns_zeroed_memory) {
    int *arr = (int *)ncd_calloc(10, sizeof(int));
    ASSERT_NOT_NULL(arr);
    
    /* All should be zero */
    for (int i = 0; i < 10; i++) {
        ASSERT_EQ_INT(0, arr[i]);
    }
    
    free(arr);
    return 0;
}

TEST(realloc_grows_allocation) {
    char *ptr = (char *)ncd_malloc(10);
    ASSERT_NOT_NULL(ptr);
    
    strcpy(ptr, "hello");
    
    char *new_ptr = (char *)ncd_realloc(ptr, 100);
    ASSERT_NOT_NULL(new_ptr);
    ASSERT_EQ_STR("hello", new_ptr);  /* Content preserved */
    
    free(new_ptr);
    return 0;
}

TEST(strdup_copies_string) {
    const char *original = "test string for duplication";
    char *copy = ncd_strdup(original);
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_STR(original, copy);
    ASSERT_TRUE(copy != original);  /* Different pointers */
    
    free(copy);
    return 0;
}

TEST(malloc_array_with_safe_sizes) {
    /* Test normal allocation */
    int *arr = (int *)ncd_malloc_array(100, sizeof(int));
    ASSERT_NOT_NULL(arr);
    
    /* Use the memory */
    for (int i = 0; i < 100; i++) {
        arr[i] = i;
    }
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ_INT(i, arr[i]);
    }
    
    free(arr);
    return 0;
}

TEST(mul_overflow_check_detects_overflow) {
    /* Test normal multiplication */
    size_t result1 = ncd_mul_overflow_check(100, 50);
    ASSERT_EQ_INT(5000, (int)result1);
    
    /* Test large but safe multiplication */
    size_t result2 = ncd_mul_overflow_check(1024, 1024);
    ASSERT_EQ_INT(1048576, (int)result2);
    
    return 0;
}

TEST(add_overflow_check_detects_overflow) {
    /* Test normal addition */
    size_t result1 = ncd_add_overflow_check(100, 200);
    ASSERT_EQ_INT(300, (int)result1);
    
    /* Test chained additions */
    size_t result2 = ncd_add_overflow_check(result1, 500);
    ASSERT_EQ_INT(800, (int)result2);
    
    return 0;
}

/* Test suites */
void suite_common(void) {
    RUN_TEST(malloc_returns_non_null);
    RUN_TEST(calloc_returns_zeroed_memory);
    RUN_TEST(realloc_grows_allocation);
    RUN_TEST(strdup_copies_string);
    RUN_TEST(malloc_array_with_safe_sizes);
    RUN_TEST(mul_overflow_check_detects_overflow);
    RUN_TEST(add_overflow_check_detects_overflow);
}

/* Main */
TEST_MAIN(
    RUN_SUITE(common);
)
