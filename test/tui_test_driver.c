/*
 * tui_test_driver.c -- Standalone TUI test driver for batch-file testing
 *
 * This program creates fake match data and runs the TUI selector,
 * allowing the stdio TUI backend (enabled via NCD_TEST_MODE) to capture
 * output for comparison against expected output files.
 *
 * Build: Must be compiled WITHOUT /DNDEBUG (debug build) so the stdio
 *        backend is available.  Also needs /DNCD_TEST_BUILD for the
 *        test backend APIs used by some helpers.
 *
 * Usage:
 *   set NCD_TEST_MODE=80,25
 *   set NCD_UI_KEYS=@tui\input\test_selector_basic.keys
 *   tui_test_driver.exe selector > tui\actual\test_selector_basic.txt
 *
 * Test modes:
 *   selector    -- Test the match selector UI (3 items)
 *   selector5   -- Test the match selector UI (5 items, for navigation)
 *   selector20  -- Test the match selector UI (20 items, for scrolling)
 *   history     -- Test the history browser UI
 *   navigator   -- Test the directory navigator UI
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/ui.h"
#include "../src/matcher.h"
#include "../src/database.h"

static int run_selector(int match_count)
{
    NcdMatch matches[20];
    memset(matches, 0, sizeof(matches));

    /* Populate with test data */
    const char *paths[] = {
        "C:\\Projects\\Alpha",
        "C:\\Projects\\Beta",
        "C:\\Projects\\Gamma",
        "C:\\Documents\\Delta",
        "C:\\Documents\\Epsilon",
        "C:\\Windows\\System32",
        "C:\\Windows\\SysWOW64",
        "C:\\Users\\TestUser\\Desktop",
        "C:\\Users\\TestUser\\Downloads",
        "C:\\Users\\TestUser\\Documents",
        "C:\\Program Files\\App1",
        "C:\\Program Files\\App2",
        "C:\\Program Files\\App3",
        "C:\\Program Files (x86)\\OldApp1",
        "C:\\Program Files (x86)\\OldApp2",
        "C:\\Temp\\Build01",
        "C:\\Temp\\Build02",
        "C:\\Temp\\Build03",
        "C:\\Temp\\Build04",
        "C:\\Temp\\Build05"
    };

    if (match_count > 20) match_count = 20;

    for (int i = 0; i < match_count; i++) {
        strncpy(matches[i].full_path, paths[i], sizeof(matches[i].full_path) - 1);
    }

    int result = ui_select_match(matches, match_count, "test");

    /* Print result to stderr so it doesn't mix with TUI output on stdout */
    fprintf(stderr, "RESULT=%d\n", result);
    if (result >= 0 && result < match_count) {
        fprintf(stderr, "SELECTED=%s\n", matches[result].full_path);
    }

    return result;
}

static int run_history(void)
{
    NcdMatch matches[5];
    int count = 5;
    memset(matches, 0, sizeof(matches));

    strcpy(matches[0].full_path, "C:\\Recent\\Dir1");
    strcpy(matches[1].full_path, "C:\\Recent\\Dir2");
    strcpy(matches[2].full_path, "C:\\Recent\\Dir3");
    strcpy(matches[3].full_path, "C:\\Older\\Dir4");
    strcpy(matches[4].full_path, "C:\\Older\\Dir5");

    NcdMetadata meta;
    memset(&meta, 0, sizeof(meta));

    int result = ui_select_history(matches, &count, &meta, NULL, NULL);

    fprintf(stderr, "RESULT=%d\n", result);
    if (result >= 0 && result < count) {
        fprintf(stderr, "SELECTED=%s\n", matches[result].full_path);
    }

    return result;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <selector|selector5|selector20|history|navigator>\n",
                argv[0]);
        return 1;
    }

    const char *mode = argv[1];

    if (strcmp(mode, "selector") == 0) {
        run_selector(3);
    } else if (strcmp(mode, "selector5") == 0) {
        run_selector(5);
    } else if (strcmp(mode, "selector20") == 0) {
        run_selector(20);
    } else if (strcmp(mode, "history") == 0) {
        run_history();
    } else if (strcmp(mode, "navigator") == 0) {
        /* Navigator needs a real filesystem path */
        char out_path[260] = {0};
        bool ok = ui_navigate_directory("C:\\", out_path, sizeof(out_path));
        fprintf(stderr, "RESULT=%s\n", ok ? out_path : "(cancelled)");
    } else {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        return 1;
    }

    return 0;
}
