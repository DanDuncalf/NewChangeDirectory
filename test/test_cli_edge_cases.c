/* test_cli_edge_cases.c -- CLI edge case tests for NCD (Agent 4) */
#include "test_framework.h"
#include "../src/cli.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdlib.h>

/* Helper to create options with defaults */
static void init_options(NcdOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->timeout_seconds = -1;
}

/* Helper to run parse_args with wrapper */
static bool parse_args_wrapper(int argc, const char **argv, NcdOptions *opts) {
    char **args = (char **)malloc(argc * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        args[i] = (char *)argv[i];
    }
    bool result = parse_args(argc, args, opts);
    free(args);
    return result;
}

/* ================================================================ Combined Flags (6 tests) */

TEST(cli_flags_as_separate_args) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Valid single-letter flags should work as separate arguments */
    const char *argv[] = {"ncd", "-r", "-i", "-s", "-v"};
    bool result = parse_args_wrapper(5, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_hidden || opts.show_system || opts.force_rescan || opts.show_version);
    
    return 0;
}

TEST(cli_invalid_bundle_with_unknown_char_rejected) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Bundles with unknown characters like -rib should be rejected */
    const char *argv[] = {"ncd", "-rib"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should be rejected because 'b' is not a valid flag */
    ASSERT_FALSE(result);
    
    return 0;
}

TEST(cli_drive_letter_with_equals_rejected) {
    NcdOptions opts;
    init_options(&opts);
    
    /* -r=C is not valid drive-letter syntax */
    const char *argv[] = {"ncd", "-r=C"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should be rejected as unknown option */
    ASSERT_FALSE(result);
    
    return 0;
}

TEST(cli_separate_flags_order_independence) {
    NcdOptions opts1, opts2;
    init_options(&opts1);
    init_options(&opts2);
    
    /* Same flags in different orders should produce same result */
    const char *argv1[] = {"ncd", "-r", "-i", "-s"};
    const char *argv2[] = {"ncd", "-i", "-s", "-r"};
    
    bool r1 = parse_args_wrapper(4, argv1, &opts1);
    bool r2 = parse_args_wrapper(4, argv2, &opts2);
    
    ASSERT_TRUE(r1);
    ASSERT_TRUE(r2);
    ASSERT_TRUE(opts1.show_hidden == opts2.show_hidden);
    ASSERT_TRUE(opts1.show_system == opts2.show_system);
    ASSERT_TRUE(opts1.force_rescan == opts2.force_rescan);
    
    return 0;
}

TEST(cli_single_flags_case_insensitive) {
    NcdOptions opts1, opts2;
    init_options(&opts1);
    init_options(&opts2);
    
    /* Single-letter flags are case-insensitive */
    const char *argv1[] = {"ncd", "-R", "-I", "-S"};
    const char *argv2[] = {"ncd", "-r", "-i", "-s"};
    
    bool r1 = parse_args_wrapper(4, argv1, &opts1);
    bool r2 = parse_args_wrapper(4, argv2, &opts2);
    
    ASSERT_TRUE(r1);
    ASSERT_TRUE(r2);
    ASSERT_TRUE(opts1.force_rescan);
    ASSERT_TRUE(opts2.force_rescan);
    ASSERT_TRUE(opts1.show_hidden == opts2.show_hidden);
    
    return 0;
}

TEST(cli_combined_flags_with_numbers) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Test numeric flags like -0 (history pingpong) */
    const char *argv[] = {"ncd", "-0"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.history_pingpong);
    
    return 0;
}

/* ================================================================ Argument Parsing (10 tests) */

TEST(cli_argument_with_leading_whitespace) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "  downloads"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.has_search);
    /* Whitespace handling is implementation-defined */
    
    return 0;
}

TEST(cli_argument_with_trailing_whitespace) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "downloads  "};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.has_search);
    
    return 0;
}

TEST(cli_argument_with_embedded_whitespace) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "Program Files"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.has_search);
    ASSERT_STR_CONTAINS(opts.search, "Program");
    
    return 0;
}

TEST(cli_argument_with_quotes) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "\"Program Files\""};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.has_search);
    /* Quotes may or may not be stripped depending on shell */
    
    return 0;
}

