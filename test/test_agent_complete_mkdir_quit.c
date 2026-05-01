/* test_agent_complete_mkdir_quit.c -- Integration tests for agent complete, mkdir, quit */

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
    snprintf(buf, size, "%s\\ncd_cq_%s", tmp, suffix);
#else
    snprintf(buf, size, "%s/ncd_cq_%s", tmp, suffix);
#endif
}

static void build_ncd_dir(char *buf, size_t size, const char *base) {
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\NCD", base);
#else
    snprintf(buf, size, "%s/ncd", base);
#endif
}

static char test_drive_letter(void) {
#if NCD_PLATFORM_WINDOWS
    return 'C';
#else
    char cwd[NCD_MAX_PATH];
    if (platform_get_current_dir(cwd, sizeof(cwd))) {
        return platform_get_drive_letter(cwd);
    }
    return 0;
#endif
}

static void build_test_drive_root(char *buf, size_t size) {
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%c:\\", test_drive_letter());
#else
    char drive = test_drive_letter();
    if (platform_build_mount_path(drive, buf, size)) {
        return;
    }
    platform_strncpy_s(buf, size, "/");
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

static bool dir_exists(const char *path) {
#if NCD_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

static bool create_test_db(const char *ncd_dir, char drive_letter) {
    char db_path[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(db_path, sizeof(db_path), "%s\\ncd_%c.database", ncd_dir, toupper((unsigned char)drive_letter));
#else
    snprintf(db_path, sizeof(db_path), "%s/ncd_%02x.database", ncd_dir, (unsigned char)drive_letter);
#endif
    NcdDatabase *db = db_create();
    if (!db) return false;
    db->last_scan = time(NULL);
    DriveData *drv = db_add_drive(db, drive_letter);
#if !NCD_PLATFORM_WINDOWS
    {
        char drive_root[NCD_MAX_PATH];
        build_test_drive_root(drive_root, sizeof(drive_root));
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

static const char* find_exe(void) {
#if NCD_PLATFORM_WINDOWS
    if (GetFileAttributesA("..\\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES) return "..\\NewChangeDirectory.exe";
    if (GetFileAttributesA(".\\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES) return ".\\NewChangeDirectory.exe";
#else
    if (access("../NewChangeDirectory", X_OK) == 0) return "../NewChangeDirectory";
    if (access("./NewChangeDirectory", X_OK) == 0) return "./NewChangeDirectory";
#endif
    return NULL;
}

static void build_db_path(char *buf, size_t size, const char *ncd_dir, char drive_letter) {
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\ncd_%c.database", ncd_dir, toupper((unsigned char)drive_letter));
#else
    snprintf(buf, size, "%s/ncd_%02x.database", ncd_dir, (unsigned char)drive_letter);
#endif
}

static int run_agent(const char *ncd_dir, const char *agent_args, char *out, size_t out_size, const char *db_path) {
    const char *exe = find_exe();
    if (!exe) {
        strncpy(out, "EXE_NOT_FOUND", out_size);
        return -1;
    }
    char cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    if (db_path && db_path[0]) {
        snprintf(cmd, sizeof(cmd),
            "set \"LOCALAPPDATA=%s\" && set NCD_TEST_MODE=1 && \"%s\" /agent %s -d \"%s\"",
            ncd_dir, exe, agent_args, db_path);
    } else {
        snprintf(cmd, sizeof(cmd),
            "set \"LOCALAPPDATA=%s\" && set NCD_TEST_MODE=1 && \"%s\" /agent %s",
            ncd_dir, exe, agent_args);
    }
#else
    if (db_path && db_path[0]) {
        snprintf(cmd, sizeof(cmd),
            "XDG_DATA_HOME='%s' NCD_TEST_MODE=1 '%s' --agent:%s -d '%s'",
            ncd_dir, exe, agent_args, db_path);
    } else {
        snprintf(cmd, sizeof(cmd),
            "XDG_DATA_HOME='%s' NCD_TEST_MODE=1 '%s' --agent:%s",
            ncd_dir, exe, agent_args);
    }
#endif
    FILE *fp = POPEN(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    return PCLOSE(fp);
}

#define SETUP_DB(base, ncd_dir, suffix) do { \
    get_temp_dir(base, sizeof(base), suffix); \
    build_ncd_dir(ncd_dir, sizeof(ncd_dir), base); \
    rm_rf(base); \
    mkdir(base, 0755); \
    mkdir(ncd_dir, 0755); \
    ASSERT_TRUE(create_test_db(ncd_dir, test_drive_letter())); \
} while (0)

TEST(agent_complete_basic) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    SETUP_DB(base, ncd_dir, "cb");
    build_db_path(db_path, sizeof(db_path), ncd_dir, test_drive_letter());
    char out[4096] = {0};
    int status = run_agent(base, "complete Do", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_TRUE(strstr(out, "Downloads") != NULL || strstr(out, "Documents") != NULL);
    rm_rf(base);
    return 0;
}

TEST(agent_complete_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    SETUP_DB(base, ncd_dir, "cj");
    build_db_path(db_path, sizeof(db_path), ncd_dir, test_drive_letter());
    char out[4096] = {0};
    int status = run_agent(base, "complete Sys --json", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    rm_rf(base);
    return 0;
}

TEST(agent_complete_limit) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    SETUP_DB(base, ncd_dir, "cl");
    build_db_path(db_path, sizeof(db_path), ncd_dir, test_drive_letter());
    char out[4096] = {0};
    int status = run_agent(base, "complete s --json --limit 2", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    rm_rf(base);
    return 0;
}

TEST(agent_complete_no_match) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    SETUP_DB(base, ncd_dir, "cn");
    build_db_path(db_path, sizeof(db_path), ncd_dir, test_drive_letter());
    char out[4096] = {0};
    int status = run_agent(base, "complete xyz123nonexistent", out, sizeof(out), db_path);
    ASSERT_TRUE(status == 0);
    rm_rf(base);
    return 0;
}

TEST(agent_mkdir_creates_directory) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "mc");
    rm_rf(base);
    mkdir(base, 0755);
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\newdir_test", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/newdir_test", base);
#endif
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mkdir \"%s\"", test_dir);
    int status = run_agent(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_TRUE(dir_exists(test_dir));
    rm_rf(base);
    return 0;
}

TEST(agent_mkdir_existing_directory) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "me");
    rm_rf(base);
    mkdir(base, 0755);
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\existing_dir", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/existing_dir", base);
#endif
    mkdir(test_dir, 0755);
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mkdir \"%s\" --json", test_dir);
    int status = run_agent(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    rm_rf(base);
    return 0;
}

TEST(agent_mkdir_nested_path) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "mn");
    rm_rf(base);
    mkdir(base, 0755);
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\a\\b\\c", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/a/b/c", base);
#endif
    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mkdir \"%s\"", test_dir);
    int status = run_agent(base, args, out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_TRUE(dir_exists(test_dir));
    rm_rf(base);
    return 0;
}

