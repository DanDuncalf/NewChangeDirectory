/* test_key_injection.c -- Key injection unit tests for NCD
 * 
 * Tests the key injection functionality without requiring a TTY.
 * Verifies that key parsing, queueing, and retrieval work correctly.
 */

#include "test_framework.h"
#include "../src/ui.h"
#include <string.h>
#include <stdlib.h>

/* Test: Parse simple key names */
TEST(inject_simple_keys) {
    ui_clear_injected_keys();
    
    /* Inject simple keys */
    int rc = ui_inject_keys("ENTER,ESC,SPACE");
    ASSERT_EQ_INT(0, rc);
    
    /* Queue should not be empty */
    ASSERT_FALSE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Parse navigation keys */
TEST(inject_navigation_keys) {
    ui_clear_injected_keys();
    
    int rc = ui_inject_keys("UP,DOWN,LEFT,RIGHT,HOME,END,PGUP,PGDN");
    ASSERT_EQ_INT(0, rc);
    ASSERT_FALSE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Parse text input */
TEST(inject_text_input) {
    ui_clear_injected_keys();
    
    /* Inject text - types 'hello' */
    int rc = ui_inject_keys("TEXT:hello");
    ASSERT_EQ_INT(0, rc);
    ASSERT_FALSE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Parse mixed keys and text */
TEST(inject_mixed_sequence) {
    ui_clear_injected_keys();
    
    /* Navigate, type filter, press enter */
    int rc = ui_inject_keys("DOWN,TEXT:project,ENTER");
    ASSERT_EQ_INT(0, rc);
    ASSERT_FALSE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Clear injected keys */
TEST(inject_clear_keys) {
    ui_clear_injected_keys();
    
    /* Add some keys */
    ui_inject_keys("ENTER,ESC,SPACE");
    ASSERT_FALSE(ui_injected_keys_empty());
    
    /* Clear them */
    ui_clear_injected_keys();
    ASSERT_TRUE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Empty injection */
TEST(inject_empty) {
    ui_clear_injected_keys();
    
    int rc = ui_inject_keys("");
    ASSERT_EQ_INT(0, rc);
    ASSERT_TRUE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Complex config editor sequence */
TEST(inject_config_sequence) {
    ui_clear_injected_keys();
    
    /* SPACE (toggle), DOWN, DOWN, SPACE (toggle fuzzy), ENTER (save) */
    int rc = ui_inject_keys("SPACE,DOWN,DOWN,SPACE,ENTER");
    ASSERT_EQ_INT(0, rc);
    ASSERT_FALSE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Numeric input sequence */
TEST(inject_numeric_input) {
    ui_clear_injected_keys();
    
    /* Navigate to timeout, type 500, confirm, save */
    int rc = ui_inject_keys("DOWN,DOWN,DOWN,TEXT:500,ENTER,ENTER");
    ASSERT_EQ_INT(0, rc);
    ASSERT_FALSE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Case insensitive key names */
TEST(inject_case_insensitive) {
    ui_clear_injected_keys();
    
    int rc = ui_inject_keys("enter,Esc,space,Up,down");
    ASSERT_EQ_INT(0, rc);
    ASSERT_FALSE(ui_injected_keys_empty());
    
    return 0;
}

/* Test: Key file loading */
TEST(inject_from_file) {
    ui_clear_injected_keys();
    
    /* Create a temp key file */
    const char *tmpfile = ".test_keys.txt";
    FILE *f = fopen(tmpfile, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "SPACE,ENTER");
    fclose(f);
    
    /* Load from file */
    int rc = ui_inject_keys_from_file(tmpfile);
    ASSERT_EQ_INT(0, rc);
    ASSERT_FALSE(ui_injected_keys_empty());
    
    /* Cleanup */
    unlink(tmpfile);
    
    return 0;
}

/* Test: Invalid key file */
TEST(inject_from_bad_file) {
    ui_clear_injected_keys();
    
    /* Try to load from non-existent file */
    int rc = ui_inject_keys_from_file("/nonexistent/file/keys.txt");
    ASSERT_EQ_INT(-1, rc);
    
    return 0;
}

void suite_key_injection(void) {
    RUN_TEST(inject_simple_keys);
    RUN_TEST(inject_navigation_keys);
    RUN_TEST(inject_text_input);
    RUN_TEST(inject_mixed_sequence);
    RUN_TEST(inject_clear_keys);
    RUN_TEST(inject_empty);
    RUN_TEST(inject_config_sequence);
    RUN_TEST(inject_numeric_input);
    RUN_TEST(inject_case_insensitive);
    RUN_TEST(inject_from_file);
    RUN_TEST(inject_from_bad_file);
}

TEST_MAIN(
    RUN_SUITE(key_injection);
)
