/* test_agent_cli_extended.c -- Comprehensive unit tests for agent CLI parsing
 *
 * Covers parse_agent_args (all subcommands, options, error paths)
 * and glob_match (edge cases, complex patterns).
 */

#include "test_framework.h"
#include "../src/cli.h"
#include <string.h>
#include <stdlib.h>

static void init_opts(NcdOptions *opts) {
    memset(opts, 0, sizeof(*opts));
}

/* ================================================================ parse_agent_args: query */

TEST(agent_parse_query_basic) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query", (char *)"downloads"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_QUERY, opts.agent_subcommand);
    ASSERT_TRUE(opts.has_search);
    ASSERT_EQ_STR("downloads", opts.search);
    ASSERT_EQ_INT(2, consumed);
    return 0;
}

TEST(agent_parse_query_json) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query", (char *)"foo", (char *)"--json"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    ASSERT_EQ_INT(3, consumed);
    return 0;
}

TEST(agent_parse_query_limit_equals) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query", (char *)"foo", (char *)"--limit=5"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_EQ_INT(5, opts.agent_limit);
    return 0;
}

TEST(agent_parse_query_limit_space) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query", (char *)"foo", (char *)"--limit", (char *)"10"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(5, argv, &consumed, &opts));
    ASSERT_EQ_INT(10, opts.agent_limit);
    return 0;
}

TEST(agent_parse_query_depth_sort) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query", (char *)"foo", (char *)"--depth-sort"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_depth_sort);
    return 0;
}

TEST(agent_parse_query_all_flag) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query", (char *)"foo", (char *)"--all"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.show_hidden);
    ASSERT_TRUE(opts.show_system);
    return 0;
}

TEST(agent_parse_query_multiple_options) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query", (char *)"foo", (char *)"--json", (char *)"--limit=3", (char *)"--depth-sort"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(6, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    ASSERT_EQ_INT(3, opts.agent_limit);
    ASSERT_TRUE(opts.agent_depth_sort);
    return 0;
}

TEST(agent_parse_query_missing_term_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"query"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(2, argv, &consumed, &opts));
    return 0;
}

/* ================================================================ parse_agent_args: ls */

TEST(agent_parse_ls_basic) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\Users"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_LS, opts.agent_subcommand);
    ASSERT_EQ_STR("C:\\Users", opts.search);
    ASSERT_EQ_INT(1, opts.agent_depth); /* default */
    return 0;
}

TEST(agent_parse_ls_json) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\", (char *)"--json"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    return 0;
}

TEST(agent_parse_ls_dirs_only) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\", (char *)"--dirs-only"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_dirs_only);
    ASSERT_FALSE(opts.agent_files_only);
    return 0;
}

TEST(agent_parse_ls_files_only) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\", (char *)"--files-only"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_files_only);
    ASSERT_FALSE(opts.agent_dirs_only);
    return 0;
}

TEST(agent_parse_ls_pattern_equals) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\", (char *)"--pattern=*.txt"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_EQ_STR("*.txt", opts.agent_pattern);
    return 0;
}

TEST(agent_parse_ls_pattern_space) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\", (char *)"--pattern", (char *)"*.log"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(5, argv, &consumed, &opts));
    ASSERT_EQ_STR("*.log", opts.agent_pattern);
    return 0;
}

TEST(agent_parse_ls_depth) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\", (char *)"--depth", (char *)"5"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(5, argv, &consumed, &opts));
    ASSERT_EQ_INT(5, opts.agent_depth);
    return 0;
}

TEST(agent_parse_ls_depth_invalid_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls", (char *)"C:\\", (char *)"--depth", (char *)"0"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(5, argv, &consumed, &opts));
    return 0;
}

TEST(agent_parse_ls_missing_path_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"ls"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(2, argv, &consumed, &opts));
    return 0;
}

/* ================================================================ parse_agent_args: tree */

TEST(agent_parse_tree_basic) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"tree", (char *)"C:\\Users"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_TREE, opts.agent_subcommand);
    ASSERT_EQ_INT(3, opts.agent_depth); /* default */
    return 0;
}

TEST(agent_parse_tree_json) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"tree", (char *)"C:\\", (char *)"--json"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    return 0;
}

