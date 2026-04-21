/* test_agent_ls_check.c -- Integration tests for agent ls and check commands */

#include "test_framework.h"
#include "../src/database.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#define access(path, mode) _access(path, mode)
#define F_OK 0
#define X_OK 0
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#define POPEN popen
#define PCLOSE pclose
#endif

static void get_temp_dir(char *buf, size_t size, const char *suffix) {
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\ncd_lc_%s", tmp, suffix);
#else
    snprintf(buf, size, "%s/ncd_lc_%s", tmp, suffix);
#endif
}

static void rm_rf(const char *path) {
#if NCD_PLATFORM_WINDOWS
    char cmd[NCD_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", path);
    system(cmd);
#else
    char cmd[NCD_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" 2>/dev/null", path);
    system(cmd);
#endif
}

static bool create_test_db(const char *ncd_dir, char drive_letter) {
    char db_path[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(db_path, sizeof(db_path), "%s\ncd_%c.database", ncd_dir, toupper((unsigned char)drive_letter));
#else
    snprintf(db_path, sizeof(db_path), "%s/ncd_%02x.database", ncd_dir, (unsigned char)drive_letter);
#endif
    NcdDatabase *db = db_create();
    if (!db) return false;
    db->last_scan = time(NULL);
    DriveData *drv = db_add_drive(db, drive_letter);
    db_add_dir(drv, "Users", -1, false, false);
    bool ok = db_save_binary_single(db, 0, db_path);
    db_free(db);
    return ok;
}

static const char* find_exe(void) {
#if NCD_PLATFORM_WINDOWS
    if (GetFileAttributesA("..\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES) return "..\NewChangeDirectory.exe";
    if (GetFileAttributesA(".\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES) return ".\NewChangeDirectory.exe";
#else
    if (access("../NewChangeDirectory", X_OK) == 0) return "../NewChangeDirectory";
    if (access("./NewChangeDirectory", X_OK) == 0) return "./NewChangeDirectory";
#endif
    return NULL;
}

static int run_agent(const char *ncd_dir, const char *agent_args, char *out, size_t out_size) {
    const char *exe = find_exe();
    if (!exe) {
        strncpy(out, "EXE_NOT_FOUND", out_size);
        return -1;
    }
    char cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "set LOCALAPPDATA=%s&& set NCD_TEST_MODE=1 && %s /agent %s",
        ncd_dir, exe, agent_args);
#else
    snprintf(cmd, sizeof(cmd),
        "XDG_DATA_HOME='%s' NCD_TEST_MODE=1 %s --agent:%s",
        ncd_dir, exe, agent_args);
#endif
    FILE *fp = POPEN(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    return PCLOSE(fp);
}

static void make_fs_tree(const char *base) {
    char path[NCD_MAX_PATH];
    mkdir(base, 0755);
#if NCD_PLATFORM_WINDOWS
    snprintf(path, sizeof(path), "%s\subdir", base);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s\subdir\nested", base);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s\file1.txt", base);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
    snprintf(path, sizeof(path), "%s\subdir\file2.log", base);
    f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
#else
    snprintf(path, sizeof(path), "%s/subdir", base);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/subdir/nested", base);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/file1.txt", base);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
    snprintf(path, sizeof(path), "%s/subdir/file2.log", base);
    f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
#endif
}

TEST(agent_ls_basic_plain) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lsp");
    rm_rf(fs_base);
    make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\"", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "subdir");
    ASSERT_STR_CONTAINS(out, "file1.txt");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_basic_json) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lsj");
    rm_rf(fs_base);
    make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --json", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"entries\"");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_dirs_only) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lsd");
    rm_rf(fs_base);
    make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --dirs-only", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "subdir");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_files_only) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lsf");
    rm_rf(fs_base);
    make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --files-only", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "file1.txt");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_pattern) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lspt");
    rm_rf(fs_base);
    make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --pattern=*.txt", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "file1.txt");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_depth) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lsdp");
    rm_rf(fs_base);
    make_fs_tree(fs_base);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --depth 2", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "nested");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_missing_path) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lsm");
    rm_rf(fs_base);
    mkdir(fs_base, 0755);
    char out[4096] = {0};
    int status = run_agent(fs_base, "ls \"nonexistent_path_12345\" --json", out, sizeof(out));
    ASSERT_TRUE(status != 0 || strstr(out, "error") != NULL);
    rm_rf(fs_base);
    return 0;
}

TEST(agent_ls_empty_dir) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "lse");
    rm_rf(fs_base);
    mkdir(fs_base, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ls \"%s\" --json", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"entries\"");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_check_path_exists) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "chp");
    rm_rf(fs_base);
    mkdir(fs_base, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "check \"%s\"", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "EXISTS");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_check_path_not_found) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "chn");
    rm_rf(fs_base);
    mkdir(fs_base, 0755);
    char out[4096] = {0};
    int status = run_agent(fs_base, "check \"nonexistent_xyz_12345\"", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "NOT_FOUND");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_check_path_json) {
    char fs_base[NCD_MAX_PATH];
    get_temp_dir(fs_base, sizeof(fs_base), "chpj");
    rm_rf(fs_base);
    mkdir(fs_base, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "check \"%s\" --json", fs_base);
    int status = run_agent(fs_base, args, out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"exists\"");
    rm_rf(fs_base);
    return 0;
}

TEST(agent_check_db_age_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "chd");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));
    char out[4096] = {0};
    int status = run_agent(base, "check --db-age --json", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"db_age\"");
    rm_rf(base);
    return 0;
}

TEST(agent_check_stats_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "chs");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));
    char out[4096] = {0};
    int status = run_agent(base, "check --stats --json", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"drives\"");
    rm_rf(base);
    return 0;
}

TEST(agent_check_service_status) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "chss");
    rm_rf(base);
    mkdir(base, 0755);
    char out[4096] = {0};
    int status = run_agent(base, "check --service-status --json", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"status\"");
    rm_rf(base);
    return 0;
}

TEST(agent_check_missing_db) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "chm");
    rm_rf(base);
    mkdir(base, 0755);
    char out[4096] = {0};
    int status = run_agent(base, "check --db-age --json", out, sizeof(out));
    ASSERT_TRUE(status == 0 || strstr(out, "error") != NULL || strstr(out, "no database") != NULL);
    rm_rf(base);
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
    RUN_SUITE(agent_ls);
    RUN_SUITE(agent_check);
)
