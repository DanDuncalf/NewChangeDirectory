/* test_ui_history.c -- UI history browser tests for NCD
 * 
 * Tests the interactive history browser UI with an in-memory backend.
 */

#include "test_framework.h"
#include "../src/ui.h"
#include "../src/matcher.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>

/* Helper: Create mock metadata with history */
static NcdMetadata *create_mock_metadata(void) {
    NcdMetadata *meta = (NcdMetadata *)calloc(1, sizeof(NcdMetadata));
    if (!meta) return NULL;
    
    /* Initialize minimal metadata */
    meta->cfg.magic = NCD_CFG_MAGIC;
    meta->cfg.version = NCD_CFG_VERSION;
    
    return meta;
}

static void free_mock_metadata(NcdMetadata *meta) {
    free(meta);
}

/* Test: History browser initial render */
TEST(history_initial_render) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\NCD");
    strcpy(matches[1].full_path, "C:\\Users\\Admin");
    strcpy(matches[2].full_path, "C:\\Temp");
    
    int count = 3;
    
    /* Inject ESC to cancel */
    ui_inject_keys("ESC");
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    /* Cancelled */
    ASSERT_EQ_INT(-1, result);
    
    /* Check that history UI was rendered */
    int header_row = ui_test_backend_find_row("History");
    ASSERT_TRUE(header_row >= 0);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Select from history */
TEST(history_select_item) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    strcpy(matches[2].full_path, "C:\\Third");
    
    int count = 3;
    
    /* DOWN to select second item, then ENTER */
    ui_inject_keys("DOWN,ENTER");
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    /* Selected index 1 */
    ASSERT_EQ_INT(1, result);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Navigate with arrow keys */
TEST(history_navigation) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[5];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\One");
    strcpy(matches[1].full_path, "C:\\Two");
    strcpy(matches[2].full_path, "C:\\Three");
    strcpy(matches[3].full_path, "C:\\Four");
    strcpy(matches[4].full_path, "C:\\Five");
    
    int count = 5;
    
    /* DOWN, DOWN, UP, then ENTER - should select index 1 */
    ui_inject_keys("DOWN,DOWN,UP,ENTER");
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    ASSERT_EQ_INT(1, result);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: PGUP/PGDN in history */
TEST(history_page_navigation) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* Create many items */
    NcdMatch matches[10];
    char paths[10][64];
    memset(matches, 0, sizeof(matches));
    
    for (int i = 0; i < 10; i++) {
        snprintf(paths[i], sizeof(paths[i]), "C:\\Path%d", i);
        strcpy(matches[i].full_path, paths[i]);
    }
    
    int count = 10;
    
    /* PGDN, ENTER */
    ui_inject_keys("PGDN,ENTER");
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    /* Should have selected something */
    ASSERT_TRUE(result >= 0);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: HOME/END keys */
TEST(history_home_end) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[5];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\A");
    strcpy(matches[1].full_path, "C:\\B");
    strcpy(matches[2].full_path, "C:\\C");
    strcpy(matches[3].full_path, "C:\\D");
    strcpy(matches[4].full_path, "C:\\E");
    
    int count = 5;
    
    /* END, ENTER - select last */
    ui_inject_keys("END,ENTER");
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    ASSERT_EQ_INT(4, result);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Single item returns immediately */
TEST(history_single_item) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[1];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Only");
    
    int count = 1;
    
    /* Single item returns immediately without showing UI */
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    ASSERT_EQ_INT(0, result);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Empty history returns -1 */
TEST(history_empty) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch *matches = NULL;
    int count = 0;
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    ASSERT_EQ_INT(-1, result);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Delete key handling (mock) */
TEST(history_delete_key) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    strcpy(matches[2].full_path, "C:\\Third");
    
    int count = 3;
    
    /* DELETE first item, then select what becomes first */
    ui_inject_keys("DELETE,DOWN,UP,ENTER");
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add some fake history entries so delete has something to remove */
    db_dir_history_add(meta, "C:\\First", 'C');
    db_dir_history_add(meta, "C:\\Second", 'C');
    db_dir_history_add(meta, "C:\\Third", 'C');
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    /* Should have returned a valid index */
    ASSERT_TRUE(result >= 0);
    /* Count should be decremented by 1 after delete */
    ASSERT_EQ_INT(2, count);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Delete all items until empty */
TEST(history_delete_until_empty) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[2];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    
    int count = 2;
    
    /* DELETE both items */
    ui_inject_keys("DELETE,DELETE");
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    db_dir_history_add(meta, "C:\\First", 'C');
    db_dir_history_add(meta, "C:\\Second", 'C');
    
    int result = ui_select_history(matches, &count, meta, NULL, NULL);
    
    /* Empty list returns -1 */
    ASSERT_EQ_INT(-1, result);
    ASSERT_EQ_INT(0, count);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

void suite_ui_history(void) {
    RUN_TEST(history_initial_render);
    RUN_TEST(history_select_item);
    RUN_TEST(history_navigation);
    RUN_TEST(history_page_navigation);
    RUN_TEST(history_home_end);
    RUN_TEST(history_single_item);
    RUN_TEST(history_empty);
    RUN_TEST(history_delete_key);
    RUN_TEST(history_delete_until_empty);
}

TEST_MAIN(
    RUN_SUITE(ui_history);
)