TEST(agent_parse_tree_flat) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"tree", (char *)"C:\\", (char *)"--flat"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_flat);
    return 0;
}

TEST(agent_parse_tree_depth) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"tree", (char *)"C:\\", (char *)"--depth", (char *)"2"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(5, argv, &consumed, &opts));
    ASSERT_EQ_INT(2, opts.agent_depth);
    return 0;
}

TEST(agent_parse_tree_depth_invalid_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"tree", (char *)"C:\\", (char *)"--depth", (char *)"-1"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(5, argv, &consumed, &opts));
    return 0;
}

TEST(agent_parse_tree_missing_path_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"tree"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(2, argv, &consumed, &opts));
    return 0;
}

/* ================================================================ parse_agent_args: check */

TEST(agent_parse_check_path_only) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"check", (char *)"C:\\Windows"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_CHECK, opts.agent_subcommand);
    ASSERT_TRUE(opts.has_search);
    ASSERT_EQ_STR("C:\\Windows", opts.search);
    return 0;
}

TEST(agent_parse_check_db_age) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"check", (char *)"--db-age"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_check_db_age);
    return 0;
}

TEST(agent_parse_check_stats) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"check", (char *)"--stats"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_check_stats);
    return 0;
}

TEST(agent_parse_check_service_status) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"check", (char *)"--service-status"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_check_service_status);
    return 0;
}

TEST(agent_parse_check_json_and_flags) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"check", (char *)"--json", (char *)"--db-age", (char *)"--stats"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(5, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    ASSERT_TRUE(opts.agent_check_db_age);
    ASSERT_TRUE(opts.agent_check_stats);
    return 0;
}

TEST(agent_parse_check_path_and_flags) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"check", (char *)"--json", (char *)"C:\\Test"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    ASSERT_TRUE(opts.has_search);
    ASSERT_EQ_STR("C:\\Test", opts.search);
    return 0;
}

/* ================================================================ parse_agent_args: complete */

TEST(agent_parse_complete_basic) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"complete", (char *)"dow"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_COMPLETE, opts.agent_subcommand);
    ASSERT_EQ_STR("dow", opts.search);
    return 0;
}

TEST(agent_parse_complete_json) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"complete", (char *)"dow", (char *)"--json"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    return 0;
}

TEST(agent_parse_complete_limit) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"complete", (char *)"dow", (char *)"--limit", (char *)"5"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(5, argv, &consumed, &opts));
    ASSERT_EQ_INT(5, opts.agent_limit);
    return 0;
}

TEST(agent_parse_complete_missing_term_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"complete"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(2, argv, &consumed, &opts));
    return 0;
}

/* ================================================================ parse_agent_args: mkdir */

TEST(agent_parse_mkdir_basic) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"mkdir", (char *)"C:\\NewDir"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_MKDIR, opts.agent_subcommand);
    ASSERT_EQ_STR("C:\\NewDir", opts.search);
    return 0;
}

TEST(agent_parse_mkdir_json) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"mkdir", (char *)"C:\\NewDir", (char *)"--json"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    return 0;
}

TEST(agent_parse_mkdir_missing_path_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"mkdir"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(2, argv, &consumed, &opts));
    return 0;
}

/* ================================================================ parse_agent_args: mkdirs */

TEST(agent_parse_mkdirs_file) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"mkdirs", (char *)"--file", (char *)"tree.txt"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(4, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_MKDIRS, opts.agent_subcommand);
    ASSERT_EQ_STR("tree.txt", opts.agent_mkdirs_file);
    return 0;
}

TEST(agent_parse_mkdirs_json_flag) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"mkdirs", (char *)"--json", (char *)"--file", (char *)"tree.txt"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(5, argv, &consumed, &opts));
    ASSERT_TRUE(opts.agent_json);
    return 0;
}

TEST(agent_parse_mkdirs_inline) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"mkdirs", (char *)"[{\"name\":\"test\"}]"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_TRUE(opts.has_search);
    ASSERT_STR_CONTAINS(opts.search, "name");
    return 0;
}

/* ================================================================ parse_agent_args: quit & errors */

TEST(agent_parse_quit) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"quit"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(2, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_QUIT, opts.agent_subcommand);
    return 0;
}

