/* test_agent_query_tree.c -- Integration tests for agent query and tree commands
 *
 * Creates temp databases, invokes NewChangeDirectory.exe, and verifies output.
 */

#include "test_framework.h"
#include "agent_test_common.h"
#include "../src/database.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#endif

/* ================================================================ Helpers */

static const char *test_chain_query(void) {
#if NCD_PLATFORM_WINDOWS
    return "scott\\doc";
#else
    return "scott/doc";
#endif
}

static const char *test_users_path(void) {
#if !NCD_PLATFORM_WINDOWS
    static char path[NCD_MAX_PATH];
    char root[NCD_MAX_PATH];
    agent_build_test_drive_root(root, sizeof(root));
    snprintf(path, sizeof(path), "%s/Users", root);
    return path;
#else
    #if NCD_PLATFORM_WINDOWS
    return "C:\\Users";
#endif
#endif
}

static const char *test_missing_tree_path(void) {
#if !NCD_PLATFORM_WINDOWS
    static char path[NCD_MAX_PATH];
    char root[NCD_MAX_PATH];
    agent_build_test_drive_root(root, sizeof(root));
    snprintf(path, sizeof(path), "%s/Nonexistent", root);
    return path;
#else
    #if NCD_PLATFORM_WINDOWS
    return "C:\\Nonexistent";
#endif
#endif
}

/* ================================================================ Query Tests */

TEST(agent_query_basic_plain) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_SETUP_DB(base, ncd_dir, "qb");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());

    char out[4096] = {0};
    int status = agent_run(base, "query Downloads", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "Downloads");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_query_basic_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_SETUP_DB(base, ncd_dir, "qj");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());

    char out[4096] = {0};
    int status = agent_run(base, "query Downloads --json", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"query\"");
    ASSERT_STR_CONTAINS(out, "\"results\"");
    ASSERT_STR_CONTAINS(out, "Downloads");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_query_no_match_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_SETUP_DB(base, ncd_dir, "qn");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());

    char out[4096] = {0};
    int status = agent_run(base, "query nonexistent_xyz_123 --json", out, sizeof(out), db_path);
    ASSERT_TRUE(status != 0 || strstr(out, "\"results\":[]") != NULL);
    ASSERT_STR_CONTAINS(out, "\"results\":[]");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_query_limit) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_SETUP_DB(base, ncd_dir, "ql");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());

    char out[4096] = {0};
    int status = agent_run(base, "query s --json --limit=2", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_query_chain_search) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    char args[128];
    AGENT_SETUP_DB(base, ncd_dir, "qc");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());

    char out[4096] = {0};
    snprintf(args, sizeof(args), "query \"%s\" --json", test_chain_query());
    int status = agent_run(base, args, out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "Documents");
    ASSERT_STR_CONTAINS(out, "scott");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_query_missing_db) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "qm");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);

    char out[4096] = {0};
    int status = agent_run(base, "query foo --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0 || strstr(out, "error") != NULL || strstr(out, "no database") != NULL);

    agent_rm_rf(base);
    return 0;
}

TEST(agent_query_case_insensitive) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_SETUP_DB(base, ncd_dir, "qci");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());

    char out1[4096] = {0};
    char out2[4096] = {0};
    agent_run(base, "query downloads --json", out1, sizeof(out1), db_path);
    agent_run(base, "query DOWNLOADS --json", out2, sizeof(out2), db_path);
    ASSERT_STR_CONTAINS(out1, "\"v\":1");
    ASSERT_STR_CONTAINS(out2, "\"v\":1");

    agent_rm_rf(base);
    return 0;
}

/* ================================================================ Tree Tests */

TEST(agent_tree_basic_plain) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    char args[128];
    AGENT_SETUP_DB(base, ncd_dir, "tp");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());

    char out[4096] = {0};
    snprintf(args, sizeof(args), "tree \"%s\"", test_users_path());
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "scott");
    ASSERT_STR_CONTAINS(out, "admin");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_tree_basic_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    char args[160];
    AGENT_SETUP_DB(base, ncd_dir, "tj");

    char out[4096] = {0};
    snprintf(args, sizeof(args), "tree \"%s\" --json", test_users_path());
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"tree\"");
    ASSERT_STR_CONTAINS(out, "\"n\"");
    ASSERT_STR_CONTAINS(out, "\"d\"");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_tree_flat) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    char args[160];
    AGENT_SETUP_DB(base, ncd_dir, "tf");

    char out[4096] = {0};
    snprintf(args, sizeof(args), "tree \"%s\" --flat", test_users_path());
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_TRUE(strstr(out, "scott") != NULL);

    agent_rm_rf(base);
    return 0;
}

TEST(agent_tree_flat_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    char args[192];
    AGENT_SETUP_DB(base, ncd_dir, "tfj");

    char out[4096] = {0};
    snprintf(args, sizeof(args), "tree \"%s\" --json --flat", test_users_path());
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"tree\"");

    agent_rm_rf(base);
    return 0;
}

TEST(agent_tree_depth_limit) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    char args[160];
    AGENT_SETUP_DB(base, ncd_dir, "td");

    char out[4096] = {0};
    snprintf(args, sizeof(args), "tree \"%s\" --depth 1", test_users_path());
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "scott");
    ASSERT_STR_CONTAINS(out, "admin");
    ASSERT_TRUE(strstr(out, "Downloads") == NULL);

    agent_rm_rf(base);
    return 0;
}

TEST(agent_tree_not_found) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    char args[160];
    AGENT_SETUP_DB(base, ncd_dir, "tn");

    char out[4096] = {0};
    snprintf(args, sizeof(args), "tree \"%s\" --json", test_missing_tree_path());
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "not found") != NULL || strstr(out, "error") != NULL);

    agent_rm_rf(base);
    return 0;
}

TEST(agent_tree_missing_db) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "tm");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);

    char out[4096] = {0};
    int status = agent_run(base, "tree C:\\Users --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "error") != NULL || strstr(out, "no database") != NULL);

    agent_rm_rf(base);
    return 0;
}

/* ================================================================ Suites */

void suite_agent_query(void) {
    printf("\n=== Agent Query Integration ===\n");
    RUN_TEST(agent_query_basic_plain);
    RUN_TEST(agent_query_basic_json);
    RUN_TEST(agent_query_no_match_json);
    RUN_TEST(agent_query_limit);
    RUN_TEST(agent_query_chain_search);
    RUN_TEST(agent_query_missing_db);
    RUN_TEST(agent_query_case_insensitive);
}

void suite_agent_tree(void) {
    printf("\n=== Agent Tree Integration ===\n");
    RUN_TEST(agent_tree_basic_plain);
    RUN_TEST(agent_tree_basic_json);
    RUN_TEST(agent_tree_flat);
    RUN_TEST(agent_tree_flat_json);
    RUN_TEST(agent_tree_depth_limit);
    RUN_TEST(agent_tree_not_found);
    RUN_TEST(agent_tree_missing_db);
}

TEST_MAIN(
    agent_kill_any_service();
    RUN_SUITE(agent_query);
    RUN_SUITE(agent_tree);
)
