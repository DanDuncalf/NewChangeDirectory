/* test_tui_integration.c -- TUI integration tests for NCD
 * 
 * Tests the actual NCD executable with injected key inputs.
 * Uses NCD_UI_KEYS_FILE environment variable to drive the TUI.
 */

#include "test_framework.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#define NCD_EXE "../NewChangeDirectory"
#define TEST_DIR ".tui_test_tmp"
#define CONFIG_FILE TEST_DIR "/ncd.metadata"
#define KEYS_FILE TEST_DIR "/input_keys.txt"
#define MAX_CMD 4096

/* Setup test environment */
static int setup_test_env(void) {
    /* Create test directory */
    mkdir(TEST_DIR, 0755);
    return 0;
}

/* Cleanup test environment */
static void cleanup_test_env(void) {
    /* Remove test files */
    unlink(CONFIG_FILE);
    unlink(KEYS_FILE);
    rmdir(TEST_DIR);
}

/* Write key sequence to file */
static int write_keys(const char *keys) {
    FILE *f = fopen(KEYS_FILE, "w");
    if (!f) return -1;
    fprintf(f, "%s", keys);
    fclose(f);
    return 0;
}

/* Run ncd with given arguments and injected keys */
static int run_ncd_with_keys(const char *keys, const char *args) {
    char cmd[MAX_CMD];
    
    if (write_keys(keys) != 0) return -1;
    
    /* Build command with environment variables */
    snprintf(cmd, sizeof(cmd),
        "NCD_UI_KEYS_FILE=%s NCD_UI_KEYS_STRICT=1 %s -conf %s %s 2>/dev/null",
        KEYS_FILE, NCD_EXE, CONFIG_FILE, args);
    
    int rc = system(cmd);
    return WEXITSTATUS(rc);
}

/* Test: Config editor - cancel without changes */
TEST(tui_config_cancel) {
    setup_test_env();
    
    /* ESC to cancel */
    int rc = run_ncd_with_keys("ESC", "/c");
    
    /* Should return 0 (cancel is not an error) */
    ASSERT_EQ_INT(0, rc);
    
    cleanup_test_env();
    return 0;
}

/* Test: Config editor - toggle hidden and save */
TEST(tui_config_toggle_hidden) {
    setup_test_env();
    
    /* SPACE to toggle first item (hidden), ENTER to save */
    int rc = run_ncd_with_keys("SPACE,ENTER", "/c");
    
    /* Should return 0 (success) */
    ASSERT_EQ_INT(0, rc);
    
    /* Verify config was saved by checking if file exists and has content */
    struct stat st;
    ASSERT_EQ_INT(0, stat(CONFIG_FILE, &st));
    ASSERT_TRUE(st.st_size > 0);
    
    cleanup_test_env();
    return 0;
}

/* Test: Config editor - navigate and toggle fuzzy match */
TEST(tui_config_navigate_toggle) {
    setup_test_env();
    
    /* DOWN, DOWN (to fuzzy match), SPACE (toggle), ENTER (save) */
    int rc = run_ncd_with_keys("DOWN,DOWN,SPACE,ENTER", "/c");
    
    /* Should succeed */
    ASSERT_EQ_INT(0, rc);
    
    /* Config file should exist */
    struct stat st;
    ASSERT_EQ_INT(0, stat(CONFIG_FILE, &st));
    
    cleanup_test_env();
    return 0;
}

/* Test: Config editor - adjust numeric value */
TEST(tui_config_adjust_timeout) {
    setup_test_env();
    
    /* DOWN, DOWN, DOWN (to timeout), PLUS, PLUS, ENTER */
    int rc = run_ncd_with_keys("DOWN,DOWN,DOWN,PLUS,PLUS,ENTER", "/c");
    
    ASSERT_EQ_INT(0, rc);
    
    struct stat st;
    ASSERT_EQ_INT(0, stat(CONFIG_FILE, &st));
    
    cleanup_test_env();
    return 0;
}

/* Test: Config editor - use END key to jump to last item */
TEST(tui_config_end_key) {
    setup_test_env();
    
    /* END (last item), SPACE (toggle), ENTER (save) */
    int rc = run_ncd_with_keys("END,SPACE,ENTER", "/c");
    
    ASSERT_EQ_INT(0, rc);
    
    cleanup_test_env();
    return 0;
}

/* Test: Config editor - HOME key */
TEST(tui_config_home_key) {
    setup_test_env();
    
    /* DOWN, DOWN, DOWN, HOME (back to first), ENTER */
    int rc = run_ncd_with_keys("DOWN,DOWN,DOWN,HOME,ENTER", "/c");
    
    ASSERT_EQ_INT(0, rc);
    
    cleanup_test_env();
    return 0;
}

/* Test: History browser - cancel */
TEST(tui_history_cancel) {
    setup_test_env();
    
    /* ESC to cancel */
    int rc = run_ncd_with_keys("ESC", "/h");
    
    ASSERT_EQ_INT(0, rc);
    
    cleanup_test_env();
    return 0;
}

/* Test: Selector with filter input */
TEST(tui_selector_with_filter) {
    setup_test_env();
    
    /* This requires a database with entries - create a minimal one first */
    char cmd[MAX_CMD];
    
    /* Create a minimal database for testing */
    snprintf(cmd, sizeof(cmd),
        "mkdir -p %s/testdb && "
        "echo 'test' > %s/testdb/file.txt",
        TEST_DIR, TEST_DIR);
    system(cmd);
    
    /* Run ncd with filter text, then ESC to cancel */
    /* First we need to scan the test directory */
    snprintf(cmd, sizeof(cmd),
        "%s -conf %s /r.%s/testdb >/dev/null 2>&1",
        NCD_EXE, CONFIG_FILE, TEST_DIR);
    system(cmd);
    
    /* Now test the selector with filter */
    (void)run_ncd_with_keys("TEXT:te,ESC,ESC", "test");
    
    /* Cleanup */
    snprintf(cmd, sizeof(cmd), "rm -rf %s/testdb", TEST_DIR);
    system(cmd);
    
    cleanup_test_env();
    return 0;
}

/* Test: Multiple boolean toggles */
TEST(tui_config_multiple_toggles) {
    setup_test_env();
    
    /* Toggle hidden, down, toggle system, down, toggle fuzzy, ENTER */
    int rc = run_ncd_with_keys("SPACE,DOWN,SPACE,DOWN,SPACE,ENTER", "/c");
    
    ASSERT_EQ_INT(0, rc);
    
    cleanup_test_env();
    return 0;
}

/* Test: Config editor - type numeric value directly */
TEST(tui_config_type_numeric) {
    setup_test_env();
    
    /* Navigate to timeout, type '500', ENTER to confirm, then ENTER to save */
    int rc = run_ncd_with_keys("DOWN,DOWN,DOWN,TEXT:500,ENTER,ENTER", "/c");
    
    ASSERT_EQ_INT(0, rc);
    
    cleanup_test_env();
    return 0;
}

void suite_tui_integration(void) {
    RUN_TEST(tui_config_cancel);
    RUN_TEST(tui_config_toggle_hidden);
    RUN_TEST(tui_config_navigate_toggle);
    RUN_TEST(tui_config_adjust_timeout);
    RUN_TEST(tui_config_end_key);
    RUN_TEST(tui_config_home_key);
    RUN_TEST(tui_history_cancel);
    RUN_TEST(tui_selector_with_filter);
    RUN_TEST(tui_config_multiple_toggles);
    RUN_TEST(tui_config_type_numeric);
}

TEST_MAIN(
    RUN_SUITE(tui_integration);
)