TEST(agent_parse_empty_args_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(1, argv, &consumed, &opts));
    return 0;
}

TEST(agent_parse_unknown_subcommand_fails) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"foobar"};
    int consumed = 0;
    ASSERT_FALSE(parse_agent_args(2, argv, &consumed, &opts));
    return 0;
}

TEST(agent_parse_case_insensitive_subcommand) {
    NcdOptions opts; init_opts(&opts);
    char *argv[] = {(char *)"ncd", (char *)"QUERY", (char *)"test"};
    int consumed = 0;
    ASSERT_TRUE(parse_agent_args(3, argv, &consumed, &opts));
    ASSERT_EQ_INT(AGENT_SUB_QUERY, opts.agent_subcommand);
    return 0;
}

/* ================================================================ glob_match comprehensive */

TEST(glob_empty_pattern_matches_empty) {
    ASSERT_TRUE(glob_match("", ""));
    return 0;
}

TEST(glob_empty_pattern_no_match_non_empty) {
    ASSERT_FALSE(glob_match("", "a"));
    return 0;
}

TEST(glob_star_matches_empty) {
    ASSERT_TRUE(glob_match("*", ""));
    return 0;
}

TEST(glob_star_matches_anything) {
    ASSERT_TRUE(glob_match("*", "hello"));
    ASSERT_TRUE(glob_match("*", ""));
    ASSERT_TRUE(glob_match("*", "a very long string with stuff"));
    return 0;
}

TEST(glob_multiple_stars) {
    ASSERT_TRUE(glob_match("*-*", "a-b"));
    ASSERT_TRUE(glob_match("*-*", "hello-world"));
    ASSERT_TRUE(glob_match("*a*b*c*", "abc"));
    ASSERT_TRUE(glob_match("*a*b*c*", "xa yb zc w"));
    return 0;
}

TEST(glob_question_at_end) {
    ASSERT_TRUE(glob_match("hell?", "hello"));
    ASSERT_FALSE(glob_match("hell?", "hell"));
    return 0;
}

TEST(glob_question_at_start) {
    ASSERT_TRUE(glob_match("?est", "test"));
    ASSERT_FALSE(glob_match("?est", "est"));
    return 0;
}

TEST(glob_multiple_questions) {
    ASSERT_TRUE(glob_match("???", "abc"));
    ASSERT_FALSE(glob_match("???", "ab"));
    ASSERT_TRUE(glob_match("?e?s?", "tests"));
    return 0;
}

TEST(glob_mixed_star_and_question) {
    ASSERT_TRUE(glob_match("*.?", "a.b"));
    ASSERT_TRUE(glob_match("*.?", "filename.txt"));
    ASSERT_FALSE(glob_match("*.?", "file"));
    return 0;
}

TEST(glob_exact_no_wildcards) {
    ASSERT_TRUE(glob_match("exact", "exact"));
    ASSERT_FALSE(glob_match("exact", "exacts"));
    ASSERT_FALSE(glob_match("exact", "exac"));
    return 0;
}

TEST(glob_case_insensitive) {
    ASSERT_TRUE(glob_match("HELLO", "hello"));
    ASSERT_TRUE(glob_match("Hello", "HELLO"));
    ASSERT_TRUE(glob_match("MiXeD", "mixed"));
    return 0;
}

TEST(glob_special_chars_in_text) {
    ASSERT_TRUE(glob_match("*.*", "file.txt"));
    ASSERT_TRUE(glob_match("*-*", "my-file"));
    ASSERT_TRUE(glob_match("*_?", "test_1"));
    return 0;
}

TEST(glob_pattern_longer_than_text) {
    ASSERT_FALSE(glob_match("abcdef", "abc"));
    return 0;
}

TEST(glob_only_questions_exact_length) {
    ASSERT_TRUE(glob_match("?????", "hello"));
    ASSERT_FALSE(glob_match("?????", "hi"));
    ASSERT_FALSE(glob_match("?????", "hello world"));
    return 0;
}

TEST(glob_complex_realworld) {
    ASSERT_TRUE(glob_match("*.txt", "document.txt"));
    ASSERT_FALSE(glob_match("*.txt", "document.pdf"));
    ASSERT_TRUE(glob_match("test_*_v?.log", "test_001_v2.log"));
    ASSERT_FALSE(glob_match("test_*_v?.log", "test_001.log"));
    return 0;
}

