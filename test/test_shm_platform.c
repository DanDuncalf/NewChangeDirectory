/* test_shm_platform.c -- Tests for shared memory platform layer (Tier 4) */
#include "test_framework.h"
#include "../src/shm_platform.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================ Tier 4: SHM Platform Tests */

TEST(shm_platform_init_succeeds) {
    ShmResult result = shm_platform_init();
    
    ASSERT_EQ_INT(SHM_OK, result);
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_create_close_lifecycle) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    /* Create a test shared memory object */
    ShmHandle *handle = NULL;
    result = shm_create("ncd_test_lifecycle", 4096, &handle);
    
    ASSERT_EQ_INT(SHM_OK, result);
    ASSERT_NOT_NULL(handle);
    
    /* Close the handle */
    shm_close(handle);
    
    /* Clean up the object */
    shm_remove("ncd_test_lifecycle");
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_create_then_open_reads_same_data) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    /* Create and map */
    ShmHandle *create_handle = NULL;
    result = shm_create("ncd_test_rw", 4096, &create_handle);
    ASSERT_EQ_INT(SHM_OK, result);
    
    void *addr = NULL;
    size_t size = 0;
    result = shm_map(create_handle, SHM_ACCESS_WRITE, &addr, &size);
    ASSERT_EQ_INT(SHM_OK, result);
    
    /* Write test data */
    strcpy((char *)addr, "Hello, Shared Memory!");
    shm_unmap(addr, size);
    shm_close(create_handle);
    
    /* Open existing and read */
    ShmHandle *open_handle = NULL;
    result = shm_open_existing("ncd_test_rw", SHM_ACCESS_READ, &open_handle);
    ASSERT_EQ_INT(SHM_OK, result);
    
    void *read_addr = NULL;
    result = shm_map(open_handle, SHM_ACCESS_READ, &read_addr, &size);
    ASSERT_EQ_INT(SHM_OK, result);
    
    ASSERT_EQ_STR("Hello, Shared Memory!", (char *)read_addr);
    
    shm_unmap(read_addr, size);
    shm_close(open_handle);
    shm_remove("ncd_test_rw");
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_map_unmap_lifecycle) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    ShmHandle *handle = NULL;
    result = shm_create("ncd_test_map", 4096, &handle);
    ASSERT_EQ_INT(SHM_OK, result);
    
    void *addr = NULL;
    size_t size = 0;
    result = shm_map(handle, SHM_ACCESS_READ, &addr, &size);
    ASSERT_EQ_INT(SHM_OK, result);
    ASSERT_NOT_NULL(addr);
    ASSERT_TRUE(size >= 4096);
    
    result = shm_unmap(addr, size);
    ASSERT_EQ_INT(SHM_OK, result);
    
    shm_close(handle);
    shm_remove("ncd_test_map");
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_get_size_returns_correct_size) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    ShmHandle *handle = NULL;
    size_t requested_size = 8192;
    result = shm_create("ncd_test_size", requested_size, &handle);
    ASSERT_EQ_INT(SHM_OK, result);
    
    size_t actual_size = shm_get_size(handle);
    ASSERT_TRUE(actual_size >= requested_size);
    
    shm_close(handle);
    shm_remove("ncd_test_size");
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_get_name_returns_correct_name) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    const char *test_name = "ncd_test_name";
    ShmHandle *handle = NULL;
    result = shm_create(test_name, 4096, &handle);
    ASSERT_EQ_INT(SHM_OK, result);
    
    const char *name = shm_get_name(handle);
    ASSERT_NOT_NULL(name);
    /* Name may have platform prefix, so just check it contains our name */
    ASSERT_TRUE(strstr(name, "ncd_test_name") != NULL);
    
    shm_close(handle);
    shm_remove(test_name);
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_exists_returns_true_after_create) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    /* Initially should not exist */
    ASSERT_FALSE(shm_exists("ncd_test_exist"));
    
    /* Create it */
    ShmHandle *handle = NULL;
    result = shm_create("ncd_test_exist", 4096, &handle);
    ASSERT_EQ_INT(SHM_OK, result);
    
    /* Now should exist */
    ASSERT_TRUE(shm_exists("ncd_test_exist"));
    
    shm_close(handle);
    shm_remove("ncd_test_exist");
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_exists_returns_false_after_unlink) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    /* Create and verify exists */
    ShmHandle *handle = NULL;
    shm_create("ncd_test_unlink", 4096, &handle);
    shm_close(handle);
    
    ASSERT_TRUE(shm_exists("ncd_test_unlink"));
    
    /* Remove it */
    shm_remove("ncd_test_unlink");
    
    /* Now should not exist */
    ASSERT_FALSE(shm_exists("ncd_test_unlink"));
    
    shm_platform_cleanup();
    return 0;
}

TEST(shm_get_page_size_returns_nonzero) {
    size_t page_size = shm_get_page_size();
    
    ASSERT_TRUE(page_size > 0);
    /* Typical page sizes are 4K, 8K, 16K, 64K */
    ASSERT_TRUE(page_size == 4096 || page_size == 8192 || 
                page_size == 16384 || page_size == 65536 ||
                page_size == 32768 || page_size == 131072);
    
    return 0;
}

TEST(shm_error_string_returns_non_null) {
    const char *str;
    
    str = shm_error_string(SHM_OK);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = shm_error_string(SHM_ERROR_GENERIC);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = shm_error_string(SHM_ERROR_NOTFOUND);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = shm_error_string(SHM_ERROR_ACCESS);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    return 0;
}

TEST(shm_open_nonexistent_fails) {
    ShmResult result = shm_platform_init();
    ASSERT_EQ_INT(SHM_OK, result);
    
    ShmHandle *handle = NULL;
    result = shm_open_existing("ncd_test_nonexistent_xyz", SHM_ACCESS_READ, &handle);
    
    /* Should fail with NOTFOUND */
    ASSERT_TRUE(result == SHM_ERROR_NOTFOUND || result == SHM_ERROR_GENERIC);
    ASSERT_NULL(handle);
    
    shm_platform_cleanup();
    return 0;
}

/* ================================================================ Test Suite */

void suite_shm_platform(void) {
    printf("\n=== SHM Platform Tests ===\n\n");
    
    RUN_TEST(shm_platform_init_succeeds);
    RUN_TEST(shm_create_close_lifecycle);
    RUN_TEST(shm_create_then_open_reads_same_data);
    RUN_TEST(shm_map_unmap_lifecycle);
    RUN_TEST(shm_get_size_returns_correct_size);
    RUN_TEST(shm_get_name_returns_correct_name);
    RUN_TEST(shm_exists_returns_true_after_create);
    RUN_TEST(shm_exists_returns_false_after_unlink);
    RUN_TEST(shm_get_page_size_returns_nonzero);
    RUN_TEST(shm_error_string_returns_non_null);
    RUN_TEST(shm_open_nonexistent_fails);
}

TEST_MAIN(
    RUN_SUITE(shm_platform);
)
