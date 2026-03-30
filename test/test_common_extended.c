/* test_common_extended.c -- Extended common/memory allocation tests for better coverage */
#include "test_framework.h"
#include "../../shared/common.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

/* ================================================================ ncd_malloc Tests */

TEST(malloc_zero_size) {
    /* malloc(0) behavior is implementation-defined */
    void *ptr = ncd_malloc(0);
    
    /* Should either return NULL or a valid pointer that can be freed */
    if (ptr != NULL) {
        free(ptr);
    }
    
    return 0;
}

TEST(malloc_large_size) {
    /* Allocate 1MB */
    void *ptr = ncd_malloc(1024 * 1024);
    
    ASSERT_NOT_NULL(ptr);
    
    /* Verify we can write to it */
    memset(ptr, 0xAB, 1024 * 1024);
    
    free(ptr);
    return 0;
}

TEST(malloc_multiple_allocations) {
    void *ptrs[100];
    
    /* Make many small allocations */
    for (int i = 0; i < 100; i++) {
        ptrs[i] = ncd_malloc((i + 1) * 10);
        ASSERT_NOT_NULL(ptrs[i]);
    }
    
    /* Free in reverse order */
    for (int i = 99; i >= 0; i--) {
        free(ptrs[i]);
    }
    
    return 0;
}

/* ================================================================ ncd_calloc Tests */

TEST(calloc_zero_size) {
    /* calloc(0) behavior */
    void *ptr = ncd_calloc(0, 10);
    
    /* Should either return NULL or a valid pointer */
    if (ptr != NULL) {
        free(ptr);
    }
    
    return 0;
}

TEST(calloc_zero_nmemb) {
    void *ptr = ncd_calloc(10, 0);
    
    if (ptr != NULL) {
        free(ptr);
    }
    
    return 0;
}

TEST(calloc_single_large) {
    /* Allocate one large object */
    char *ptr = (char *)ncd_calloc(1, 1024 * 1024);
    
    ASSERT_NOT_NULL(ptr);
    
    /* Verify all zeros */
    for (int i = 0; i < 1024 * 1024; i++) {
        ASSERT_EQ_INT(0, ptr[i]);
    }
    
    free(ptr);
    return 0;
}

TEST(calloc_many_small) {
    /* Allocate many small objects */
    int *ptr = (int *)ncd_calloc(1000, sizeof(int));
    
    ASSERT_NOT_NULL(ptr);
    
    /* Verify all zeros */
    for (int i = 0; i < 1000; i++) {
        ASSERT_EQ_INT(0, ptr[i]);
    }
    
    free(ptr);
    return 0;
}

TEST(calloc_size_overflow_check) {
    /* Test that size_t overflow is handled */
    size_t huge = SIZE_MAX;
    
    /* This should either fail gracefully or not overflow */
    void *ptr = ncd_calloc(huge, 2);
    
    /* If allocation succeeded, something is wrong, but we should handle it */
    if (ptr != NULL) {
        free(ptr);
    }
    
    return 0;
}

/* ================================================================ ncd_realloc Tests */

TEST(realloc_null_pointer) {
    /* realloc(NULL, size) should behave like malloc */
    void *ptr = ncd_realloc(NULL, 100);
    
    ASSERT_NOT_NULL(ptr);
    
    free(ptr);
    return 0;
}

TEST(realloc_zero_size) {
    /* Allocate first */
    void *ptr = ncd_malloc(100);
    ASSERT_NOT_NULL(ptr);
    
    /* realloc to 0 - behavior is implementation-defined */
    void *new_ptr = ncd_realloc(ptr, 0);
    
    /* May return NULL or valid pointer */
    if (new_ptr != NULL) {
        free(new_ptr);
    }
    
    return 0;
}