/* ================================================================ Suites */

void suite_agent_cli_extended(void) {
    printf("\n=== parse_agent_args: query ===\n");
    RUN_TEST(agent_parse_query_basic);
    RUN_TEST(agent_parse_query_json);
    RUN_TEST(agent_parse_query_limit_equals);
    RUN_TEST(agent_parse_query_limit_space);
    RUN_TEST(agent_parse_query_depth_sort);
    RUN_TEST(agent_parse_query_all_flag);
    RUN_TEST(agent_parse_query_multiple_options);
    RUN_TEST(agent_parse_query_missing_term_fails);

    printf("\n=== parse_agent_args: ls ===\n");
    RUN_TEST(agent_parse_ls_basic);
    RUN_TEST(agent_parse_ls_json);
    RUN_TEST(agent_parse_ls_dirs_only);
    RUN_TEST(agent_parse_ls_files_only);
    RUN_TEST(agent_parse_ls_pattern_equals);
    RUN_TEST(agent_parse_ls_pattern_space);
    RUN_TEST(agent_parse_ls_depth);
    RUN_TEST(agent_parse_ls_depth_invalid_fails);
    RUN_TEST(agent_parse_ls_missing_path_fails);

    printf("\n=== parse_agent_args: tree ===\n");
    RUN_TEST(agent_parse_tree_basic);
    RUN_TEST(agent_parse_tree_json);
    RUN_TEST(agent_parse_tree_flat);
    RUN_TEST(agent_parse_tree_depth);
    RUN_TEST(agent_parse_tree_depth_invalid_fails);
    RUN_TEST(agent_parse_tree_missing_path_fails);

    printf("\n=== parse_agent_args: check ===\n");
    RUN_TEST(agent_parse_check_path_only);
    RUN_TEST(agent_parse_check_db_age);
    RUN_TEST(agent_parse_check_stats);
    RUN_TEST(agent_parse_check_service_status);
    RUN_TEST(agent_parse_check_json_and_flags);
    RUN_TEST(agent_parse_check_path_and_flags);

    printf("\n=== parse_agent_args: complete ===\n");
    RUN_TEST(agent_parse_complete_basic);
    RUN_TEST(agent_parse_complete_json);
    RUN_TEST(agent_parse_complete_limit);
    RUN_TEST(agent_parse_complete_missing_term_fails);

    printf("\n=== parse_agent_args: mkdir / mkdirs ===\n");
    RUN_TEST(agent_parse_mkdir_basic);
    RUN_TEST(agent_parse_mkdir_json);
    RUN_TEST(agent_parse_mkdir_missing_path_fails);
    RUN_TEST(agent_parse_mkdirs_file);
    RUN_TEST(agent_parse_mkdirs_json_flag);
    RUN_TEST(agent_parse_mkdirs_inline);

    printf("\n=== parse_agent_args: quit / errors ===\n");
    RUN_TEST(agent_parse_quit);
    RUN_TEST(agent_parse_empty_args_fails);
    RUN_TEST(agent_parse_unknown_subcommand_fails);
    RUN_TEST(agent_parse_case_insensitive_subcommand);

    printf("\n=== glob_match edge cases ===\n");
    RUN_TEST(glob_empty_pattern_matches_empty);
    RUN_TEST(glob_empty_pattern_no_match_non_empty);
    RUN_TEST(glob_star_matches_empty);
    RUN_TEST(glob_star_matches_anything);
    RUN_TEST(glob_multiple_stars);
    RUN_TEST(glob_question_at_end);
    RUN_TEST(glob_question_at_start);
    RUN_TEST(glob_multiple_questions);
    RUN_TEST(glob_mixed_star_and_question);
    RUN_TEST(glob_exact_no_wildcards);
    RUN_TEST(glob_case_insensitive);
    RUN_TEST(glob_special_chars_in_text);
    RUN_TEST(glob_pattern_longer_than_text);
    RUN_TEST(glob_only_questions_exact_length);
    RUN_TEST(glob_complex_realworld);
}

TEST_MAIN(
    RUN_SUITE(agent_cli_extended);
)