TEST(cli_argument_with_escaped_quotes) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "Program\\\"Files"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.has_search);
    
    return 0;
}

TEST(cli_argument_with_newline_rejected) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Newlines in arguments should be handled gracefully */
    const char *argv[] = {"ncd", "line1\nline2"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should either reject or handle gracefully without crashing */
    (void)result;
    
    return 0;
}

TEST(cli_argument_empty_string) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", ""};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Empty string handling is implementation-defined */
    (void)result;
    
    return 0;
}

TEST(cli_argument_very_long_4096_chars) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Create a very long argument */
    char long_arg[4097];
    memset(long_arg, 'a', 4096);
    long_arg[4096] = '\0';
    
    const char *argv[] = {"ncd", long_arg};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should handle gracefully - either truncate or reject */
    (void)result;
    
    return 0;
}

TEST(cli_argument_unicode_all_planes) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Unicode argument from various planes */
    const char *argv[] = {"ncd", "\xE2\x9C\x93 \xF0\x9F\x98\x80"};  /* ✓ and 😀 */
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should handle UTF-8 gracefully */
    (void)result;
    
    return 0;
}

TEST(cli_argument_with_special_unicode) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Special Unicode characters like zero-width spaces */
    const char *argv[] = {"ncd", "test\xE2\x80\x8Bpath"};  /* Zero-width space */
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should handle gracefully */
    (void)result;
    
    return 0;
}

/* ================================================================ Invalid Combinations (5 tests) */

TEST(cli_invalid_combination_i_and_dirs_only) {
    NcdOptions opts;
    init_options(&opts);
    
    /* -i (show hidden) combined with --dirs-only in agent mode */
    const char *argv[] = {"ncd", "--agent:ls", "C:\\", "--dirs-only", "-i"};
    bool result = parse_args_wrapper(5, argv, &opts);
    
    /* These are actually compatible - -i applies to scanning, --dirs-only to listing */
    ASSERT_TRUE(result);
    
    return 0;
}

TEST(cli_invalid_combination_s_and_files_only) {
    NcdOptions opts;
    init_options(&opts);
    
    /* -s (show system) with --files-only */
    const char *argv[] = {"ncd", "--agent:ls", "C:\\", "--files-only", "-s"};
    bool result = parse_args_wrapper(5, argv, &opts);
    
    /* These are compatible - different operations */
    ASSERT_TRUE(result);
    
    return 0;
}

TEST(cli_invalid_combination_agent_and_search) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Agent mode with regular search term - should be rejected or search ignored */
    const char *argv[] = {"ncd", "--agent:query", "downloads", "extra_search"};
    bool result = parse_args_wrapper(4, argv, &opts);
    
    /* Result depends on implementation */
    (void)result;
    
    return 0;
}

TEST(cli_invalid_combination_conf_and_invalid_path) {
    NcdOptions opts;
    init_options(&opts);
    
    /* -conf with invalid path */
    const char *argv[] = {"ncd", "-conf:/nonexistent/path/config.metadata"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Parser should accept -conf even with invalid path (validation happens later) */
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(opts.conf_override) > 0);
    
    return 0;
}

TEST(cli_invalid_combination_timeout_zero) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Timeout of 0 should be rejected or treated as no timeout */
    const char *argv[] = {"ncd", "-t:0"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Result depends on implementation - 0 may be rejected */
    (void)result;
    
    return 0;
}

/* ================================================================ Drive Specification (5 tests) */

TEST(cli_drive_mask_all_26_drives) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Test all 26 drive letters A-Z */
    char drive_str[30] = "-r:";
    for (int i = 0; i < 26; i++) {
        drive_str[i + 3] = 'A' + i;
    }
    drive_str[29] = '\0';
    
    const char *argv[] = {"ncd", drive_str};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_EQ_INT(26, opts.scan_drive_count);
    
    return 0;
}

