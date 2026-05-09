/* test_agent_mkdir_extended.c -- Extended integration tests for agent mkdir/mkdirs
 *
 * Tests the Agent 1 enhancements from AdditionalAgentFeatures.md:
 *   --dry-run, --parents-required, --force, --mode, --verify,
 *   --atomic, --stop-on-error, stdin input, buffered JSON output.
 */

#include "test_framework.h"
#include "../src/ncd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path) _rmdir(path)
#else
#include <unistd.h>
#include <errno.h>
#endif

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

static void rm_rf(const char *path)
{
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

static bool dir_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && (st.st_mode & S_IFDIR));
}

static void read_output(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f) { buf[0] = '\0'; return; }
    size_t n = fread(buf, 1, size - 1, f);
    buf[n] = '\0';
    fclose(f);
}

static const char* find_exe(void)
{
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

static int run_agent_mkdir(const char *extra_args, char *out, size_t out_size)
{
    const char *exe = find_exe();
    if (!exe) {
        strncpy(out, "EXE_NOT_FOUND", out_size);
        return -1;
    }
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/ncd_a1_out.txt", tmp);

    char cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "set NCD_TEST_MODE=1 && %s /agent mkdir %s > \"%s\" 2>&1",
        exe, extra_args, output_file);
#else
    snprintf(cmd, sizeof(cmd),
        "NCD_TEST_MODE=1 %s --agent:mkdir %s > '%s' 2>&1",
        exe, extra_args, output_file);
#endif
    int ret = system(cmd);
    read_output(output_file, out, out_size);
    return ret;
}

static int run_agent_mkdirs(const char *extra_args,
                            const char *input_file,
                            char *out, size_t out_size)
{
    const char *exe = find_exe();
    if (!exe) {
        strncpy(out, "EXE_NOT_FOUND", out_size);
        return -1;
    }
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    char output_file[NCD_MAX_PATH];
    snprintf(output_file, sizeof(output_file), "%s/ncd_a1_out.txt", tmp);

    char cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    if (input_file && input_file[0]) {
        snprintf(cmd, sizeof(cmd),
            "set NCD_TEST_MODE=1 && %s /agent mkdirs %s < \"%s\" > \"%s\" 2>&1",
            exe, extra_args, input_file, output_file);
    } else {
        snprintf(cmd, sizeof(cmd),
            "set NCD_TEST_MODE=1 && %s /agent mkdirs %s > \"%s\" 2>&1",
            exe, extra_args, output_file);
    }
#else
    if (input_file && input_file[0]) {
        snprintf(cmd, sizeof(cmd),
            "NCD_TEST_MODE=1 %s --agent:mkdirs %s < '%s' > '%s' 2>&1",
            exe, extra_args, input_file, output_file);
    } else {
        snprintf(cmd, sizeof(cmd),
            "NCD_TEST_MODE=1 %s --agent:mkdirs %s > '%s' 2>&1",
            exe, extra_args, output_file);
    }
#endif
    int ret = system(cmd);
    read_output(output_file, out, out_size);
    return ret;
}

static void make_input_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "FAIL: %s:%d: fopen failed for %s\n", __FILE__, __LINE__, path);
        return;
    }
    fprintf(f, "%s", content);
    fclose(f);
}

/* ------------------------------------------------------------------
 * mkdir tests
 * ------------------------------------------------------------------ */

TEST(mkdir_dry_run_does_not_create)
{
    const char *dir = "ncd_a1_mkdir_dryrun";
    rm_rf(dir);

    char args[NCD_MAX_PATH];
    snprintf(args, sizeof(args), "\"%s\" --dry-run", dir);

    char out[4096] = {0};
    int ret = run_agent_mkdir(args, out, sizeof(out));
    ASSERT_TRUE(ret == 0);
    ASSERT_FALSE(dir_exists(dir));
    ASSERT_STR_CONTAINS(out, "would be created");

    rm_rf(dir);
    return 0;
}

TEST(mkdir_verify_existing_passes)
{
    const char *dir = "ncd_a1_mkdir_verify_ok";
    rm_rf(dir);
    mkdir(dir, 0755);

    char args[NCD_MAX_PATH];
    snprintf(args, sizeof(args), "\"%s\" --verify --json", dir);

    char out[4096] = {0};
    int ret = run_agent_mkdir(args, out, sizeof(out));
    ASSERT_TRUE(ret == 0);
    ASSERT_STR_CONTAINS(out, "\"result\":\"verified\"");

    rm_rf(dir);
    return 0;
}

