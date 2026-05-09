/* test_agent_verify.c -- Tests for verify and chmod agent commands */
#include "test_framework.h"
#include "../src/ncd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* Helper to create temp directory path */
static void get_temp_dir(char *buf, size_t size, const char *suffix) {
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\ncd_verify_test_%s", tmp, suffix);
#else
    snprintf(buf, size, "%s/ncd_verify_test_%s", tmp, suffix);
#endif
}

/* Helper to recursively remove a directory */
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

static const char* find_exe(void) {
#if NCD_PLATFORM_WINDOWS
    if (GetFileAttributesA(".\\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES)
        return ".\\NewChangeDirectory.exe";
    if (GetFileAttributesA("..\\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES)
        return "..\\NewChangeDirectory.exe";
#else
    if (access("./NewChangeDirectory", X_OK) == 0)
        return "./NewChangeDirectory";
    if (access("../NewChangeDirectory", X_OK) == 0)
        return "../NewChangeDirectory";
#endif
    return NULL;
}

static int normalize_exit_code(int status) {
#if NCD_PLATFORM_WINDOWS
    return status;
#else
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return status;
#endif
}

TEST(verify_existing_directory_passes) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "exists");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);

    char cmd[NCD_MAX_PATH * 4];
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/output.txt", test_dir);

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "%s /agent:verify \"%s\" --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
#else
    snprintf(cmd, sizeof(cmd),
        "%s --agent:verify \"%s\" --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
#endif
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *f = fopen(output_file, "r");
    ASSERT_NOT_NULL(f);
    size_t n = fread(output, 1, sizeof(output) - 1, f);
    output[n] = '\0';
    fclose(f);

    ASSERT_EQ_INT(0, ret);
    ASSERT_STR_CONTAINS(output, "\"verified\":true");
    ASSERT_STR_CONTAINS(output, "\"exists\"");
    ASSERT_STR_CONTAINS(output, "\"is_directory\"");

    rm_rf(test_dir);
    return 0;
}

TEST(verify_missing_directory_fails) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "missing");
    rm_rf(test_dir);

    char output_file[NCD_MAX_PATH];
    get_temp_dir(output_file, sizeof(output_file), "missing_out");

    char cmd[NCD_MAX_PATH * 4];

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "%s /agent:verify \"%s\" --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
#else
    snprintf(cmd, sizeof(cmd),
        "%s --agent:verify \"%s\" --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
#endif
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *f = fopen(output_file, "r");
    ASSERT_NOT_NULL(f);
    size_t n = fread(output, 1, sizeof(output) - 1, f);
    output[n] = '\0';
    fclose(f);

    ASSERT_EQ_INT(1, ret);
    ASSERT_STR_CONTAINS(output, "\"verified\":false");

    return 0;
}

TEST(verify_empty_directory_passes) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "empty");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);

    char output_file[NCD_MAX_PATH];
    get_temp_dir(output_file, sizeof(output_file), "empty_out");

    char cmd[NCD_MAX_PATH * 4];

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "%s /agent:verify \"%s\" --empty --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
#else
    snprintf(cmd, sizeof(cmd),
        "%s --agent:verify \"%s\" --empty --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
#endif
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *f = fopen(output_file, "r");
    ASSERT_NOT_NULL(f);
    size_t n = fread(output, 1, sizeof(output) - 1, f);
    output[n] = '\0';
    fclose(f);

    ASSERT_EQ_INT(0, ret);
    ASSERT_STR_CONTAINS(output, "\"verified\":true");
    ASSERT_STR_CONTAINS(output, "\"empty\"");

    rm_rf(test_dir);
    return 0;
}

#if !NCD_PLATFORM_WINDOWS
TEST(verify_mode_mismatch_fails) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "mode");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);

    char cmd[NCD_MAX_PATH * 4];
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/output.txt", test_dir);

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
    snprintf(cmd, sizeof(cmd),
        "%s --agent:verify \"%s\" --mode 0700 --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *f = fopen(output_file, "r");
    ASSERT_NOT_NULL(f);
    size_t n = fread(output, 1, sizeof(output) - 1, f);
    output[n] = '\0';
    fclose(f);

    ASSERT_EQ_INT(1, ret);
    ASSERT_STR_CONTAINS(output, "\"verified\":false");
    ASSERT_STR_CONTAINS(output, "\"mode\"");
    ASSERT_STR_CONTAINS(output, "\"passed\":false");

    rm_rf(test_dir);
    return 0;
}
#endif

TEST(verify_tree_structure_matches) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "tree");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);

    /* Create actual directory tree */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/src", test_dir);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/src/core", test_dir);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/docs", test_dir);
    mkdir(path, 0755);

    /* Create tree spec file (flat format) */
    char spec_file[NCD_MAX_PATH];
    snprintf(spec_file, sizeof(spec_file), "%s/tree.spec", test_dir);
    FILE *f = fopen(spec_file, "w");
    ASSERT_NOT_NULL(f);
    fprintf(f, "src\n");
    fprintf(f, "  core\n");
    fprintf(f, "docs\n");
    fclose(f);

    char cmd[NCD_MAX_PATH * 4];
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/output.txt", test_dir);

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "%s /agent:verify \"%s\" --tree \"%s\" --json > \"%s\" 2>&1",
        exe, test_dir, spec_file, output_file);
#else
    snprintf(cmd, sizeof(cmd),
        "%s --agent:verify \"%s\" --tree \"%s\" --json > \"%s\" 2>&1",
        exe, test_dir, spec_file, output_file);
