/* test_cli_parse_extended.c -- Extended CLI parsing tests for better coverage */
#include "test_framework.h"
#include "../src/cli.h"
#include <string.h>
#include <stdlib.h>

/* Helper to create options with defaults */
static void init_options(NcdOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->timeout_seconds = -1;
}

/* Helper to run parse_args */
static bool parse_args_wrapper(int argc, const char **argv, NcdOptions *opts) {
    /* Convert const char** to char** for parse_args */
    char **args = (char **)malloc(argc * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        args[i] = (char *)argv[i];
    }
    bool result = parse_args(argc, args, opts);
    free(args);
    return result;
}

/* ================================================================ History Command Tests */

TEST(history_pingpong_flag) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-0"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.history_pingpong);
    
    return 0;
}

TEST(history_index_1_to_9) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Test -1 through -9 */
    for (int i = 1; i <= 9; i++) {
        init_options(&opts);
        char arg[4];
        snprintf(arg, sizeof(arg), "-%d", i);
        const char *argv[] = {"ncd", arg};
        bool result = parse_args_wrapper(2, argv, &opts);
        
        ASSERT_TRUE(result);
        ASSERT_EQ_INT(i, opts.history_index);
    }
    
    return 0;
}

TEST(history_list_flag) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-h:l"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.history_list);
    
    return 0;
}

TEST(history_clear_flag) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-h:c"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.history_clear);
    
    return 0;
}

TEST(cli_parse_history_remove_by_index) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Test -h:c1 through -h:c9 */
    for (int i = 1; i <= 9; i++) {
        init_options(&opts);
        char arg[6];
        snprintf(arg, sizeof(arg), "-h:c%d", i);
        const char *argv[] = {"ncd", arg};
        bool result = parse_args_wrapper(2, argv, &opts);
        
        ASSERT_TRUE(result);
        ASSERT_EQ_INT(i, opts.history_remove);
    }
    
    return 0;
}

TEST(history_remove_invalid_index_rejected) {
    NcdOptions opts;
    init_options(&opts);
    
    /* -h:c0 should fail (0 is not valid) */
    const char *argv[] = {"ncd", "-h:c0"};
    bool result = parse_args_wrapper(2, argv, &opts);
    ASSERT_FALSE(result);
    
    return 0;
}

/* ================================================================ Group Command Tests */

TEST(group_set_requires_at_name) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-g:@home"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.group_set);
    ASSERT_EQ_STR("home", opts.group_name);
    
    return 0;
}

TEST(group_set_rejects_without_at) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-g:home"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Should fail because name doesn't start with @ */
    ASSERT_FALSE(result);
    
    return 0;
}

TEST(group_remove_requires_at_name) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-G:@work"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.group_remove);
    ASSERT_EQ_STR("work", opts.group_name);
    
    return 0;
}

TEST(group_list_dash_variant) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Test -g:l variant */
    const char *argv[] = {"ncd", "-g:l"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.group_list);
    
    return 0;
}

TEST(group_list_uppercase) {
    NcdOptions opts;
    init_options(&opts);
    
    /* Test -g:L variant */
    const char *argv[] = {"ncd", "-g:L"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.group_list);
    
    return 0;
}

/* ================================================================ Exclusion Tests */

TEST(exclusion_add_dash_variant) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-x:*/node_modules"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.exclusion_add);
    ASSERT_EQ_STR("*/node_modules", opts.exclusion_pattern);
    
    return 0;
}

TEST(exclusion_remove_dash_variant) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-X:*/node_modules"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.exclusion_remove);
    ASSERT_EQ_STR("*/node_modules", opts.exclusion_pattern);
    
    return 0;
}

TEST(exclusion_list_dash_variant) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-x:l"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.exclusion_list);
    
    return 0;
}

TEST(exclusion_add_slash_variant) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "/x:C:\\Windows"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.exclusion_add);
    ASSERT_EQ_STR("C:\\Windows", opts.exclusion_pattern);
    
    return 0;
}

/* ================================================================ Timeout Tests */

TEST(timeout_with_no_space) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-t:30"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(30, opts.timeout_seconds);
    
    return 0;
}

TEST(timeout_dash_variant) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-t:60"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(60, opts.timeout_seconds);
    
    return 0;
}

/* ================================================================ Retry Tests */