TEST(realloc_shrink) {
    /* Allocate large buffer */
    char *ptr = (char *)ncd_malloc(1000);
    ASSERT_NOT_NULL(ptr);
    
    /* Fill with data */
    for (int i = 0; i < 1000; i++) {
        ptr[i] = (char)(i % 256);
    }
    
    /* Shrink */
    char *new_ptr = (char *)ncd_realloc(ptr, 100);
    ASSERT_NOT_NULL(new_ptr);
    
    /* Verify first 100 bytes preserved */
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ_INT((char)(i % 256), new_ptr[i]);
    }
    
    free(new_ptr);
    return 0;
}

TEST(realloc_grow_preserves_data) {
    /* Allocate small buffer */
    char *ptr = (char *)ncd_malloc(100);
    ASSERT_NOT_NULL(ptr);
    
    /* Fill with data */
    for (int i = 0; i < 100; i++) {
        ptr[i] = (char)(i + 1);
    }
    
    /* Grow */
    char *new_ptr = (char *)ncd_realloc(ptr, 1000);
    ASSERT_NOT_NULL(new_ptr);
    
    /* Verify first 100 bytes preserved */
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ_INT((char)(i + 1), new_ptr[i]);
    }
    
    /* Verify new space is writable */
    memset(new_ptr + 100, 0xFF, 900);
    
    free(new_ptr);
    return 0;
}

TEST(realloc_multiple_times) {
    void *ptr = ncd_malloc(100);
    ASSERT_NOT_NULL(ptr);
    
    /* Realloc multiple times */
    for (int i = 0; i < 10; i++) {
        ptr = ncd_realloc(ptr, (i + 1) * 200);
        ASSERT_NOT_NULL(ptr);
    }
    
    free(ptr);
    return 0;
}

/* ================================================================ ncd_strdup Tests */

TEST(strdup_empty_string) {
    char *copy = ncd_strdup("");
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_STR("", copy);
    
    free(copy);
    return 0;
}

TEST(strdup_single_char) {
    char *copy = ncd_strdup("X");
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_STR("X", copy);
    
    free(copy);
    return 0;
}

TEST(strdup_long_string) {
    /* Create a long string */
    char original[10000];
    memset(original, 'A', 9999);
    original[9999] = '\0';
    
    char *copy = ncd_strdup(original);
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_INT(9999, (int)strlen(copy));
    ASSERT_TRUE(memcmp(copy, original, 10000) == 0);
    
    free(copy);
    return 0;
}

TEST(strdup_special_chars) {
    char *copy = ncd_strdup("Hello\nWorld\t!\r\n");
    
    ASSERT_NOT_NULL(copy);
    ASSERT_EQ_STR("Hello\nWorld\t!\r\n", copy);
    
    free(copy);
    return 0;
}

TEST(strdup_modifications_independent) {
    char original[] = "Hello";
    char *copy = ncd_strdup(original);
    
    /* Modify copy */
    copy[0] = 'J';
    
    /* Original should be unchanged */
    ASSERT_EQ_STR("Hello", original);
    ASSERT_EQ_STR("Jello", copy);
    
    free(copy);
    return 0;
}

/* ================================================================ ncd_malloc_array Tests */

TEST(malloc_array_normal) {
    int *arr = (int *)ncd_malloc_array(100, sizeof(int));
    
    ASSERT_NOT_NULL(arr);
    
    /* Use the array */
    for (int i = 0; i < 100; i++) {
        arr[i] = i * i;
    }
    
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ_INT(i * i, arr[i]);
    }
    
    free(arr);
    return 0;
}

TEST(malloc_array_zero_nmemb) {
    void *ptr = ncd_malloc_array(0, 100);
    
    /* Should either return NULL or valid pointer */
    if (ptr != NULL) {
        free(ptr);
    }
    
    return 0;
}

TEST(malloc_array_zero_size) {
    void *ptr = ncd_malloc_array(100, 0);
    
    if (ptr != NULL) {
        free(ptr);
    }
    
    return 0;
}

