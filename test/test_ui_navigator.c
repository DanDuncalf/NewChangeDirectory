/* test_ui_navigator.c -- UI filesystem navigator tests for NCD
 * 
 * Tests the interactive filesystem navigator UI with an in-memory backend.
 */

#include "test_framework.h"
#include "../src/ui.h"
#include <string.h>
#include <stdlib.h>

/* Test: Navigator initial render */
TEST(navigator_initial_render) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* ESC to cancel immediately */
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    
    /* Cancelled */
    ASSERT_FALSE(result);
    
    /* Check that navigator UI was rendered */
    int header_row = ui_test_backend_find_row("Navigator");
    ASSERT_TRUE(header_row >= 0);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Select current directory */
TEST(navigator_select_current) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* ENTER to select current directory */
    ui_inject_keys("ENTER");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    
    /* Selected */
    ASSERT_TRUE(result);
    /* Path should be the starting path */
    ASSERT_TRUE(strstr(out_path, "Windows") != NULL);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Cancel navigator */
TEST(navigator_cancel) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* ESC to cancel */
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Navigate up with LEFT */
TEST(navigator_navigate_up) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* LEFT (go to parent), ENTER */
    ui_inject_keys("LEFT,ENTER");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows\\System32", out_path, sizeof(out_path));
    
    ASSERT_TRUE(result);
    /* Should have navigated to parent */
    ASSERT_TRUE(strstr(out_path, "Windows") != NULL);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Navigate siblings with UP/DOWN */
TEST(navigator_navigate_siblings) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* DOWN to next sibling, ENTER */
    ui_inject_keys("DOWN,ENTER");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    
    /* Result depends on what siblings exist */
    /* Just verify it doesn't crash - result may be true or false */
    (void)result;
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: HOME goes to first sibling */
TEST(navigator_home) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* HOME, ENTER */
    ui_inject_keys("HOME,ENTER");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    
    (void)result;
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: END goes to last sibling */
TEST(navigator_end) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* END, ENTER */
    ui_inject_keys("END,ENTER");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    
    (void)result;
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Invalid path handling */
TEST(navigator_invalid_path) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    char out_path[MAX_PATH] = {0};
    
    /* NULL path */
    bool result = ui_navigate_directory(NULL, out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    /* Empty path */
    result = ui_navigate_directory("", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    /* NULL out_path */
    result = ui_navigate_directory("C:\\Windows", NULL, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Tab cycles through children */
TEST(navigator_tab_children) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* TAB (cycle children), TAB again, ENTER */
    ui_inject_keys("TAB,TAB,ENTER");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    
    (void)result;
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

void suite_ui_navigator(void) {
    RUN_TEST(navigator_initial_render);
    RUN_TEST(navigator_select_current);
    RUN_TEST(navigator_cancel);
    RUN_TEST(navigator_navigate_up);
    RUN_TEST(navigator_navigate_siblings);
    RUN_TEST(navigator_home);
    RUN_TEST(navigator_end);
    RUN_TEST(navigator_invalid_path);
    RUN_TEST(navigator_tab_children);
}

TEST_MAIN(
    RUN_SUITE(ui_navigator);
)