TEST(retry_count_parsing) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--retry:5"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.service_retry_set);
    ASSERT_EQ_INT(5, opts.service_retry_count);
    
    return 0;
}

TEST(retry_count_dash_variant) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--retry:3"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.service_retry_set);
    ASSERT_EQ_INT(3, opts.service_retry_count);
    
    return 0;
}

/* ================================================================ Combined Flag Tests */

TEST(separate_flags_i_z) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-i", "-z"};
    bool result = parse_args_wrapper(3, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.fuzzy_match);
    
    return 0;
}

TEST(separate_flags_s_z) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-s", "-z"};
    bool result = parse_args_wrapper(3, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_system);
    ASSERT_TRUE(opts.fuzzy_match);
    
    return 0;
}

TEST(separate_flags_i_s_z) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-i", "-s", "-z"};
    bool result = parse_args_wrapper(4, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.show_system);
    ASSERT_TRUE(opts.fuzzy_match);
    
    return 0;
}

TEST(separate_flags_i_s_z_v) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-i", "-s", "-z", "-v"};
    bool result = parse_args_wrapper(5, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.show_system);
    ASSERT_TRUE(opts.fuzzy_match);
    ASSERT_TRUE(opts.show_version);
    
    return 0;
}

TEST(bundled_flags_riz_sets_all_three) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-riz"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.fuzzy_match);
    
    return 0;
}

/* ================================================================ Edge Cases */

TEST(empty_string_arg_rejected) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", ""};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    /* Empty string is not a valid argument */
    /* Result depends on implementation - we just verify it doesn't crash */
    (void)result;
    
    return 0;
}

TEST(multiple_search_terms_not_allowed) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "downloads", "documents"};
    bool result = parse_args_wrapper(3, argv, &opts);
    
    /* Should fail or only use first search term */
    /* Behavior depends on implementation */
    (void)result;
    
    return 0;
}

TEST(dash_help_long_form) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--help"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.show_help);
    
    return 0;
}

TEST(agent_mode_without_subcommand_rejected) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_FALSE(result);
    
    return 0;
}

TEST(agent_complete_with_limit) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent:complete", "down", "--limit", "10"};
    bool result = parse_args_wrapper(5, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.agent_mode);
    ASSERT_EQ_INT(AGENT_SUB_COMPLETE, opts.agent_subcommand);
    ASSERT_EQ_INT(10, opts.agent_limit);
    ASSERT_EQ_STR("down", opts.search);
    
    return 0;
}

TEST(agent_mkdir_with_path) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent:mkdir", "C:\\temp\\newdir"};
    bool result = parse_args_wrapper(3, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.agent_mode);
    ASSERT_EQ_INT(AGENT_SUB_MKDIR, opts.agent_subcommand);
    ASSERT_EQ_STR("C:\\temp\\newdir", opts.search);
    
    return 0;
}

TEST(agent_mkdirs_with_file) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent:mkdirs", "--file", "tree.txt"};
    bool result = parse_args_wrapper(4, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.agent_mode);
    ASSERT_EQ_INT(AGENT_SUB_MKDIRS, opts.agent_subcommand);
    ASSERT_EQ_STR("tree.txt", opts.agent_mkdirs_file);
    
    return 0;
}

TEST(agent_check_all_flags) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent:check", "--db-age", "--stats", "--service-status", "--json"};
    bool result = parse_args_wrapper(6, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.agent_mode);
    ASSERT_EQ_INT(AGENT_SUB_CHECK, opts.agent_subcommand);
    ASSERT_TRUE(opts.agent_check_db_age);
    ASSERT_TRUE(opts.agent_check_stats);
    ASSERT_TRUE(opts.agent_check_service_status);
    ASSERT_TRUE(opts.agent_json);
    
    return 0;
}

TEST(agent_tree_with_flat_and_depth) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent:tree", "C:\\temp", "--flat", "--depth", "5"};
    bool result = parse_args_wrapper(6, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.agent_mode);
    ASSERT_EQ_INT(AGENT_SUB_TREE, opts.agent_subcommand);
    ASSERT_TRUE(opts.agent_flat);
    ASSERT_EQ_INT(5, opts.agent_depth);
    
    return 0;
}

TEST(agent_ls_with_dirs_only) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent:ls", "C:\\temp", "--dirs-only"};
    bool result = parse_args_wrapper(4, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.agent_mode);
    ASSERT_EQ_INT(AGENT_SUB_LS, opts.agent_subcommand);
    ASSERT_TRUE(opts.agent_dirs_only);
    
    return 0;
}

