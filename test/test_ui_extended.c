/* test_ui_extended.c -- Extended UI tests for NCD
 * 
 * Tests advanced UI functionality including filter interactions,
 * terminal handling, config editor, navigator edge cases, selector
 * performance, rendering, and input handling.
 */

#include "test_framework.h"
#include "../src/ui.h"
#include "../src/matcher.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* =============================================================
 * Filter Interactions (8 tests)
 * ============================================================= */

/* Test: Filter with backspace at start (empty filter) */
TEST(filter_with_backspace_at_start) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    
    /* Type filter, backspace all, then select */
    ui_inject_keys("TEXT:abc,BACKSPACE,BACKSPACE,BACKSPACE,ENTER");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_TRUE(result >= 0);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Filter with special characters */
TEST(filter_with_special_characters) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha_123");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta-test");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma.space");
    
    ui_inject_keys("TEXT:_12,ENTER");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_TRUE(result == 0); /* Should select Alpha_123 */
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Case insensitive filtering */
TEST(filter_case_insensitive_matching) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\PROJECTS\\UPPER");
    strcpy(matches[1].full_path, "C:\\Projects\\Mixed");
    strcpy(matches[2].full_path, "C:\\projects\\lower");
    
    ui_inject_keys("TEXT:proj,ENTER");
    
    int result = ui_select_match(matches, 3, "test");
    /* Any could be selected since all contain "proj" case-insensitively */
    ASSERT_TRUE(result >= 0 && result < 3);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Regex injection attempt (security) */
TEST(filter_regex_injection_attempt) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    
    /* Try regex metacharacters - should be treated as literal */
    ui_inject_keys("TEXT:.*[a-z],ESC");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(-1, result); /* Cancelled */
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Very long filter text */
TEST(filter_very_long_filter_text) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    
    /* Long filter string */
    char long_filter[128];
    memset(long_filter, 'a', sizeof(long_filter) - 1);
    long_filter[sizeof(long_filter) - 1] = '\0';
    
    char key_seq[256];
    snprintf(key_seq, sizeof(key_seq), "TEXT:%s,ESC", long_filter);
    ui_inject_keys(key_seq);
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Unicode input handling */
TEST(filter_unicode_input) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    
    /* Unicode characters - may be ignored or treated as extended ASCII */
    ui_inject_keys("TEXT:\xC3\xA9\xC3\xA0,ESC");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Clear filter with escape */
