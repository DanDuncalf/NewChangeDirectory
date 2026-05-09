/* test_agent_ls_check.c -- Integration tests for agent ls and check commands */

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

static bool create_test_db_small(const char *ncd_dir, char drive_letter) {
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
    db_add_dir(drv, "Users", -1, false, false);
    bool ok = db_save_binary_single(db, 0, db_path);
    db_free(db);
    return ok;
}

#define AGENT_LS_SETUP(base, ncd_dir, suffix) do { \
    agent_get_temp_dir(base, sizeof(base), suffix); \
    agent_build_ncd_dir(ncd_dir, sizeof(ncd_dir), base); \
    agent_rm_rf(base); \
    AGENT_MKDIR(base, 0755); \
    AGENT_MKDIR(ncd_dir, 0755); \
    ASSERT_TRUE(create_test_db_small(ncd_dir, agent_test_drive_letter())); \
} while (0)

TEST(agent_ls_basic_plain) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lsp");
    agent_rm_rf(fs_base);
    agent_make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\"", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "subdir");
    ASSERT_STR_CONTAINS(out, "file1.txt");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_basic_json) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lsj");
    agent_rm_rf(fs_base);
    agent_make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --json", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"entries\"");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_dirs_only) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lsd");
    agent_rm_rf(fs_base);
    agent_make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --dirs-only", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "subdir");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_files_only) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lsf");
    agent_rm_rf(fs_base);
    agent_make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --files-only", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "file1.txt");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_pattern) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lspt");
    agent_rm_rf(fs_base);
    agent_make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --pattern=*.txt", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "file1.txt");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_depth) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lsdp");
    agent_rm_rf(fs_base);
    agent_make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --depth 2", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "nested");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_missing_path) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lsm");
    agent_rm_rf(fs_base);
    AGENT_MKDIR(fs_base, 0755);
    char out[4096] = {0};
    int status = agent_run(fs_base, "ls \"nonexistent_path_12345\" --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "error") != NULL);
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_empty_dir) {
    char fs_base[NCD_MAX_PATH];
    char ncd_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "lse");
    agent_get_temp_dir(ncd_base, sizeof(ncd_base), "lse_ncd");
    agent_rm_rf(fs_base);
    agent_rm_rf(ncd_base);
    AGENT_MKDIR(fs_base, 0755);
    AGENT_MKDIR(ncd_base, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --json", fs_base);
    int status = agent_run(ncd_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0);
    ASSERT_TRUE(strstr(out, "\"entries\"") != NULL || strstr(out, "\"error\"") != NULL || strstr(out, "cannot open directory") != NULL);
    agent_rm_rf(fs_base);
    agent_rm_rf(ncd_base);
    return 0;
}

TEST(agent_check_path_exists) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "chp");
    agent_rm_rf(fs_base);
    AGENT_MKDIR(fs_base, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "check \"%s\"", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "EXISTS");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_check_path_not_found) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "chn");
    agent_rm_rf(fs_base);
    AGENT_MKDIR(fs_base, 0755);
    char out[4096] = {0};
    int status = agent_run(fs_base, "check \"nonexistent_xyz_12345\"", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "NOT_FOUND") != NULL);
    ASSERT_STR_CONTAINS(out, "NOT_FOUND");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_check_path_json) {
    char fs_base[NCD_MAX_PATH];
    agent_get_temp_dir(fs_base, sizeof(fs_base), "chpj");
    agent_rm_rf(fs_base);
    AGENT_MKDIR(fs_base, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "check \"%s\" --json", fs_base);
    int status = agent_run(fs_base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"exists\"");
    agent_rm_rf(fs_base);
    return 0;
}

TEST(agent_check_db_age_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_LS_SETUP(base, ncd_dir, "chd");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());
    char out[4096] = {0};
    int status = agent_run(base, "check --db-age --json", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"db_age\"");
    agent_rm_rf(base);
    return 0;
}

TEST(agent_check_stats_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    AGENT_LS_SETUP(base, ncd_dir, "chs");
    agent_build_db_path(db_path, sizeof(db_path), ncd_dir, agent_test_drive_letter());
    char out[4096] = {0};
    int status = agent_run(base, "check --stats --json", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"drives\"");
    agent_rm_rf(base);
    return 0;
}

TEST(agent_check_service_status) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "chss");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char out[4096] = {0};
    int status = agent_run(base, "check --service-status --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"status\"");
    agent_rm_rf(base);
    return 0;
}

TEST(agent_check_missing_db) {
    char base[NCD_MAX_PATH];
    agent_get_temp_dir(base, sizeof(base), "chm");
    agent_rm_rf(base);
    AGENT_MKDIR(base, 0755);
    char out[4096] = {0};
    int status = agent_run(base, "check --db-age --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0 || strstr(out, "error") != NULL || strstr(out, "no database") != NULL);
    agent_rm_rf(base);
    return 0;
}

void suite_agent_ls(void) {
    printf("\n=== Agent LS Integration ===\n");
    RUN_TEST(agent_ls_basic_plain);
    RUN_TEST(agent_ls_basic_json);
    RUN_TEST(agent_ls_dirs_only);
    RUN_TEST(agent_ls_files_only);
    RUN_TEST(agent_ls_pattern);
    RUN_TEST(agent_ls_depth);
    RUN_TEST(agent_ls_missing_path);
    RUN_TEST(agent_ls_empty_dir);
}

void suite_agent_check(void) {
    printf("\n=== Agent Check Integration ===\n");
    RUN_TEST(agent_check_path_exists);
    RUN_TEST(agent_check_path_not_found);
    RUN_TEST(agent_check_path_json);
    RUN_TEST(agent_check_db_age_json);
    RUN_TEST(agent_check_stats_json);
    RUN_TEST(agent_check_service_status);
    RUN_TEST(agent_check_missing_db);
}

TEST_MAIN(
    agent_kill_any_service();
    RUN_SUITE(agent_ls);
    RUN_SUITE(agent_check);
)