TEST(mkdir_verify_missing_fails)
{
    const char *dir = "ncd_a1_mkdir_verify_missing";
    rm_rf(dir);

    char args[NCD_MAX_PATH];
    snprintf(args, sizeof(args), "\"%s\" --verify --json", dir);

    char out[4096] = {0};
    int ret = run_agent_mkdir(args, out, sizeof(out));
    ASSERT_TRUE(ret != 0);
    ASSERT_STR_CONTAINS(out, "\"result\":\"error\"");

    return 0;
}

TEST(mkdir_force_recreate_empty)
{
    const char *dir = "ncd_a1_mkdir_force_empty";
    rm_rf(dir);
    mkdir(dir, 0755);

    char args[NCD_MAX_PATH];
    snprintf(args, sizeof(args), "\"%s\" --force", dir);

    char out[4096] = {0};
    int ret = run_agent_mkdir(args, out, sizeof(out));
    ASSERT_TRUE(ret == 0);
    ASSERT_TRUE(dir_exists(dir));
    ASSERT_STR_CONTAINS(out, "created");

    rm_rf(dir);
    return 0;
}

TEST(mkdir_force_fails_on_non_empty)
{
    const char *dir = "ncd_a1_mkdir_force_nonempty";
    rm_rf(dir);
    mkdir(dir, 0755);

    char inner[NCD_MAX_PATH];
    snprintf(inner, sizeof(inner), "%s/inner.txt", dir);
    FILE *f = fopen(inner, "w");
    if (f) { fprintf(f, "x"); fclose(f); }

    char args[NCD_MAX_PATH];
    snprintf(args, sizeof(args), "\"%s\" --force --json", dir);

    char out[4096] = {0};
    int ret = run_agent_mkdir(args, out, sizeof(out));
    ASSERT_TRUE(ret != 0);
    ASSERT_STR_CONTAINS(out, "\"result\":\"error_not_empty\"");

    rm_rf(dir);
    return 0;
}

TEST(mkdir_mode_0700)
{
    char dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(dir, sizeof(dir), "%s", "ncd_a1_mkdir_mode");
#else
    /* Use /tmp so chmod works (Windows mounts don't support Unix permissions) */
    snprintf(dir, sizeof(dir), "/tmp/ncd_a1_mkdir_mode");
#endif
    rm_rf(dir);

    char args[NCD_MAX_PATH];
    snprintf(args, sizeof(args), "\"%s\" --mode 0700", dir);

    char out[4096] = {0};
    int ret = run_agent_mkdir(args, out, sizeof(out));
    ASSERT_TRUE(ret == 0);
    ASSERT_TRUE(dir_exists(dir));

#if !NCD_PLATFORM_WINDOWS
    struct stat st;
    ASSERT_TRUE(stat(dir, &st) == 0);
    ASSERT_EQ_INT(0700, (st.st_mode & 0777));
#endif

    rm_rf(dir);
    return 0;
}

/* ------------------------------------------------------------------
 * mkdirs tests
 * ------------------------------------------------------------------ */

TEST(mkdirs_dry_run_lists_all_paths)
{
    const char *tree = "ncd_a1_dryrun_tree";
    rm_rf(tree);

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    char input_file[NCD_MAX_PATH];
    snprintf(input_file, sizeof(input_file), "%s/ncd_a1_dryrun.txt", tmp);
    make_input_file(input_file,
        "ncd_a1_dryrun_tree\n"
        "  src\n"
        "  docs\n");

    char out[4096] = {0};
    int ret = run_agent_mkdirs("--dry-run --json", input_file, out, sizeof(out));
    ASSERT_TRUE(ret == 0);
    ASSERT_STR_CONTAINS(out, "ncd_a1_dryrun_tree");
#if NCD_PLATFORM_WINDOWS
    ASSERT_STR_CONTAINS(out, "ncd_a1_dryrun_tree\\\\src");
    ASSERT_STR_CONTAINS(out, "ncd_a1_dryrun_tree\\\\docs");
#else
    ASSERT_STR_CONTAINS(out, "ncd_a1_dryrun_tree/src");
    ASSERT_STR_CONTAINS(out, "ncd_a1_dryrun_tree/docs");
#endif
    ASSERT_STR_CONTAINS(out, "\"result\":\"created\"");

    /* directories must NOT have been created */
    ASSERT_FALSE(dir_exists(tree));

    rm_rf(tree);
    return 0;
}