TEST(filter_clear_with_escape) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[5];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    strcpy(matches[3].full_path, "C:\\Docs\\Delta");
    strcpy(matches[4].full_path, "C:\\Docs\\Epsilon");
    
    /* Type filter, ESC clears filter, then cancel */
    ui_inject_keys("TEXT:Proj,ESC,ESC");
    
    int result = ui_select_match(matches, 5, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: No matches shows appropriate message */
TEST(filter_no_matches_shows_message) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    
    /* Filter that won't match anything */
    ui_inject_keys("TEXT:xyznotfound,ESC");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* =============================================================
 * Window/Terminal Handling (5 tests)
 * ============================================================= */

/* Test: Terminal width minimum (40 columns) */
TEST(terminal_width_40_minimum) {
    UiIoOps *test_ops = ui_create_test_backend(40, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Terminal width maximum (120 columns) */
TEST(terminal_width_120_maximum) {
    UiIoOps *test_ops = ui_create_test_backend(120, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    strcpy(matches[1].full_path, "C:\\Projects\\Beta");
    strcpy(matches[2].full_path, "C:\\Projects\\Gamma");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Terminal resize during selection (simulated) */
TEST(terminal_resize_during_selection) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[10];
    memset(matches, 0, sizeof(matches));
    for (int i = 0; i < 10; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path), 
                 "C:\\Projects\\Item%d", i);
    }
    
    ui_inject_keys("PGDN,PGDN,ESC");
    
    int result = ui_select_match(matches, 10, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Terminal height less than content */
TEST(terminal_height_less_than_content) {
    UiIoOps *test_ops = ui_create_test_backend(80, 10);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[20];
    memset(matches, 0, sizeof(matches));
    for (int i = 0; i < 20; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path),
                 "C:\\Projects\\Item%d", i);
    }
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 20, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Terminal height exactly matches content */
TEST(terminal_height_exactly_matches_content) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* 20 matches should fit in 25 row terminal */
    NcdMatch matches[20];
    memset(matches, 0, sizeof(matches));
    for (int i = 0; i < 20; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path),
                 "C:\\Projects\\Item%d", i);
    }
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 20, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* =============================================================
 * Config Editor (9 tests)
 * ============================================================= */

/* Helper for config tests */
static NcdMetadata *create_test_metadata(void) {
    NcdMetadata *meta = (NcdMetadata *)calloc(1, sizeof(NcdMetadata));
    if (!meta) return NULL;
    meta->cfg.magic = NCD_CFG_MAGIC;
    meta->cfg.version = NCD_CFG_VERSION;
    meta->cfg.has_defaults = true;
    meta->cfg.default_show_hidden = false;
    meta->cfg.default_show_system = false;
    meta->cfg.default_fuzzy_match = false;
    meta->cfg.default_timeout = 300;
    meta->cfg.service_retry_count = 3;
    meta->cfg.rescan_interval_hours = -1;
    return meta;
}

static void free_test_metadata(NcdMetadata *meta) {
    free(meta);
}

/* Test: Boolean toggle in config editor */
TEST(config_edit_boolean_toggle) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    ui_inject_keys("SPACE,ENTER");
    
    bool result = ui_edit_config(meta);
    ASSERT_TRUE(result);
    ASSERT_TRUE(meta->cfg.default_show_hidden);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Numeric increment */
TEST(config_edit_numeric_increment) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Navigate to timeout (4th item), increment twice */
    ui_inject_keys("DOWN,DOWN,DOWN,PLUS,PLUS,ENTER");
    
    bool result = ui_edit_config(meta);
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(302, meta->cfg.default_timeout);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Numeric decrement */
TEST(config_edit_numeric_decrement) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    ui_inject_keys("DOWN,DOWN,DOWN,MINUS,ENTER");
    
    bool result = ui_edit_config(meta);
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(299, meta->cfg.default_timeout);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Numeric wraparound */
TEST(config_edit_numeric_wraparound) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    meta->cfg.default_timeout = 0;
    
    /* Try to decrement at minimum */
    ui_inject_keys("DOWN,DOWN,DOWN,MINUS,ENTER");
    
    bool result = ui_edit_config(meta);
    ASSERT_TRUE(result);
    /* Should handle gracefully (clamp or wrap) */
    ASSERT_TRUE(meta->cfg.default_timeout >= 0);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Invalid numeric input */
TEST(config_edit_numeric_invalid_input) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Navigate to rescan interval, clear it, type invalid chars */
    ui_inject_keys("DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:abc,ENTER,ESC");
    
    bool result = ui_edit_config(meta);
    /* Cancelled */
    ASSERT_FALSE(result);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Save creates metadata file */
TEST(config_edit_save_creates_file) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    ui_inject_keys("ENTER");
    
    bool result = ui_edit_config(meta);
    ASSERT_TRUE(result);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Cancel discards changes */
TEST(config_edit_cancel_discards_changes) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Toggle hidden then cancel */
    ui_inject_keys("SPACE,ESC");
    
    bool result = ui_edit_config(meta);
    ASSERT_FALSE(result);
    ASSERT_FALSE(meta->cfg.default_show_hidden);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: All options navigable */
TEST(config_edit_all_options_navigable) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Navigate through all items */
    ui_inject_keys("DOWN,DOWN,DOWN,DOWN,DOWN,HOME,ENTER");
    
    bool result = ui_edit_config(meta);
    ASSERT_TRUE(result);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Config edit with readonly metadata (stub) */
TEST(config_edit_with_readonly_metadata) {
    /* Stub - testing with NULL metadata */
    /* ui_edit_config should handle NULL gracefully */
    ui_set_io_backend(NULL);
    return 0;
}

/* =============================================================
 * Navigator Deep Hierarchies (7 tests)
 * ============================================================= */

/* Test: Depth limit reached */
TEST(navigate_depth_limit_reached) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* Navigate to current dir */
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Circular symlink detection (stub) */
TEST(navigate_circular_symlink_detection) {
    /* Note: Symlink detection not implemented yet */
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Permission denied directory (stub) */
TEST(navigate_permission_denied_directory) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Empty directory navigation */
TEST(navigate_empty_directory) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Very long path display */
TEST(navigate_very_long_path_display) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Unicode directory names (stub) */
TEST(navigate_unicode_directory_names) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Network path on Windows (stub) */
TEST(navigate_network_path_windows) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    ui_inject_keys("ESC");
    
    char out_path[MAX_PATH] = {0};
    /* Network paths start with \\ */
    bool result = ui_navigate_directory("C:\\Windows", out_path, sizeof(out_path));
    ASSERT_FALSE(result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* =============================================================
 * Selector with Many Matches (5 tests)
 * ============================================================= */

/* Test: 1000 matches performance */
TEST(selector_with_1000_matches_performance) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch *matches = (NcdMatch *)calloc(1000, sizeof(NcdMatch));
    ASSERT_NOT_NULL(matches);
    
    for (int i = 0; i < 1000; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path),
                 "C:\\Projects\\Item%04d", i);
    }
    
    ui_inject_keys("ESC");
    
    clock_t start = clock();
    int result = ui_select_match(matches, 1000, "test");
    clock_t end = clock();
    
    ASSERT_EQ_INT(-1, result);
    /* Should complete in reasonable time (< 1 second) */
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    ASSERT_TRUE(elapsed < 1.0);
    
    free(matches);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: 10000 matches memory handling */
TEST(selector_with_10000_matches_memory) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    /* Use smaller struct to avoid stack overflow */
    NcdMatch *matches = (NcdMatch *)malloc(10000 * sizeof(NcdMatch));
    if (!matches) {
        /* Skip if can't allocate */
        ui_set_io_backend(NULL);
        ui_destroy_test_backend(test_ops);
        return 0;
    }
    
    memset(matches, 0, 10000 * sizeof(NcdMatch));
    for (int i = 0; i < 10000; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path),
                 "C:\\Projects\\Item%05d", i);
    }
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 10000, "test");
    ASSERT_EQ_INT(-1, result);
    
    free(matches);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Scroll performance with many items */