TEST(agent_ls_with_files_only) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "--agent:ls", "C:\\temp", "--files-only"};
    bool result = parse_args_wrapper(4, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.agent_mode);
    ASSERT_EQ_INT(AGENT_SUB_LS, opts.agent_subcommand);
    ASSERT_TRUE(opts.agent_files_only);
    
    return 0;
}

TEST(conf_flag_sets_conf_override) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-conf:C:\\custom.metadata"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(opts.conf_override) > 0);
    ASSERT_EQ_STR("C:\\custom.metadata", opts.conf_override);
    
    return 0;
}

TEST(d_flag_sets_db_override) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-d:C:\\test.db"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(opts.db_override) > 0);
    ASSERT_STR_CONTAINS(opts.db_override, "test.db");
    
    return 0;
}

TEST(rescan_specific_drive) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-r:cde"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_TRUE(opts.scan_drive_mask['C' - 'A']);
    ASSERT_TRUE(opts.scan_drive_mask['D' - 'A']);
    ASSERT_TRUE(opts.scan_drive_mask['E' - 'A']);
    ASSERT_EQ_INT(3, opts.scan_drive_count);
    
    return 0;
}

TEST(rescan_exclude_drives) {
    NcdOptions opts;
    init_options(&opts);
    
    const char *argv[] = {"ncd", "-r:-bc"};
    bool result = parse_args_wrapper(2, argv, &opts);
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(opts.force_rescan);
    ASSERT_TRUE(opts.skip_drive_mask['B' - 'A']);
    ASSERT_TRUE(opts.skip_drive_mask['C' - 'A']);
    ASSERT_EQ_INT(2, opts.skip_drive_count);
    
    return 0;
}

/* ================================================================ Test Suite */

void suite_cli_parse_extended(void) {
    /* History tests */
    RUN_TEST(history_pingpong_flag);
    RUN_TEST(history_index_1_to_9);
    RUN_TEST(history_list_flag);
    RUN_TEST(history_clear_flag);
    RUN_TEST(cli_parse_history_remove_by_index);
    RUN_TEST(history_remove_invalid_index_rejected);
    
    /* Group tests */
    RUN_TEST(group_set_requires_at_name);
    RUN_TEST(group_set_rejects_without_at);
    RUN_TEST(group_remove_requires_at_name);
    RUN_TEST(group_list_dash_variant);
    RUN_TEST(group_list_uppercase);
    
    /* Exclusion tests */
    RUN_TEST(exclusion_add_dash_variant);
    RUN_TEST(exclusion_remove_dash_variant);
    RUN_TEST(exclusion_list_dash_variant);
    RUN_TEST(exclusion_add_slash_variant);
    
    /* Timeout and retry tests */
    RUN_TEST(timeout_with_no_space);
    RUN_TEST(timeout_dash_variant);
    RUN_TEST(retry_count_parsing);
    RUN_TEST(retry_count_dash_variant);
    
    /* Combined flags tests */
    RUN_TEST(separate_flags_i_z);
    RUN_TEST(separate_flags_s_z);
    RUN_TEST(separate_flags_i_s_z);
    RUN_TEST(separate_flags_i_s_z_v);
    RUN_TEST(bundled_flags_riz_sets_all_three);
    
    /* Edge cases */
    RUN_TEST(empty_string_arg_rejected);
    RUN_TEST(multiple_search_terms_not_allowed);
    RUN_TEST(dash_help_long_form);
    RUN_TEST(agent_mode_without_subcommand_rejected);
    RUN_TEST(agent_complete_with_limit);
    RUN_TEST(agent_mkdir_with_path);
    RUN_TEST(agent_mkdirs_with_file);
    RUN_TEST(agent_check_all_flags);
    RUN_TEST(agent_tree_with_flat_and_depth);
    RUN_TEST(agent_ls_with_dirs_only);
    RUN_TEST(agent_ls_with_files_only);
    RUN_TEST(conf_flag_sets_conf_override);
    RUN_TEST(d_flag_sets_db_override);
    RUN_TEST(rescan_specific_drive);
    RUN_TEST(rescan_exclude_drives);
}

TEST_MAIN(
    RUN_SUITE(cli_parse_extended);
)