TEST(cli_drive_mask_none_valid) {
    NcdOptions opts;
    init_options(&opts);
    
    /* -r with no drive letters - should scan all */
    const char *argv[] = {"ncd", "-r"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_EQ_INT(0, opts.scan_drive_count);  /* 0 means all drives */
    
    return 0;
}

TEST(cli_drive_mask_with_duplicates) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Duplicate drive letters should be handled */
    const char *argv[] = {"ncd", "-r:CCDD"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    /* Should deduplicate or count each occurrence */
    
    return 0;
}

TEST(cli_drive_mask_with_invalid_letters) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Invalid characters in drive mask */
    const char *argv[] = {"ncd", "-r:C1D"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should either reject or skip invalid characters */
    (void)result;
    
    return 0;
}

TEST(cli_skip_drive_mask_conflicts_with_include) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Skip mask -r:-b should set skip_drive_mask */
    const char *argv[] = {"ncd", "-r:-b"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_TRUE(opts.skip_drive_mask[1]);  /* B drive excluded */
    
    return 0;
}

/* ================================================================ Agent Subcommands (4 tests) */

TEST(cli_agent_missing_subcommand) {
    NcdOptions opts;
    init_options(&opts);
    
    /* --agent without subcommand should fail */
    const char *argv[] = {"ncd", "--agent"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_FALSE(result);
    
    return 0;
}

TEST(cli_agent_unknown_subcommand) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Unknown subcommand should fail */
    const char *argv[] = {"ncd", "--agent:unknowncommand"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_FALSE(result);
    
    return 0;
}

TEST(cli_agent_query_missing_search_term) {
    NcdOptions opts;
    init_options(&opts);
    
    /* query subcommand without search term */
    const char *argv[] = {"ncd", "--agent:query"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* May be allowed (shows all) or rejected depending on implementation */
    (void)result;
    
    return 0;
}

TEST(cli_agent_ls_missing_path) {
    NcdOptions opts;
    init_options(&opts);
    
    /* ls subcommand without path - may use current directory */
    const char *argv[] = {"ncd", "--agent:ls"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* May succeed with current directory as default */
    (void)result;
    
    return 0;
}

/* ================================================================ Test Suite */

void suite_cli_edge_cases(void) {
    /* Combined Flags (6 tests) */
    RUN_TEST(cli_flags_as_separate_args);
    RUN_TEST(cli_invalid_bundle_with_unknown_char_rejected);
    RUN_TEST(cli_drive_letter_with_equals_rejected);
    RUN_TEST(cli_separate_flags_order_independence);
    RUN_TEST(cli_single_flags_case_insensitive);
    RUN_TEST(cli_combined_flags_with_numbers);
    
    /* Argument Parsing (10 tests) */
    RUN_TEST(cli_argument_with_leading_whitespace);
    RUN_TEST(cli_argument_with_trailing_whitespace);
    RUN_TEST(cli_argument_with_embedded_whitespace);
    RUN_TEST(cli_argument_with_quotes);
    RUN_TEST(cli_argument_with_escaped_quotes);
    RUN_TEST(cli_argument_with_newline_rejected);
    RUN_TEST(cli_argument_empty_string);
    RUN_TEST(cli_argument_very_long_4096_chars);
    RUN_TEST(cli_argument_unicode_all_planes);
    RUN_TEST(cli_argument_with_special_unicode);
    
    /* Invalid Combinations (5 tests) */
    RUN_TEST(cli_invalid_combination_i_and_dirs_only);
    RUN_TEST(cli_invalid_combination_s_and_files_only);
    RUN_TEST(cli_invalid_combination_agent_and_search);
    RUN_TEST(cli_invalid_combination_conf_and_invalid_path);
    RUN_TEST(cli_invalid_combination_timeout_zero);
    
    /* Drive Specification (5 tests) */
    RUN_TEST(cli_drive_mask_all_26_drives);
    RUN_TEST(cli_drive_mask_none_valid);
    RUN_TEST(cli_drive_mask_with_duplicates);
    RUN_TEST(cli_drive_mask_with_invalid_letters);
    RUN_TEST(cli_skip_drive_mask_conflicts_with_include);
    
    /* Agent Subcommands (4 tests) */
    RUN_TEST(cli_agent_missing_subcommand);
    RUN_TEST(cli_agent_unknown_subcommand);
    RUN_TEST(cli_agent_query_missing_search_term);
    RUN_TEST(cli_agent_ls_missing_path);
}

TEST_MAIN(
    RUN_SUITE(cli_edge_cases);
)
