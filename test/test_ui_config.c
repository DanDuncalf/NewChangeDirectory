/* test_ui_config.c -- UI config editor tests for NCD
 * 
 * Tests the interactive configuration editor UI with an in-memory backend.
 */

#include "test_framework.h"
#include "../src/ui.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>

/* Helper: Create mock metadata */
static NcdMetadata *create_mock_metadata(void) {
    NcdMetadata *meta = (NcdMetadata *)calloc(1, sizeof(NcdMetadata));
    if (!meta) return NULL;
    
    /* Initialize config with defaults */
    meta->cfg.magic = NCD_CFG_MAGIC;
    meta->cfg.version = NCD_CFG_VERSION;
    meta->cfg.has_defaults = true;
    meta->cfg.default_show_hidden = false;
    meta->cfg.default_show_system = false;
    meta->cfg.default_fuzzy_match = false;
    meta->cfg.default_timeout = 300;
    meta->cfg.service_retry_count = NCD_DEFAULT_SERVICE_RETRY_COUNT;
    meta->cfg.rescan_interval_hours = NCD_RESCAN_HOURS_DEFAULT;
    
    return meta;
}

static void free_mock_metadata(NcdMetadata *meta) {
    free(meta);
}

/* Test: Config editor initial render */
TEST(config_initial_render) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* ESC to cancel */
    ui_inject_keys("ESC");
    
    bool result = ui_edit_config(meta);
    
    /* Cancelled */
    ASSERT_FALSE(result);
    
    /* Check that config UI was rendered */
    int header_row = ui_test_backend_find_row("Configuration");
    ASSERT_TRUE(header_row >= 0);
    
    /* Should show config items */
    ASSERT_TRUE(ui_test_backend_find_row("hidden") >= 0);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Save configuration */
TEST(config_save_changes) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* ENTER to save */
    ui_inject_keys("ENTER");
    
    bool result = ui_edit_config(meta);
    
    /* Saved */
    ASSERT_TRUE(result);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Toggle boolean value */
TEST(config_toggle_boolean) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* SPACE to toggle first item (hidden), ENTER to save */
    ui_inject_keys("SPACE,ENTER");
    
    bool result = ui_edit_config(meta);
    
    ASSERT_TRUE(result);
    /* Hidden should now be true */
    ASSERT_TRUE(meta->cfg.default_show_hidden);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Navigate up/down */
TEST(config_navigation) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* DOWN, DOWN, SPACE (toggle fuzzy match), ENTER */
    ui_inject_keys("DOWN,DOWN,SPACE,ENTER");
    
    bool result = ui_edit_config(meta);
    
    ASSERT_TRUE(result);
    /* Fuzzy match should now be true (third item) */
    ASSERT_TRUE(meta->cfg.default_fuzzy_match);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: HOME/END navigation */
TEST(config_home_end) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* END (last item), SPACE (toggle), ENTER */
    ui_inject_keys("END,SPACE,ENTER");
    
    bool result = ui_edit_config(meta);
    
    /* Saved */
    ASSERT_TRUE(result);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Adjust numeric value with +/- */
TEST(config_adjust_numeric) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* DOWN, DOWN, DOWN (timeout), PLUS twice, ENTER */
    ui_inject_keys("DOWN,DOWN,DOWN,PLUS,PLUS,ENTER");
    
    bool result = ui_edit_config(meta);
    
    ASSERT_TRUE(result);
    /* Timeout should be increased by 2 */
    ASSERT_EQ_INT(302, meta->cfg.default_timeout);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Decrease numeric value */
TEST(config_decrease_numeric) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* DOWN, DOWN, DOWN (timeout), MINUS, ENTER */
    ui_inject_keys("DOWN,DOWN,DOWN,MINUS,ENTER");
    
    bool result = ui_edit_config(meta);
    
    ASSERT_TRUE(result);
    /* Timeout should be decreased by 1 */
    ASSERT_EQ_INT(299, meta->cfg.default_timeout);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Cancel with ESC */
TEST(config_cancel) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_mock_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Toggle something then cancel */
    ui_inject_keys("SPACE,ESC");
    
    bool result = ui_edit_config(meta);
    
    ASSERT_FALSE(result);
    /* Config should not be changed */
    ASSERT_FALSE(meta->cfg.default_show_hidden);
    
    free_mock_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

void suite_ui_config(void) {
    RUN_TEST(config_initial_render);
    RUN_TEST(config_save_changes);
    RUN_TEST(config_toggle_boolean);
    RUN_TEST(config_navigation);
    RUN_TEST(config_home_end);
    RUN_TEST(config_adjust_numeric);
    RUN_TEST(config_decrease_numeric);
    RUN_TEST(config_cancel);
}

TEST_MAIN(
    RUN_SUITE(ui_config);
)
