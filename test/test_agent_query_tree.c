/* test_agent_query_tree.c -- Integration tests for agent query and tree commands
 *
 * Creates temp databases, invokes NewChangeDirectory.exe, and verifies output.
 */

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

/* ================================================================ Helpers */

static void get_temp_dir(char *buf, size_t size, const char *suffix) {
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\ncd_qt_%s", tmp, suffix);
#else
    snprintf(buf, size, "%s/ncd_qt_%s", tmp, suffix);
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
    snprintf(db_path, sizeof(db_path), "%s\\ncd_%c.database", ncd_dir, toupper((unsigned char)drive_letter));
#else
    snprintf(db_path, sizeof(db_path), "%s/ncd_%02x.database", ncd_dir, (unsigned char)drive_letter);
#endif
    NcdDatabase *db = db_create();
    if (!db) return false;
    db->last_scan = time(NULL);
    DriveData *drv = db_add_drive(db, drive_letter);
    int users = db_add_dir(drv, "Users", -1, false, false);
    int scott = db_add_dir(drv, "scott", users, false, false);
    int admin = db_add_dir(drv, "admin", users, false, false);
    db_add_dir(drv, "Downloads", scott, false, false);
    db_add_dir(drv, "Documents", scott, false, false);
    db_add_dir(drv, "Music", scott, false, false);
    db_add_dir(drv, "Pictures", scott, false, false);
    db_add_dir(drv, "Downloads", admin, false, false);
    int windows = db_add_dir(drv, "Windows", -1, false, true);
    db_add_dir(drv, "System32", windows, false, true);
    db_add_dir(drv, "SysWOW64", windows, false, true);
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

/* ================================================================ Query Tests */

TEST(agent_query_basic_plain) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qb");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "query Downloads", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "Downloads");

    rm_rf(base);
    return 0;
}

TEST(agent_query_basic_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qj");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "query Downloads --json", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"query\"");
    ASSERT_STR_CONTAINS(out, "\"results\"");
    ASSERT_STR_CONTAINS(out, "Downloads");

    rm_rf(base);
    return 0;
}

TEST(agent_query_no_match_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qn");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "query nonexistent_xyz_123 --json", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"results\":[]");

    rm_rf(base);
    return 0;
}

TEST(agent_query_limit) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "ql");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "query s --json --limit=2", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    /* Should limit to 2 results */
    ASSERT_STR_CONTAINS(out, "\"v\":1");

    rm_rf(base);
    return 0;
}

TEST(agent_query_chain_search) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qc");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "query scott\\doc --json", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "Documents");
    ASSERT_STR_CONTAINS(out, "scott");

    rm_rf(base);
    return 0;
}

TEST(agent_query_missing_db) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qm");
    rm_rf(base);
    mkdir(base, 0755);
    /* No database created */

    char out[4096] = {0};
    int status = run_agent(base, "query foo --json", out, sizeof(out));
    /* Should return error or empty */
    ASSERT_TRUE(status == 0 || strstr(out, "error") != NULL || strstr(out, "no database") != NULL);

    rm_rf(base);
    return 0;
}

TEST(agent_query_case_insensitive) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "qci");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out1[4096] = {0};
    char out2[4096] = {0};
    run_agent(base, "query downloads --json", out1, sizeof(out1));
    run_agent(base, "query DOWNLOADS --json", out2, sizeof(out2));
    ASSERT_STR_CONTAINS(out1, "\"v\":1");
    ASSERT_STR_CONTAINS(out2, "\"v\":1");

    rm_rf(base);
    return 0;
}

/* ================================================================ Tree Tests */

TEST(agent_tree_basic_plain) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "tp");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "tree C:\\Users", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "Users");
    ASSERT_STR_CONTAINS(out, "scott");

    rm_rf(base);
    return 0;
}

TEST(agent_tree_basic_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "tj");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "tree C:\\Users --json", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"tree\"");
    ASSERT_STR_CONTAINS(out, "\"n\"");
    ASSERT_STR_CONTAINS(out, "\"d\"");

    rm_rf(base);
    return 0;
}

TEST(agent_tree_flat) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "tf");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "tree C:\\Users --flat", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    /* Flat format should show relative paths */
    ASSERT_TRUE(strstr(out, "scott") != NULL);

    rm_rf(base);
    return 0;
}

TEST(agent_tree_flat_json) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "tfj");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "tree C:\\Users --json --flat", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "\"v\":1");
    ASSERT_STR_CONTAINS(out, "\"tree\"");

    rm_rf(base);
    return 0;
}

TEST(agent_tree_depth_limit) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "td");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "tree C:\\Users --depth 1", out, sizeof(out));
    ASSERT_TRUE(status == 0);
    ASSERT_STR_CONTAINS(out, "Users");
    /* With depth 1, should only show immediate children */

    rm_rf(base);
    return 0;
}

TEST(agent_tree_not_found) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "tn");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/NCD", base);
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    ASSERT_TRUE(create_test_db(ncd_dir, 'C'));

    char out[4096] = {0};
    int status = run_agent(base, "tree C:\\Nonexistent --json", out, sizeof(out));
    ASSERT_TRUE(status != 0 || strstr(out, "not found") != NULL || strstr(out, "error") != NULL);

    rm_rf(base);
    return 0;
}

TEST(agent_tree_missing_db) {
    char base[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "tm");
    rm_rf(base);
    mkdir(base, 0755);

    char out[4096] = {0};
    int status = run_agent(base, "tree C:\\Users --json", out, sizeof(out));
    ASSERT_TRUE(status != 0 || strstr(out, "error") != NULL || strstr(out, "no database") != NULL);

    rm_rf(base);
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
    RUN_SUITE(agent_query);
    RUN_SUITE(agent_tree);
)