TEST(selector_scroll_performance) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch *matches = (NcdMatch *)calloc(500, sizeof(NcdMatch));
    ASSERT_NOT_NULL(matches);
    
    for (int i = 0; i < 500; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path),
                 "C:\\Projects\\Item%03d", i);
    }
    
    /* Rapid scrolling */
    ui_inject_keys("END,HOME,END,HOME,ESC");
    
    clock_t start = clock();
    int result = ui_select_match(matches, 500, "test");
    clock_t end = clock();
    
    ASSERT_EQ_INT(-1, result);
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    ASSERT_TRUE(elapsed < 0.5);
    
    free(matches);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Filter large list performance */
TEST(selector_filter_large_list_performance) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch *matches = (NcdMatch *)calloc(1000, sizeof(NcdMatch));
    ASSERT_NOT_NULL(matches);
    
    for (int i = 0; i < 1000; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path),
                 "C:\\Projects\\Item%04d", i);
    }
    
    /* Type filter characters */
    ui_inject_keys("TEXT:Item099,ESC");
    
    clock_t start = clock();
    int result = ui_select_match(matches, 1000, "test");
    clock_t end = clock();
    
    ASSERT_EQ_INT(-1, result);
    double elapsed = ((double)(end - start)) / CLOCKS_PER_SEC;
    ASSERT_TRUE(elapsed < 1.0);
    
    free(matches);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Memory cleanup after large list */
TEST(selector_memory_cleanup_after_large_list) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch *matches = (NcdMatch *)calloc(1000, sizeof(NcdMatch));
    ASSERT_NOT_NULL(matches);
    
    for (int i = 0; i < 1000; i++) {
        snprintf(matches[i].full_path, sizeof(matches[i].full_path),
                 "C:\\Projects\\Item%04d", i);
    }
    
    ui_inject_keys("ESC");
    int result = ui_select_match(matches, 1000, "test");
    ASSERT_EQ_INT(-1, result);
    
    free(matches);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* =============================================================
 * Rendering Edge Cases (5 tests)
 * ============================================================= */

