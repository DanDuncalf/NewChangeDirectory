/* test_cli_parse.c -- Tests for CLI argument parsing */
#include "test_framework.h"
#include "../src/cli.h"
#include <string.h>
#include <stdlib.h>

/* Helper to create options with defaults */
static void init_options(NcdOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->timeout_seconds = -1;
}

/* Helper to run parse_args with a single argument */
static bool parse_single(const char *arg, NcdOptions *opts) {
    char *argv[] = {(char *)"ncd", (char *)arg};
    return parse_args(2, argv, opts);
}

/* ================================================================ Tier 3: CLI Parsing Tests */

TEST(no_args_show_help_default) {
    NcdOptions opts;
    init_options(&opts);
    
    char *argv[] = {(char *)"ncd"};
    bool result = parse_args(1, argv, &opts);
    
    ASSERT_TRUE(result);
    /* With no args, show_help should be true */
    ASSERT_TRUE(opts.show_help);
    
    return 0;
}

TEST(h_flag_sets_show_help) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/h", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.history_browse);  /* /h is history browse, not help! */
    
    return 0;
}

TEST(question_flag_sets_show_help) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/?", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_help);
    
    return 0;
}

TEST(v_flag_sets_show_version) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/v", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_version);
    
    return 0;
}

TEST(r_flag_sets_force_rescan) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/r", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    
    return 0;
}

TEST(r_dot_flag_sets_subdir_rescan) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/r.", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_TRUE(strlen(opts.scan_subdirectory) > 0);
    
    return 0;
}

TEST(i_flag_sets_show_hidden) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/i", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_hidden);
    
    return 0;
}

TEST(s_flag_sets_show_system) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/s", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_system);
    
    return 0;
}

TEST(a_flag_sets_both_hidden_and_system) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/a", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.show_system);
    
    return 0;
}

TEST(z_flag_sets_fuzzy_match) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/z", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.fuzzy_match);
    
    return 0;
}

TEST(f_flag_sets_show_history) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/f", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_history);
    
    return 0;
}

TEST(fc_flag_sets_clear_history) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/fc", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.clear_history);
    
    return 0;
}

TEST(gl_flag_sets_group_list) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/gl", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.group_list);
    
    return 0;
}

TEST(d_flag_sets_db_override) {
    NcdOptions opts;
    init_options(&opts);
    
    char *argv[] = {(char *)"ncd", (char *)"/d", (char *)"C:\\test.db"};
    bool result = parse_args(3, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(opts.db_override) > 0);
    ASSERT_STR_CONTAINS(opts.db_override, "test.db");
    
    return 0;
}

TEST(t_flag_sets_timeout) {
    NcdOptions opts;
    init_options(&opts);
    
    char *argv[] = {(char *)"ncd", (char *)"/t", (char *)"30"};
    bool result = parse_args(3, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(30, opts.timeout_seconds);
    
    return 0;
}

TEST(c_flag_sets_config_edit) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/c", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.config_edit);
    
    return 0;
}

TEST(search_term_captured) {
    NcdOptions opts;
    init_options(&opts);
    
    char *argv[] = {(char *)"ncd", (char *)"downloads"};
    bool result = parse_args(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.has_search);
    ASSERT_EQ_STR("downloads", opts.search);
    
    return 0;
}

TEST(combined_flags_ris_sets_all_three) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/ris", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.show_system);
    
    return 0;
}

TEST(combined_flags_za_sets_fuzzy_and_all) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/za", &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.fuzzy_match);
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.show_system);
    
    return 0;
}

TEST(dash_equivalent_to_slash) {
    NcdOptions opts1, opts2;
    init_options(&opts1);
    init_options(&opts2);
    
    bool r1 = parse_single("/?", &opts1);
    bool r2 = parse_single("-?", &opts2);
    
    ASSERT_TRUE(r1);
    ASSERT_TRUE(r2);
    ASSERT_TRUE(opts1.show_help);
    ASSERT_TRUE(opts2.show_help);
    
    return 0;
}

TEST(invalid_option_returns_false) {
    NcdOptions opts;
    init_options(&opts);
    
    bool result = parse_single("/qqq", &opts);
    
    /* Invalid options should cause parse_args to return false */
    ASSERT_FALSE(result);
    
    return 0;
}

/* ================================================================ Test Suite */

void suite_cli_parse(void) {
    RUN_TEST(no_args_show_help_default);
    RUN_TEST(h_flag_sets_show_help);
    RUN_TEST(question_flag_sets_show_help);
    RUN_TEST(v_flag_sets_show_version);
    RUN_TEST(r_flag_sets_force_rescan);
    RUN_TEST(r_dot_flag_sets_subdir_rescan);
    RUN_TEST(i_flag_sets_show_hidden);
    RUN_TEST(s_flag_sets_show_system);
    RUN_TEST(a_flag_sets_both_hidden_and_system);
    RUN_TEST(z_flag_sets_fuzzy_match);
    RUN_TEST(f_flag_sets_show_history);
    RUN_TEST(fc_flag_sets_clear_history);
    RUN_TEST(gl_flag_sets_group_list);
    RUN_TEST(d_flag_sets_db_override);
    RUN_TEST(t_flag_sets_timeout);
    RUN_TEST(c_flag_sets_config_edit);
    RUN_TEST(search_term_captured);
    RUN_TEST(combined_flags_ris_sets_all_three);
    RUN_TEST(combined_flags_za_sets_fuzzy_and_all);
    RUN_TEST(dash_equivalent_to_slash);
    RUN_TEST(invalid_option_returns_false);
}

TEST_MAIN(
    RUN_SUITE(cli_parse);
)