#endif
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *out = fopen(output_file, "r");
    ASSERT_NOT_NULL(out);
    size_t n = fread(output, 1, sizeof(output) - 1, out);
    output[n] = '\0';
    fclose(out);

    ASSERT_EQ_INT(0, ret);
    ASSERT_STR_CONTAINS(output, "\"verified\":true");
    ASSERT_STR_CONTAINS(output, "\"tree_structure\"");

    rm_rf(test_dir);
    return 0;
}

#if !NCD_PLATFORM_WINDOWS
TEST(chmod_changes_mode) {
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    get_temp_dir(test_dir, sizeof(test_dir), "chmod");
#else
    /* Use /tmp so chmod works (Windows mounts don't support Unix permissions) */
    snprintf(test_dir, sizeof(test_dir), "/tmp/ncd_verify_test_chmod");
#endif
    rm_rf(test_dir);
    mkdir(test_dir, 0755);

    char cmd[NCD_MAX_PATH * 4];
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/output.txt", test_dir);

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
    snprintf(cmd, sizeof(cmd),
        "%s --agent:chmod \"%s\" --mode 0700 --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *out = fopen(output_file, "r");
    ASSERT_NOT_NULL(out);
    size_t n = fread(output, 1, sizeof(output) - 1, out);
    output[n] = '\0';
    fclose(out);

    ASSERT_EQ_INT(0, ret);
    ASSERT_STR_CONTAINS(output, "\"result\":\"changed\"");
    ASSERT_STR_CONTAINS(output, "\"changed\":1");

    struct stat st;
    ASSERT_TRUE(stat(test_dir, &st) == 0);
    ASSERT_EQ_INT(0700, st.st_mode & 0777);

    rm_rf(test_dir);
    return 0;
}
#endif

#if !NCD_PLATFORM_WINDOWS
TEST(chmod_recursive_changes_all) {
    char test_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    get_temp_dir(test_dir, sizeof(test_dir), "chmod_rec");
#else
    /* Use /tmp so chmod works (Windows mounts don't support Unix permissions) */
    snprintf(test_dir, sizeof(test_dir), "/tmp/ncd_verify_test_chmod_rec");
#endif
    rm_rf(test_dir);
    mkdir(test_dir, 0755);

    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/a", test_dir);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/a/b", test_dir);
    mkdir(path, 0755);

    char cmd[NCD_MAX_PATH * 4];
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/output.txt", test_dir);

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
    snprintf(cmd, sizeof(cmd),
        "%s --agent:chmod \"%s\" --mode 0700 --recursive --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *out = fopen(output_file, "r");
    ASSERT_NOT_NULL(out);
    size_t n = fread(output, 1, sizeof(output) - 1, out);
    output[n] = '\0';
    fclose(out);

    ASSERT_EQ_INT(0, ret);
    ASSERT_STR_CONTAINS(output, "\"result\":\"changed\"");
    ASSERT_STR_CONTAINS(output, "\"changed\":3");

    struct stat st;
    stat(test_dir, &st);
    ASSERT_EQ_INT(0700, st.st_mode & 0777);
    snprintf(path, sizeof(path), "%s/a", test_dir);
    stat(path, &st);
    ASSERT_EQ_INT(0700, st.st_mode & 0777);
    snprintf(path, sizeof(path), "%s/a/b", test_dir);
    stat(path, &st);
    ASSERT_EQ_INT(0700, st.st_mode & 0777);

    rm_rf(test_dir);
    return 0;
}
#endif

#if NCD_PLATFORM_WINDOWS
TEST(chmod_returns_unsupported_on_windows) {
    char test_dir[NCD_MAX_PATH];
    get_temp_dir(test_dir, sizeof(test_dir), "chmod_win");
    rm_rf(test_dir);
    mkdir(test_dir, 0755);

    char cmd[NCD_MAX_PATH * 4];
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/output.txt", test_dir);

    const char *exe = find_exe();
    if (!exe) {
        SKIP_TEST("NCD executable not found");
    }
    snprintf(cmd, sizeof(cmd),
        "%s /agent:chmod \"%s\" --mode 0700 --json > \"%s\" 2>&1",
        exe, test_dir, output_file);
    int ret = normalize_exit_code(system(cmd));

    char output[1024];
    FILE *out = fopen(output_file, "r");
    ASSERT_NOT_NULL(out);
    size_t n = fread(output, 1, sizeof(output) - 1, out);
    output[n] = '\0';
    fclose(out);

    ASSERT_EQ_INT(1, ret);
    ASSERT_STR_CONTAINS(output, "\"result\":\"error_unsupported\"");

    rm_rf(test_dir);
    return 0;
}
#endif

static void kill_any_service(void) {
#if NCD_PLATFORM_WINDOWS
    system("taskkill /F /IM NCDService.exe >nul 2>nul");
#else
    system("pkill -9 -x NCDService 2>/dev/null; killall -9 NCDService 2>/dev/null");
#endif
}

void suite_agent_verify(void) {
    RUN_TEST(verify_existing_directory_passes);
    RUN_TEST(verify_missing_directory_fails);
    RUN_TEST(verify_empty_directory_passes);
#if !NCD_PLATFORM_WINDOWS
    RUN_TEST(verify_mode_mismatch_fails);
#endif
    RUN_TEST(verify_tree_structure_matches);
#if !NCD_PLATFORM_WINDOWS
    RUN_TEST(chmod_changes_mode);
    RUN_TEST(chmod_recursive_changes_all);
#endif
#if NCD_PLATFORM_WINDOWS
    RUN_TEST(chmod_returns_unsupported_on_windows);
#endif
}

TEST_MAIN(
    kill_any_service();
    RUN_SUITE(agent_verify);
)