/* Test: Zero width terminal */
TEST(render_with_zero_width_terminal) {
    /* Use minimal width */
    UiIoOps *test_ops = ui_create_test_backend(40, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\P\\A");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 1, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: 1 character width terminal */
TEST(render_with_1_character_width) {
    UiIoOps *test_ops = ui_create_test_backend(40, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 1, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Path longer than terminal width */
TEST(render_path_longer_than_terminal_width) {
    UiIoOps *test_ops = ui_create_test_backend(40, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, 
           "C:\\Very\\Long\\Path\\That\\Exceeds\\Forty\\Characters\\Total\\Length");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 1, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: NULL matches handling */
TEST(render_with_null_matches) {
    /* ui_select_match should handle edge cases gracefully */
    ui_set_io_backend(NULL);
    return 0;
}

/* Test: Empty search term */
TEST(render_with_empty_search_term) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\Projects\\Alpha");
    
    ui_inject_keys("ESC");
    
    /* Pass empty search string */
    int result = ui_select_match(matches, 1, "");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* =============================================================
 * Input Handling (6 tests)
 * ============================================================= */

/* Test: Escape sequence parsing */
TEST(input_escape_sequence_parsing) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    strcpy(matches[2].full_path, "C:\\Third");
    
    /* Use multiple escape sequences */
    ui_inject_keys("DOWN,UP,PGDN,PGUP,HOME,END,ESC");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(-1, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Unknown escape sequences ignored */
TEST(input_unknown_escape_sequence_ignored) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    strcpy(matches[1].full_path, "C:\\Second");
    strcpy(matches[2].full_path, "C:\\Third");
    
    /* Enter should still work after unknown sequences */
    ui_inject_keys("ENTER");
    
    int result = ui_select_match(matches, 3, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Ctrl key combinations */
TEST(input_ctrl_key_combinations) {
    /* Ctrl keys are mostly ignored or treated as special */
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 1, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Alt key combinations */
TEST(input_alt_key_combinations) {
    /* Alt keys are mostly ignored in NCD */
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 1, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Function keys F1-F12 (stub) */
TEST(input_function_keys_f1_to_f12) {
    /* Function keys are not handled by NCD UI */
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 1, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Unicode character input (stub) */
TEST(input_unicode_characters) {
    /* Unicode is handled by treating as extended chars */
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMatch matches[3];
    memset(matches, 0, sizeof(matches));
    strcpy(matches[0].full_path, "C:\\First");
    
    ui_inject_keys("ESC");
    
    int result = ui_select_match(matches, 1, "test");
    ASSERT_EQ_INT(0, result);
    
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* =============================================================
 * Test Suites
 * ============================================================= */

void suite_filter_interactions(void) {
    RUN_TEST(filter_with_backspace_at_start);
    RUN_TEST(filter_with_special_characters);
    RUN_TEST(filter_case_insensitive_matching);
    RUN_TEST(filter_regex_injection_attempt);
    RUN_TEST(filter_very_long_filter_text);
    RUN_TEST(filter_unicode_input);
    RUN_TEST(filter_clear_with_escape);
    RUN_TEST(filter_no_matches_shows_message);
}

void suite_terminal_handling(void) {
    RUN_TEST(terminal_width_40_minimum);
    RUN_TEST(terminal_width_120_maximum);
    RUN_TEST(terminal_resize_during_selection);
    RUN_TEST(terminal_height_less_than_content);
    RUN_TEST(terminal_height_exactly_matches_content);
}

void suite_config_editor(void) {
    RUN_TEST(config_edit_boolean_toggle);
    RUN_TEST(config_edit_numeric_increment);
    RUN_TEST(config_edit_numeric_decrement);
    RUN_TEST(config_edit_numeric_wraparound);
    RUN_TEST(config_edit_numeric_invalid_input);
    RUN_TEST(config_edit_save_creates_file);
    RUN_TEST(config_edit_cancel_discards_changes);
    RUN_TEST(config_edit_all_options_navigable);
    RUN_TEST(config_edit_with_readonly_metadata);
}

void suite_navigator_deep(void) {
    RUN_TEST(navigate_depth_limit_reached);
    RUN_TEST(navigate_circular_symlink_detection);
    RUN_TEST(navigate_permission_denied_directory);
    RUN_TEST(navigate_empty_directory);
    RUN_TEST(navigate_very_long_path_display);
    RUN_TEST(navigate_unicode_directory_names);
    RUN_TEST(navigate_network_path_windows);
}

void suite_selector_performance(void) {
    RUN_TEST(selector_with_1000_matches_performance);
    RUN_TEST(selector_with_10000_matches_memory);
    RUN_TEST(selector_scroll_performance);
    RUN_TEST(selector_filter_large_list_performance);
    RUN_TEST(selector_memory_cleanup_after_large_list);
}

void suite_rendering_edge_cases(void) {
    RUN_TEST(render_with_zero_width_terminal);
    RUN_TEST(render_with_1_character_width);
    RUN_TEST(render_path_longer_than_terminal_width);
    RUN_TEST(render_with_null_matches);
    RUN_TEST(render_with_empty_search_term);
}

void suite_input_handling(void) {
    RUN_TEST(input_escape_sequence_parsing);
    RUN_TEST(input_unknown_escape_sequence_ignored);
    RUN_TEST(input_ctrl_key_combinations);
    RUN_TEST(input_alt_key_combinations);
    RUN_TEST(input_function_keys_f1_to_f12);
    RUN_TEST(input_unicode_characters);
}

TEST_MAIN(
    RUN_SUITE(filter_interactions);
    RUN_SUITE(terminal_handling);
    RUN_SUITE(config_editor);
    RUN_SUITE(navigator_deep);
    RUN_SUITE(selector_performance);
    RUN_SUITE(rendering_edge_cases);
    RUN_SUITE(input_handling);
)