TEST(mkdirs_atomic_all_or_nothing)
{
    const char *tree = "ncd_a1_atomic_tree";
    rm_rf(tree);

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    char input_file[NCD_MAX_PATH];
    snprintf(input_file, sizeof(input_file), "%s/ncd_a1_atomic.txt", tmp);
    make_input_file(input_file,
        "ncd_a1_atomic_tree\n"
        "  src\n"
        "    core\n"
        "  docs\n");

    char out[4096] = {0};
    int ret = run_agent_mkdirs("--atomic --json", input_file, out, sizeof(out));
    ASSERT_TRUE(ret == 0);

    ASSERT_TRUE(dir_exists("ncd_a1_atomic_tree"));
    ASSERT_TRUE(dir_exists("ncd_a1_atomic_tree/src"));
    ASSERT_TRUE(dir_exists("ncd_a1_atomic_tree/src/core"));
    ASSERT_TRUE(dir_exists("ncd_a1_atomic_tree/docs"));

    /* JSON should contain empty rollback array */
    ASSERT_STR_CONTAINS(out, "\"rollback\":[]");

    rm_rf(tree);
    return 0;
}

TEST(mkdirs_atomic_rollback_on_failure)
{
    const char *tree = "ncd_a1_rb_tree";
    rm_rf(tree);

    /* Pre-create root and a file that blocks one directory */
    mkdir(tree, 0755);
    char block_path[NCD_MAX_PATH];
    snprintf(block_path, sizeof(block_path), "%s/block_file", tree);
    FILE *f = fopen(block_path, "w");
    if (f) fclose(f);

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    char input_file[NCD_MAX_PATH];
    snprintf(input_file, sizeof(input_file), "%s/ncd_a1_rb.txt", tmp);
    make_input_file(input_file,
        "ncd_a1_rb_tree\n"
        "  ok_dir\n"
        "  block_file\n"
        "    child\n");

    char out[4096] = {0};
    int ret = run_agent_mkdirs("--atomic --json", input_file, out, sizeof(out));
    ASSERT_TRUE(ret != 0);

    /* ok_dir should have been rolled back */
    char ok_path[NCD_MAX_PATH];
    snprintf(ok_path, sizeof(ok_path), "%s/ok_dir", tree);
    ASSERT_FALSE(dir_exists(ok_path));

    /* output must contain rollback array (empty since validation failed before creation) */
    ASSERT_STR_CONTAINS(out, "\"rollback\"");
    ASSERT_STR_CONTAINS(out, "\"result\":\"error_not_empty\"");

    rm_rf(tree);
    return 0;
}

TEST(mkdirs_verify_tree_mismatch_fails)
{
    const char *tree = "ncd_a1_verify_tree";
    rm_rf(tree);

    /* Create partial tree */
    mkdir(tree, 0755);
    char sub[NCD_MAX_PATH];
    snprintf(sub, sizeof(sub), "%s/a", tree);
    mkdir(sub, 0755);
    /* intentionally omit 'b' */

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    char input_file[NCD_MAX_PATH];
    snprintf(input_file, sizeof(input_file), "%s/ncd_a1_verify.txt", tmp);
    make_input_file(input_file,
        "ncd_a1_verify_tree\n"
        "  a\n"
        "    b\n");

    char out[4096] = {0};
    int ret = run_agent_mkdirs("--verify --json", input_file, out, sizeof(out));
    ASSERT_TRUE(ret != 0);
    ASSERT_STR_CONTAINS(out, "error");

    rm_rf(tree);
    return 0;
}

