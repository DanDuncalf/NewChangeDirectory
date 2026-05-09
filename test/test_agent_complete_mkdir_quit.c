/* test_agent_complete_mkdir_quit.c -- Integration tests for agent complete, mkdir, quit */

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

/* Smaller test DB for this file - just Users\scott\{Downloads,Documents} + Windows\System32 */
static bool create_test_db_cmq(const char *ncd_dir, char drive_letter) {
    char db_path[NCD_MAX_PATH];
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, drive_letter);
    NcdDatabase *db = db_create();
    if (!db) return false;
    db->last_scan = time(NULL);
    DriveData *drv = db_add_drive(db, drive_letter);
#if !NCD_PLATFORM_WINDOWS
    {
        char drive_root[NCD_MAX_PATH];
        agent_build_test_drive_root(drive_root, sizeof(drive_root));
        platform_strncpy_s(drv->label, sizeof(drv->label), drive_root);
    }
#endif
    int users = db_add_dir(drv, "Users", -1, false, false);
    int scott = db_add_dir(drv, "scott", users, false, false);
    db_add_dir(drv, "Downloads", scott, false, false);
    db_add_dir(drv, "Documents", scott, false, false);
    {
        int windows = db_add_dir(drv, "Windows", -1, false, true);
        db_add_dir(drv, "System32", windows, false, true);
    }
    bool ok = db_save_binary_single(db, 0, db_path);
    db_free(db);
    return ok;
}

#define AGENT_CMQ_SETUP(base, ncd_dir, suffix) do { \
    agent_get_temp_dir(base, sizeof(base), suffix); \
    agent_build_ncd_dir(ncd_dir, sizeof(ncd_dir), base); \
    agent_rm_rf(base); \
    AGENT_MKDIR(base, 0755); \
    AGENT_MKDIR(ncd_dir, 0755); \
    ASSERT_TRUE(create_test_db_cmq(ncd_dir, agent_test_drive_letter())); \
} while (0)

TEST(agent_complete_basic) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_CMQ_SETUP(base, ncd_dir, "cb");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());
    char out[4096] = {0};
    int status = agent_run(base, "complete Do", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_TRUE(strstr(out, "Downloads") != NULL || strstr(out, "Documents") != NULL);
    agent_rm_rf(base);
    return 0;
}

TEST(agent_complete_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_CMQ_SETUP(base, ncd_dir, "cj");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());
    char out[4096] = {0};
    int status = agent_run(base, "complete Sys --json", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    agent_rm_rf(base);
    return 0;
}

TEST(agent_complete_limit) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_CMQ_SETUP(base, ncd_dir, "cl");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());
    char out[4096] = {0};
    int status = agent_run(base, "complete s --json --limit 2", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    agent_rm_rf(base);
    return 0;
}

TEST(agent_complete_no_match) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_CMQ_SETUP(base, ncd_dir, "cn");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());
    char out[4096] = {0};
    int status = agent_run(base, "complete xyz123nonexistent", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    agent_rm_rf(base);
    return 0;
}

TEST(agent_mkdir_creates_directory) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "mc");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\newdir_test", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/newdir_test", base);
#endif
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mkdir \"%s\"", test_dir);
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_TRUE(agent_dir_exists(test_dir));
    agent_rm_rf(base);
    return 0;
}

TEST(agent_mkdir_existing_directory) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "me");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\existing_dir", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/existing_dir", base);
#endif
    AGENT_MKDIR(test_dir, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mkdir \"%s\" --json", test_dir);
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    agent_rm_rf(base);
    return 0;
}

TEST(agent_mkdir_nested_path) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "mn");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\a\\b\\c", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/a/b/c", base);
#endif
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mkdir \"%s\"", test_dir);
    int status = agent_run(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_TRUE(agent_dir_exists(test_dir));
    agent_rm_rf(base);
    return 0;
}

TEST(agent_mkdir_invalid_path) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "mi");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char out[4096] = {0};
    int status = agent_run(base, "mkdir \"\" --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "error") != NULL);
    agent_rm_rf(base);
    return 0;
}

TEST(agent_quit_plain) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "qp");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char out[4096] = {0};
    int status = agent_run(base, "quit", out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    agent_rm_rf(base);
    return 0;
}

TEST(agent_quit_json) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "qj");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char out[4096] = {0};
    int status = agent_run(base, "quit --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    agent_rm_rf(base);
    return 0;
}

TEST(agent_unknown_subcommand) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "us");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char out[4096] = {0};
    int status = agent_run(base, "foobar", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "unknown") != NULL || strstr(out, "error") != NULL);
    agent_rm_rf(base);
    return 0;
}

TEST(agent_no_subcommand) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "ns");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char out[4096] = {0};
    int status = agent_run(base, "", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "requires") != NULL || strstr(out, "error") != NULL);
    agent_rm_rf(base);
    return 0;
}

void suite_agent_complete(void) {
    printf("\n=== Agent Complete Integration ===\n");
    RUN_TEST(agent_complete_basic);
    RUN_TEST(agent_complete_json);
    RUN_TEST(agent_complete_limit);
    RUN_TEST(agent_complete_no_match);
}

void suite_agent_mkdir(void) {
    printf("\n=== Agent Mkdir Integration ===\n");
    RUN_TEST(agent_mkdir_creates_directory);
    RUN_TEST(agent_mkdir_existing_directory);
    RUN_TEST(agent_mkdir_nested_path);
    RUN_TEST(agent_mkdir_invalid_path);
}

void suite_agent_quit(void) {
    printf("\n=== Agent Quit Integration ===\n");
    RUN_TEST(agent_quit_plain);
    RUN_TEST(agent_quit_json);
}

void suite_agent_edge_cases(void) {
    printf("\n=== Agent Edge Cases ===\n");
    RUN_TEST(agent_unknown_subcommand);
    RUN_TEST(agent_no_subcommand);
}

TEST_MAIN(
    agent_kill_any_service();
    RUN_SUITE(agent_complete);
    RUN_SUITE(agent_mkdir);
    RUN_SUITE(agent_quit);
    RUN_SUITE(agent_edge_cases);
)