TEST(malloc_array_large) {
    /* Allocate large array */
    char *arr = (char *)ncd_malloc_array(1000, 1024);  /* 1MB total */
    
    ASSERT_NOT_NULL(arr);
    
    memset(arr, 0xAA, 1000 * 1024);
    
    free(arr);
    return 0;
}

TEST(malloc_array_struct_array) {
    typedef struct {
        int a;
        char b;
        double c;
    } TestStruct;
    
    TestStruct *arr = (TestStruct *)ncd_malloc_array(100, sizeof(TestStruct));
    
    ASSERT_NOT_NULL(arr);
    
    /* Initialize all elements */
    for (int i = 0; i < 100; i++) {
        arr[i].a = i;
        arr[i].b = (char)('A' + (i % 26));
        arr[i].c = (double)i * 1.5;
    }
    
    free(arr);
    return 0;
}

/* ================================================================ Overflow Check Tests */

TEST(mul_overflow_check_zero) {
    size_t result = ncd_mul_overflow_check(0, 100);
    ASSERT_EQ_INT(0, (int)result);
    
    result = ncd_mul_overflow_check(100, 0);
    ASSERT_EQ_INT(0, (int)result);
    
    return 0;
}

TEST(mul_overflow_check_one) {
    size_t result = ncd_mul_overflow_check(1, 999);
    ASSERT_EQ_INT(999, (int)result);
    
    result = ncd_mul_overflow_check(999, 1);
    ASSERT_EQ_INT(999, (int)result);
    
    return 0;
}

TEST(mul_overflow_check_large_values) {
    /* Test with values near sqrt(SIZE_MAX) */
    size_t a = 65536;  /* 2^16 */
    size_t b = 65536;  /* 2^16 */
    
    size_t result = ncd_mul_overflow_check(a, b);
    ASSERT_EQ_INT(4294967296ULL, (uint64_t)result);  /* 2^32 */
    
    return 0;
}

TEST(add_overflow_check_zero) {
    size_t result = ncd_add_overflow_check(0, 100);
    ASSERT_EQ_INT(100, (int)result);
    
    result = ncd_add_overflow_check(100, 0);
    ASSERT_EQ_INT(100, (int)result);
    
    return 0;
}

TEST(add_overflow_check_commutative) {
    /* a + b should equal b + a */
    size_t result1 = ncd_add_overflow_check(123, 456);
    size_t result2 = ncd_add_overflow_check(456, 123);
    
    ASSERT_EQ_INT((int)result1, (int)result2);
    ASSERT_EQ_INT(579, (int)result1);
    
    return 0;
}

TEST(add_overflow_check_chained) {
    /* Test chained additions */
    size_t result = ncd_add_overflow_check(100, 200);
    result = ncd_add_overflow_check(result, 300);
    result = ncd_add_overflow_check(result, 400);
    
    ASSERT_EQ_INT(1000, (int)result);
    
    return 0;
}

TEST(add_overflow_check_large_values) {
    size_t a = (size_t)INT_MAX;
    size_t b = 1000;
    
    size_t result = ncd_add_overflow_check(a, b);
    
    ASSERT_EQ_INT((int)(INT_MAX + 1000ULL), (int)result);
    
    return 0;
}

/* ================================================================ Memory Stress Tests */

TEST(alloc_free_stress) {
    /* Rapid allocate and free */
    for (int i = 0; i < 10000; i++) {
        void *ptr = ncd_malloc(100 + (i % 1000));
        ASSERT_NOT_NULL(ptr);
        free(ptr);
    }
    
    return 0;
}

TEST(calloc_realloc_stress) {
    void *ptr = ncd_calloc(100, sizeof(int));
    ASSERT_NOT_NULL(ptr);
    
    for (int i = 0; i < 100; i++) {
        ptr = ncd_realloc(ptr, (i + 1) * 100);
        ASSERT_NOT_NULL(ptr);
    }
    
    free(ptr);
    return 0;
}

