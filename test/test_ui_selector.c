/* test_ui_selector.c -- UI selector tests for NCD
 * 
 * Tests the interactive match selector UI with an in-memory backend
 * to enable headless, deterministic testing.
 */

#include "test_framework.h"
#include "../src/ui.h"
#include "../src/matcher.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>

/* Test: Initial render shows matches and header */
TEST(selector_initial_render) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* Create mock matches */
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Windows\\System32");
    strcpy(matches[1].full_path, "C:\\Windows\\SysWOW64");
    strcpy(matches[2].full_path, "C:\\WinPython");
    
    /* Inject ESC key to cancel immediately */
    ui_inject_keys("ESC");
    
    /* Call selector - will use injected keys */
    int result = ui_select_match(matches, 3, "win");
    
    /* Verify result is -1 (cancelled) */
    ASSERT_EQ_INT(-1, result);
    
    /* Check that header row was rendered */
    int header_row = ui_test_backend_find_row("NCD");
    ASSERT_TRUE(header_row >= 0);
    
    /* Check that matches are visible */
    ASSERT_TRUE(ui_test_backend_find_row("System32") >= 0);
    ASSERT_TRUE(ui_test_backend_find_row("SysWOW64") >= 0);
    ASSERT_TRUE(ui_test_backend_find_row("WinPython") >= 0);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: DOWN key moves selection */
TEST(selector_down_key) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    strcpy(matches[2].full_path, "C:\\Third");
    
    /* DOWN then ENTER to select second item */
    ui_inject_keys("DOWN,ENTER");
    
    int result = ui_select_match(matches, 3, "test");
    
    /* Result should be index 1 (second item) */
    ASSERT_EQ_INT(1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Filter narrows results */
TEST(selector_filter_typing) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[4];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Documents\\Gamma");
    strcpy(matches[3].full_path, "C:\\Documents\\Delta");
    
    /* Type "Proj" to filter, then ENTER */
    ui_inject_keys("TEXT:Proj,ENTER");
    
    int result = ui_select_match(matches, 4, "test");
    
    /* Should select one of the Projects */
    ASSERT_TRUE(result >= 0 && result < 4);
    ASSERT_TRUE(strstr(matches[result].full_path, "Proj") != NULL);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Empty results handling */
TEST(selector_empty_results) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* No matches - single item returns immediately */
    NcdMatch matches[1];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Only");
    
    /* Single match returns 0 immediately without UI */
    int result = ui_select_match(matches, 1, "test");
    
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: PGUP/PGDN scroll through list */
TEST(selector_page_scroll) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* Create many matches to test scrolling */
    NcdMatch matches[20];
    char paths[20][64];
    memset(matches, 0, sizeof(matches));
    
    for (int i = 0; i < 20; i++) {
        snprintf(paths[i], sizeof(paths[i]), "C:\\Path\\Item%02d", i);
        strcpy(matches[i].full_path, paths[i]);
    }
    
    /* PGDN then ENTER */
    ui_inject_keys("PGDN,ENTER");
    
    int result = ui_select_match(matches, 20, "test");
    
    /* Should have scrolled and selected something */
    ASSERT_TRUE(result >= 0);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: HOME/END keys */
TEST(selector_home_end_keys) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[5];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    strcpy(matches[2].full_path, "C:\\Third");
    strcpy(matches[3].full_path, "C:\\Fourth");
    strcpy(matches[4].full_path, "C:\\Fifth");
    
    /* END then ENTER - should select last */
    ui_inject_keys("END,ENTER");
    
    int result = ui_select_match(matches, 5, "test");
    
    ASSERT_EQ_INT(4, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: ESC cancels selection */
TEST(selector_esc_cancels) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\One");
    strcpy(matches[1].full_path, "C:\\Two");
    strcpy(matches[2].full_path, "C:\\Three");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 3, "test");
    
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Clear filter with ESC, then cancel */
TEST(selector_esc_clears_filter_then_cancels) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Alpha");
    strcpy(matches[1].full_path, "C:\\Beta");
    strcpy(matches[2].full_path, "C:\\Gamma");
    
    /* Type filter, then ESC (clears filter), then ESC again (cancels) */
    ui_inject_keys("TEXT:x,ESC,ESC");
    
    int result = ui_select_match(matches, 3, "test");
    
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Snapshot capture works */
TEST(selector_snapshot_capture) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[2];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    
    /* Draw the UI but don't select yet - inject ESC after snapshot */
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 2, "test");
    ASSERT_EQ_INT(-1, result);
    
    /* Capture snapshot after UI closes */
    UiSnapshot *snap = ui_snapshot_capture();
    ASSERT_NOT_NULL(snap);
    
    ui_snapshot_free(snap);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Extended selector with navigator */
TEST(selector_ex_navigator_cancel) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[2];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Windows");
    strcpy(matches[1].full_path, "C:\\Users");
    
    /* TAB enters navigator, then ESC cancels */
    ui_inject_keys("TAB,ESC");
    
    char out_path[MAX_PATH] = {0};
    int result = ui_select_match_ex(matches, 2, "test", out_path, sizeof(out_path));
    
    /* Cancelled from navigator */
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

void suite_ui_selector(void) {
    RUN_TEST(selector_initial_render);
    RUN_TEST(selector_down_key);
    RUN_TEST(selector_filter_typing);
    RUN_TEST(selector_empty_results);
    RUN_TEST(selector_page_scroll);
    RUN_TEST(selector_home_end_keys);
    RUN_TEST(selector_esc_cancels);
    RUN_TEST(selector_esc_clears_filter_then_cancels);
    RUN_TEST(selector_snapshot_capture);
    RUN_TEST(selector_ex_navigator_cancel);
}

TEST_MAIN(
    RUN_SUITE(ui_selector);
)