TEST(mkdirs_reads_from_stdin)
{
    const char *tree = "ncd_a1_stdin_tree";
    rm_rf(tree);

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    char input_file[NCD_MAX_PATH];
    snprintf(input_file, sizeof(input_file), "%s/ncd_a1_stdin.txt", tmp);
    make_input_file(input_file,
        "ncd_a1_stdin_tree\n"
        "  src\n"
        "  docs\n");

    char out[4096] = {0};
    /* no --file and no positional argument => reads from stdin */
    int ret = run_agent_mkdirs("", input_file, out, sizeof(out));
    ASSERT_TRUE(ret == 0);
    ASSERT_TRUE(dir_exists("ncd_a1_stdin_tree"));
    ASSERT_TRUE(dir_exists("ncd_a1_stdin_tree/src"));
    ASSERT_TRUE(dir_exists("ncd_a1_stdin_tree/docs"));

    rm_rf(tree);
    return 0;
}

TEST(mkdirs_json_is_valid_on_failure)
{
    const char *tree = "ncd_a1_json_fail";
    rm_rf(tree);

    /* block one node with a file */
    mkdir(tree, 0755);
    char block_path[NCD_MAX_PATH];
    snprintf(block_path, sizeof(block_path), "%s/block_file", tree);
    FILE *f = fopen(block_path, "w");
    if (f) fclose(f);

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    char input_file[NCD_MAX_PATH];
    snprintf(input_file, sizeof(input_file), "%s/ncd_a1_jsonf.txt", tmp);
    make_input_file(input_file,
        "ncd_a1_json_fail\n"
        "  ok_dir\n"
        "  block_file\n"
        "    child\n");

    char out[4096] = {0};
    /* non-atomic, but with --json => must still emit complete JSON */
    int ret = run_agent_mkdirs("--json", input_file, out, sizeof(out));
    ASSERT_TRUE(ret != 0);

    /* basic JSON structural checks */
    ASSERT_STR_CONTAINS(out, "{\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"dirs\":");
    ASSERT_STR_CONTAINS(out, "\"summary\":");
    ASSERT_STR_CONTAINS(out, "}");

    rm_rf(tree);
    return 0;
}

TEST(mkdirs_json_contains_rollback_array)
{
    const char *tree = "ncd_a1_json_rb";
    rm_rf(tree);

    mkdir(tree, 0755);
    char block_path[NCD_MAX_PATH];
    snprintf(block_path, sizeof(block_path), "%s/block_file", tree);
    FILE *f = fopen(block_path, "w");
    if (f) fclose(f);

    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = "/tmp";
    char input_file[NCD_MAX_PATH];
    snprintf(input_file, sizeof(input_file), "%s/ncd_a1_jsonrb.txt", tmp);
    make_input_file(input_file,
        "ncd_a1_json_rb\n"
        "  ok_dir\n"
        "  block_file\n"
        "    child\n");

    char out[4096] = {0};
    int ret = run_agent_mkdirs("--atomic --json", input_file, out, sizeof(out));
    ASSERT_TRUE(ret != 0);

    ASSERT_STR_CONTAINS(out, "\"rollback\"");
    ASSERT_STR_CONTAINS(out, "\"result\":\"error_not_empty\"");

    rm_rf(tree);
    return 0;
}

static void kill_any_service(void) {
#if NCD_PLATFORM_WINDOWS
    system("taskkill /F /IM NCDService.exe >nul 2>nul");
#else
    system("pkill -9 -x NCDService 2>/dev/null; killall -9 NCDService 2>/dev/null");
#endif
}

/* ------------------------------------------------------------------
 * Suite & main
 * ------------------------------------------------------------------ */

void suite_agent_mkdir_extended(void)
{
    RUN_TEST(mkdir_dry_run_does_not_create);
    RUN_TEST(mkdir_verify_existing_passes);
    RUN_TEST(mkdir_verify_missing_fails);
    RUN_TEST(mkdir_force_recreate_empty);
    RUN_TEST(mkdir_force_fails_on_non_empty);
    RUN_TEST(mkdir_mode_0700);
    RUN_TEST(mkdirs_dry_run_lists_all_paths);
    RUN_TEST(mkdirs_atomic_all_or_nothing);
    RUN_TEST(mkdirs_atomic_rollback_on_failure);
    RUN_TEST(mkdirs_verify_tree_mismatch_fails);
    RUN_TEST(mkdirs_reads_from_stdin);
    RUN_TEST(mkdirs_json_is_valid_on_failure);
    RUN_TEST(mkdirs_json_contains_rollback_array);
}

TEST_MAIN(
    kill_any_service();
    RUN_SUITE(agent_mkdir_extended);
)