TEST(agent_mkdir_invalid_path) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "mi");
    rm_rf(base);
    mkdir(base, 0755);
    char out[4096] = {0};
    int status = run_agent(base, "mkdir \"\" --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "error") != NULL);
    rm_rf(base);
    return 0;
}

TEST(agent_quit_plain) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qp");
    rm_rf(base);
    mkdir(base, 0755);
    char out[4096] = {0};
    int status = run_agent(base, "quit", out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    rm_rf(base);
    return 0;
}

TEST(agent_quit_json) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qj");
    rm_rf(base);
    mkdir(base, 0755);
    char out[4096] = {0};
    int status = run_agent(base, "quit --json", out, sizeof(out), NULL);
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    rm_rf(base);
    return 0;
}

TEST(agent_unknown_subcommand) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "us");
    rm_rf(base);
    mkdir(base, 0755);
    char out[4096] = {0};
    int status = run_agent(base, "foobar", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "unknown") != NULL || strstr(out, "error") != NULL);
    rm_rf(base);
    return 0;
}

TEST(agent_no_subcommand) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "ns");
    rm_rf(base);
    mkdir(base, 0755);
    char out[4096] = {0};
    int status = run_agent(base, "", out, sizeof(out), NULL);
    ASSERT_TRUE(status != 0 || strstr(out, "requires") != NULL || strstr(out, "error") != NULL);
    rm_rf(base);
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

static void kill_any_service(void) {
#if NCD_PLATFORM_WINDOWS
    system("taskkill /F /IM NCDService.exe >nul 2>nul");
#else
    system("pkill -9 -x NCDService 2>/dev/null; killall -9 NCDService 2>/dev/null");
#endif
}

TEST_MAIN(
    kill_any_service();
    RUN_SUITE(agent_complete);
    RUN_SUITE(agent_mkdir);
    RUN_SUITE(agent_quit);
    RUN_SUITE(agent_edge_cases);
)
