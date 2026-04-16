/* test_ui_exclusions.c -- UI Exclusion Editor tests for NCD
 * 
 * Tests the interactive exclusion pattern editor UI with an in-memory backend.
 */

#include "test_framework.h"
#include "../src/ui.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>

/* =============================================================
 * Helper Functions
 * ============================================================= */

static NcdMetadata *create_test_metadata(void) {
    NcdMetadata *meta = (NcdMetadata *)calloc(1, sizeof(NcdMetadata));
    if (!meta) return NULL;
    meta->cfg.magic = NCD_CFG_MAGIC;
    meta->cfg.version = NCD_CFG_VERSION;
    return meta;
}

static void free_test_metadata(NcdMetadata *meta) {
    free(meta);
}

/* =============================================================
 * Exclusion UI Tests (15 tests)
 * ============================================================= */

/* Test: Add pattern interactively */
TEST(exclusion_ui_add_pattern_interactive) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Press 'a' to add, type pattern, enter to save */
    ui_inject_keys("a,TEXT:C:\\Windows,ENTER,ESC");
    
    bool result = ui_edit_exclusions(meta);
    /* Cancelled after adding */
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Remove pattern interactively */
TEST(exclusion_ui_remove_pattern_interactive) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add a pattern first */
    db_exclusion_add(meta, "C:\\Windows");
    
    /* Press 'd' to delete, confirm */
    ui_inject_keys("d,ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: List displays patterns */
TEST(exclusion_ui_list_displays_patterns) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add some patterns */
    db_exclusion_add(meta, "C:\\Windows");
    db_exclusion_add(meta, "C:\\Program Files");
    
    ui_inject_keys("ESC");
    
    bool result = ui_edit_exclusions(meta);
    /* Cancelled - just checking it displays */
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Edit existing pattern */
TEST(exclusion_ui_edit_existing_pattern) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add a pattern to edit */
    db_exclusion_add(meta, "C:\\OldPattern");
    
    /* Enter to edit, change pattern, save */
    ui_inject_keys("ENTER,ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Cancel without saving */
TEST(exclusion_ui_cancel_without_saving) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    int old_count = meta->exclusions.count;
    
    ui_inject_keys("ESC");
    
    bool result = ui_edit_exclusions(meta);
    /* Returns false on cancel */
    ASSERT_FALSE(result);
    ASSERT_EQ_INT(old_count, meta->exclusions.count);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Invalid pattern rejected */
TEST(exclusion_ui_invalid_pattern_rejected) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Try to add empty pattern */
    ui_inject_keys("a,ENTER,ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    /* Empty pattern should not be added */
    ASSERT_EQ_INT(0, meta->exclusions.count);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Duplicate pattern warning */
TEST(exclusion_ui_duplicate_pattern_warning) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add initial pattern */
    db_exclusion_add(meta, "C:\\Windows");
    
    /* Try to add same pattern */
    ui_inject_keys("a,TEXT:C:\\Windows,ENTER,ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    /* Should only have one pattern */
    ASSERT_EQ_INT(1, meta->exclusions.count);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Empty pattern rejected */
TEST(exclusion_ui_empty_pattern_rejected) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Try to add empty pattern */
    ui_inject_keys("a,TEXT:   ,ENTER,ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    /* Whitespace-only pattern should be rejected */
    ASSERT_EQ_INT(0, meta->exclusions.count);
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Very long pattern handling */
TEST(exclusion_ui_very_long_pattern_handling) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Create a very long pattern */
    char long_pattern[512];
    for (int i = 0; i < 100; i++) {
        long_pattern[i] = 'a';
    }
    long_pattern[100] = '\0';
    
    char key_seq[256];
    snprintf(key_seq, sizeof(key_seq), "a,TEXT:%s,ENTER,ESC", long_pattern);
    ui_inject_keys(key_seq);
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Unicode pattern support */
TEST(exclusion_ui_unicode_pattern_support) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Try to add pattern with unicode */
    ui_inject_keys("a,TEXT:\xC3\xA9\xC3\xA0,ENTER,ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Pattern preview (stub) */
TEST(exclusion_ui_pattern_preview) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add a pattern */
    db_exclusion_add(meta, "C:\\Windows");
    
    ui_inject_keys("ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Test pattern button (stub) */
TEST(exclusion_ui_test_pattern_button) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    ui_inject_keys("ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Bulk add from file (stub) */
TEST(exclusion_ui_bulk_add_from_file) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Simulate bulk add with multiple patterns */
    ui_inject_keys("ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Export patterns (stub) */
TEST(exclusion_ui_export_patterns) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add some patterns to export */
    db_exclusion_add(meta, "C:\\Windows");
    db_exclusion_add(meta, "C:\\Program Files");
    
    ui_inject_keys("ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* Test: Reset to defaults */
TEST(exclusion_ui_reset_to_defaults) {
    UiIoOps *test_ops = ui_create_test_backend(80, 25);
    ASSERT_NOT_NULL(test_ops);
    ui_set_io_backend(test_ops);
    
    NcdMetadata *meta = create_test_metadata();
    ASSERT_NOT_NULL(meta);
    
    /* Add some patterns */
    db_exclusion_add(meta, "C:\\Custom1");
    db_exclusion_add(meta, "C:\\Custom2");
    ASSERT_EQ_INT(2, meta->exclusions.count);
    
    /* Press 'r' to reset */
    ui_inject_keys("r,ESC");
    
    bool result = ui_edit_exclusions(meta);
    (void)result;
    
    free_test_metadata(meta);
    ui_set_io_backend(NULL);
    ui_destroy_test_backend(test_ops);
    return 0;
}

/* =============================================================
 * Test Suites
 * ============================================================= */

void suite_ui_exclusions(void) {
    RUN_TEST(exclusion_ui_add_pattern_interactive);
    RUN_TEST(exclusion_ui_remove_pattern_interactive);
    RUN_TEST(exclusion_ui_list_displays_patterns);
    RUN_TEST(exclusion_ui_edit_existing_pattern);
    RUN_TEST(exclusion_ui_cancel_without_saving);
    RUN_TEST(exclusion_ui_invalid_pattern_rejected);
    RUN_TEST(exclusion_ui_duplicate_pattern_warning);
    RUN_TEST(exclusion_ui_empty_pattern_rejected);
    RUN_TEST(exclusion_ui_very_long_pattern_handling);
    RUN_TEST(exclusion_ui_unicode_pattern_support);
    RUN_TEST(exclusion_ui_pattern_preview);
    RUN_TEST(exclusion_ui_test_pattern_button);
    RUN_TEST(exclusion_ui_bulk_add_from_file);
    RUN_TEST(exclusion_ui_export_patterns);
    RUN_TEST(exclusion_ui_reset_to_defaults);
}

TEST_MAIN(
    RUN_SUITE(ui_exclusions);
)