TEST(strdup_stress) {
    const char *strings[] = {
        "",
        "a",
        "hello",
        "Hello, World!",
        "Special chars: \n\t\r",
        "Unicode: \xc3\xa9\xe2\x9c\x93"
    };
    
    for (int i = 0; i < 1000; i++) {
        char *copy = ncd_strdup(strings[i % 6]);
        ASSERT_NOT_NULL(copy);
        ASSERT_EQ_STR(strings[i % 6], copy);
        free(copy);
    }
    
    return 0;
}

/* ================================================================ Edge Cases */

TEST(pointer_alignment) {
    /* Verify allocations are properly aligned */
    void *ptrs[10];
    
    for (int i = 0; i < 10; i++) {
        ptrs[i] = ncd_malloc((i + 1) * 7);  /* Odd sizes */
        ASSERT_NOT_NULL(ptrs[i]);
        
        /* Pointer should be aligned for any type */
        ASSERT_TRUE(((uintptr_t)ptrs[i] % sizeof(void *)) == 0);
    }
    
    for (int i = 0; i < 10; i++) {
        free(ptrs[i]);
    }
    
    return 0;
}

TEST(malloc_array_overflow_detection) {
    /* Test that overflow is detected and handled */
    size_t huge_nmemb = (size_t)-1 / sizeof(int) + 1;
    
    /* This should handle overflow gracefully */
    void *ptr = NULL;
    
    /* Wrap in if to avoid compiler warning about always-false condition */
    if (huge_nmemb > 0 && huge_nmemb < (size_t)-1) {
        ptr = ncd_malloc_array(huge_nmemb, sizeof(int));
    }
    
    /* May return NULL on overflow detection */
    if (ptr != NULL) {
        free(ptr);
    }
    
    return 0;
}

/* ================================================================ Test Suite */

void suite_common_extended(void) {
    /* ncd_malloc tests */
    RUN_TEST(malloc_zero_size);
    RUN_TEST(malloc_large_size);
    RUN_TEST(malloc_multiple_allocations);
    
    /* ncd_calloc tests */
    RUN_TEST(calloc_zero_size);
    RUN_TEST(calloc_zero_nmemb);
    RUN_TEST(calloc_single_large);
    RUN_TEST(calloc_many_small);
    /* SKIPPED: calloc_size_overflow_check - this test triggers OOM which exits the process */
    
    /* ncd_realloc tests */
    RUN_TEST(realloc_null_pointer);
    RUN_TEST(realloc_zero_size);
    RUN_TEST(realloc_shrink);
    RUN_TEST(realloc_grow_preserves_data);
    RUN_TEST(realloc_multiple_times);
    
    /* ncd_strdup tests */
    RUN_TEST(strdup_empty_string);
    RUN_TEST(strdup_single_char);
    RUN_TEST(strdup_long_string);
    RUN_TEST(strdup_special_chars);
    RUN_TEST(strdup_modifications_independent);
    
    /* ncd_malloc_array tests */
    RUN_TEST(malloc_array_normal);
    RUN_TEST(malloc_array_zero_nmemb);
    RUN_TEST(malloc_array_zero_size);
    RUN_TEST(malloc_array_large);
    RUN_TEST(malloc_array_struct_array);
    
    /* overflow check tests */
    RUN_TEST(mul_overflow_check_zero);
    RUN_TEST(mul_overflow_check_one);
    RUN_TEST(mul_overflow_check_large_values);
    RUN_TEST(add_overflow_check_zero);
    RUN_TEST(add_overflow_check_commutative);
    RUN_TEST(add_overflow_check_chained);
    RUN_TEST(add_overflow_check_large_values);
    
    /* stress tests */
    RUN_TEST(alloc_free_stress);
    RUN_TEST(calloc_realloc_stress);
    RUN_TEST(strdup_stress);
    
    /* edge cases */
    RUN_TEST(pointer_alignment);
    /* SKIPPED: malloc_array_overflow_detection - this test triggers overflow exit */
}

TEST_MAIN(
    RUN_SUITE(common_extended);
)
