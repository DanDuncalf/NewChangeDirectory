/* test_agent_rmdir.c -- Tests for agent rmdir and rmdirs commands */

#include "test_framework.h"
#include "../src/ncd.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
    snprintf(buf, size, "%s\\ncd_rm_%s", tmp, suffix);
#else
    snprintf(buf, size, "%s/ncd_rm_%s", tmp, suffix);
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

static bool file_exists(const char *path) {
#if NCD_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
#endif
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

static void create_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "test\n");
        fclose(f);
    }
}

static void mkdir_p(const char *path) {
#if NCD_PLATFORM_WINDOWS
    char cmd[NCD_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "mkdir \"%s\" 2>nul", path);
    system(cmd);
#else
    char cmd[NCD_MAX_PATH];
    snprintf(cmd, sizeof(cmd), "mkdir -p \"%s\" 2>/dev/null", path);
    system(cmd);
#endif
}

TEST(rmdir_removes_empty) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "re");
    rm_rf(base);
    mkdir(base, 0755);

    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\empty_dir", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/empty_dir", base);
#endif
    mkdir(test_dir, 0755);
    ASSERT_TRUE(dir_exists(test_dir));

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "rmdir \"%s\" --json", test_dir);
    int status = run_agent(base, args, out, sizeof(out));
    ASSERT_EQ_INT(0, status);
    ASSERT_STR_CONTAINS(out, "\"result\":\"removed\"");
    ASSERT_FALSE(dir_exists(test_dir));

    rm_rf(base);
    return 0;
}

TEST(rmdir_fails_on_non_empty) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "rne");
    rm_rf(base);
    mkdir(base, 0755);

    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\non_empty", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/non_empty", base);
#endif
    mkdir(test_dir, 0755);

    char test_file[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_file, sizeof(test_file), "%s\\file.txt", test_dir);
#else
    snprintf(test_file, sizeof(test_file), "%s/file.txt", test_dir);
#endif
    create_file(test_file);
    ASSERT_TRUE(dir_exists(test_dir));
    ASSERT_TRUE(file_exists(test_file));

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "rmdir \"%s\" --json", test_dir);
    int status = run_agent(base, args, out, sizeof(out));
    ASSERT_TRUE(status != 0);
    ASSERT_STR_CONTAINS(out, "error_not_empty");
    ASSERT_TRUE(dir_exists(test_dir));

    rm_rf(base);
    return 0;
}

TEST(rmdirs_force_removes_tree) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "rft");
    rm_rf(base);
    mkdir(base, 0755);

    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\a\\b\\c", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/a/b/c", base);
#endif
    mkdir_p(test_dir);
    ASSERT_TRUE(dir_exists(test_dir));

    char root_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(root_dir, sizeof(root_dir), "%s\\a", base);
#else
    snprintf(root_dir, sizeof(root_dir), "%s/a", base);
#endif

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "rmdirs \"%s\" --force --json", root_dir);
    int status = run_agent(base, args, out, sizeof(out));
    ASSERT_EQ_INT(0, status);
    ASSERT_STR_CONTAINS(out, "\"result\":\"removed\"");
    ASSERT_FALSE(dir_exists(root_dir));

    rm_rf(base);
    return 0;
}

TEST(rmdirs_preserve_root_blocks_drive_root) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "rpr");
    rm_rf(base);
    mkdir(base, 0755);

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(args, sizeof(args), "rmdirs C:/ --force --json");
#else
    snprintf(args, sizeof(args), "rmdirs \"/\" --force --json");
#endif
    int status = run_agent(base, args, out, sizeof(out));
    ASSERT_TRUE(status != 0);
    ASSERT_STR_CONTAINS(out, "root");

    rm_rf(base);
    return 0;
}

TEST(rmdir_force_removes_non_empty) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "rfn");
    rm_rf(base);
    mkdir(base, 0755);

    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\forced", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/forced", base);
#endif
    mkdir(test_dir, 0755);

    char test_file[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_file, sizeof(test_file), "%s\\file.txt", test_dir);
#else
    snprintf(test_file, sizeof(test_file), "%s/file.txt", test_dir);
#endif
    create_file(test_file);
    ASSERT_TRUE(dir_exists(test_dir));

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "rmdir \"%s\" --force --json", test_dir);
    int status = run_agent(base, args, out, sizeof(out));
    ASSERT_EQ_INT(0, status);
    ASSERT_STR_CONTAINS(out, "\"result\":\"removed\"");
    ASSERT_FALSE(dir_exists(test_dir));

    rm_rf(base);
    return 0;
}

TEST(rmdirs_dry_run_does_not_delete) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "rdr");
    rm_rf(base);
    mkdir(base, 0755);

    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\a\\b", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/a/b", base);
#endif
    mkdir_p(test_dir);
    ASSERT_TRUE(dir_exists(test_dir));

    char root_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(root_dir, sizeof(root_dir), "%s\\a", base);
#else
    snprintf(root_dir, sizeof(root_dir), "%s/a", base);
#endif

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "rmdirs \"%s\" --dry-run --json", root_dir);
    int status = run_agent(base, args, out, sizeof(out));
    ASSERT_EQ_INT(0, status);
    ASSERT_STR_CONTAINS(out, "would_remove");
    ASSERT_TRUE(dir_exists(root_dir));

    rm_rf(base);
    return 0;
}

TEST(rmdirs_without_force_fails) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "rwf");
    rm_rf(base);
    mkdir(base, 0755);

    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(test_dir, sizeof(test_dir), "%s\\a\\b", base);
#else
    snprintf(test_dir, sizeof(test_dir), "%s/a/b", base);
#endif
    mkdir_p(test_dir);
    ASSERT_TRUE(dir_exists(test_dir));

    char root_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(root_dir, sizeof(root_dir), "%s\\a", base);
#else
    snprintf(root_dir, sizeof(root_dir), "%s/a", base);
#endif

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "rmdirs \"%s\" --json", root_dir);
    int status = run_agent(base, args, out, sizeof(out));
    ASSERT_TRUE(status != 0);
    ASSERT_STR_CONTAINS(out, "force");
    ASSERT_TRUE(dir_exists(root_dir));

    rm_rf(base);
    return 0;
}

void suite_agent_rmdir(void) {
    printf("\n=== Agent Rmdir Integration ===\n");
    RUN_TEST(rmdir_removes_empty);
    RUN_TEST(rmdir_fails_on_non_empty);
    RUN_TEST(rmdir_force_removes_non_empty);
}

void suite_agent_rmdirs(void) {
    printf("\n=== Agent Rmdirs Integration ===\n");
    RUN_TEST(rmdirs_force_removes_tree);
    RUN_TEST(rmdirs_preserve_root_blocks_drive_root);
    RUN_TEST(rmdirs_dry_run_does_not_delete);
    RUN_TEST(rmdirs_without_force_fails);
}

TEST_MAIN(
    RUN_SUITE(agent_rmdir);
    RUN_SUITE(agent_rmdirs);
)
