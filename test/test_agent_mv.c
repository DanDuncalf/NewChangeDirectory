/* test_agent_mv.c -- Tests for agent mv and ln commands */
#include "test_framework.h"
#include "../src/ncd.h"
#include "../src/database.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* Platform-specific includes */
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
#if NCD_PLATFORM_WINDOWS
    char user_profile[NCD_MAX_PATH] = {0};
    const char *tmp = NULL;
    DWORD n = GetEnvironmentVariableA("USERPROFILE", user_profile, (DWORD)sizeof(user_profile));
    if (n > 0 && n < sizeof(user_profile)) {
        tmp = user_profile;
    }
    if (!tmp) tmp = getenv("USERPROFILE");
    if (!tmp) tmp = getenv("TEMP");
    if (!tmp) tmp = "C:\\";
    snprintf(buf, size, "%s\\ncd_mv_%s", tmp, suffix);
#else
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
    snprintf(buf, size, "%s/ncd_mv_%s", tmp, suffix);
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

static bool create_test_db(const char *ncd_dir, char drive_letter, const char *label) {
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
    if (label) {
        strncpy(drv->label, label, sizeof(drv->label) - 1);
        drv->label[sizeof(drv->label) - 1] = '\0';
    }
    db_add_dir(drv, "TestSrc", -1, false, false);
    db_add_dir(drv, "Child", 0, false, false);
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

static int run_agent(const char *base_dir, const char *agent_args, char *out, size_t out_size) {
    const char *exe = find_exe();
    if (!exe) {
        strncpy(out, "EXE_NOT_FOUND", out_size);
        return -1;
    }
    char cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "set \"LOCALAPPDATA=%s\" && set NCD_TEST_MODE=1 && \"%s\" /agent %s",
        base_dir, exe, agent_args);
#else
    snprintf(cmd, sizeof(cmd),
        "XDG_DATA_HOME='%s' NCD_TEST_MODE=1 %s --agent:%s",
        base_dir, exe, agent_args);
#endif
    FILE *fp = POPEN(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    return PCLOSE(fp);
}

TEST(mv_renames_directory) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "mv1");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
    char src[NCD_MAX_PATH], dst[NCD_MAX_PATH];
    snprintf(src, sizeof(src), "%s\\TestSrc", base);
    snprintf(dst, sizeof(dst), "%s\\TestDst", base);
    const char test_drive = 'C';
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/ncd", base);
    char src[NCD_MAX_PATH], dst[NCD_MAX_PATH];
    snprintf(src, sizeof(src), "%s/TestSrc", base);
    snprintf(dst, sizeof(dst), "%s/TestDst", base);
    const char test_drive = '/';
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    mkdir(src, 0755);

    ASSERT_TRUE(create_test_db(ncd_dir, test_drive, base));

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mv \"%s\" \"%s\" --json", src, dst);
    int status = run_agent(base, args, out, sizeof(out));

    if (strstr(out, "EXE_NOT_FOUND")) {
        printf("SKIP: NCD executable not found\n");
        rm_rf(base);
        return 0;
    }

    ASSERT_EQ_INT(0, status);
    ASSERT_STR_CONTAINS(out, "\"result\":\"moved\"");
    ASSERT_TRUE(platform_dir_exists(dst));
    ASSERT_FALSE(platform_dir_exists(src));

    rm_rf(base);
    return 0;
}

TEST(mv_updates_database) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    get_temp_dir(base, sizeof(base), "mvdb");
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
    char src[NCD_MAX_PATH], dst[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    snprintf(src, sizeof(src), "%s\\TestSrc", base);
    snprintf(dst, sizeof(dst), "%s\\TestDst", base);
    snprintf(db_path, sizeof(db_path), "%s\\ncd_%c.database", ncd_dir, 'C');
    const char test_drive = 'C';
#else
    /* Use /tmp to keep drive label short (label buffer is only 64 bytes) */
    snprintf(base, sizeof(base), "/tmp/ncd_mv_mvdb");
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/ncd", base);
    char src[NCD_MAX_PATH], dst[NCD_MAX_PATH], db_path[NCD_MAX_PATH];
    snprintf(src, sizeof(src), "%s/TestSrc", base);
    snprintf(dst, sizeof(dst), "%s/TestDst", base);
    snprintf(db_path, sizeof(db_path), "%s/ncd_%02x.database", ncd_dir, (unsigned char)'/');
    const char test_drive = '/';
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    mkdir(src, 0755);

    ASSERT_TRUE(create_test_db(ncd_dir, test_drive, base));

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mv \"%s\" \"%s\" --json", src, dst);
    int status = run_agent(base, args, out, sizeof(out));

    if (strstr(out, "EXE_NOT_FOUND")) {
        printf("SKIP: NCD executable not found\n");
        rm_rf(base);
        return 0;
    }

    ASSERT_EQ_INT(0, status);

    /* Load database and verify update */
    NcdDatabase *db = db_load_binary(db_path);
    ASSERT_NOT_NULL(db);

    bool found_src = false;
    bool found_dst = false;
    char path_buf[NCD_MAX_PATH];

    for (int d = 0; d < db->drive_count; d++) {
        DriveData *drv = &db->drives[d];
        for (int i = 0; i < drv->dir_count; i++) {
            db_full_path(drv, i, path_buf, sizeof(path_buf));
            if (_stricmp(path_buf, src) == 0) found_src = true;
            if (_stricmp(path_buf, dst) == 0) found_dst = true;
        }
    }

    ASSERT_FALSE(found_src);
    ASSERT_TRUE(found_dst);

    db_free(db);
    rm_rf(base);
    return 0;
}

TEST(mv_force_overwrite_empty) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    get_temp_dir(base, sizeof(base), "mvf");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
    char src[NCD_MAX_PATH], dst[NCD_MAX_PATH];
    snprintf(src, sizeof(src), "%s\\TestSrc", base);
    snprintf(dst, sizeof(dst), "%s\\TestDst", base);
    const char test_drive = 'C';
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/ncd", base);
    char src[NCD_MAX_PATH], dst[NCD_MAX_PATH];
    snprintf(src, sizeof(src), "%s/TestSrc", base);
    snprintf(dst, sizeof(dst), "%s/TestDst", base);
    const char test_drive = '/';
#endif
    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    mkdir(src, 0755);
    mkdir(dst, 0755);  /* Create empty dst */

    ASSERT_TRUE(create_test_db(ncd_dir, test_drive, base));

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "mv \"%s\" \"%s\" --force --json", src, dst);
    int status = run_agent(base, args, out, sizeof(out));

    if (strstr(out, "EXE_NOT_FOUND")) {
        printf("SKIP: NCD executable not found\n");
        rm_rf(base);
        return 0;
    }

    ASSERT_EQ_INT(0, status);
    ASSERT_STR_CONTAINS(out, "\"result\":\"moved\"");
    ASSERT_TRUE(platform_dir_exists(dst));
    ASSERT_FALSE(platform_dir_exists(src));

    rm_rf(base);
    return 0;
}

TEST(ln_creates_symlink) {
    char base[NCD_MAX_PATH], ncd_dir[NCD_MAX_PATH];
    char target[NCD_MAX_PATH], link[NCD_MAX_PATH];
    const char test_drive =
#if NCD_PLATFORM_WINDOWS
        'C';
#else
        '/';
#endif

    get_temp_dir(base, sizeof(base), "ln1");
#if NCD_PLATFORM_WINDOWS
    snprintf(ncd_dir, sizeof(ncd_dir), "%s\\NCD", base);
    snprintf(target, sizeof(target), "%s\\TargetDir", base);
    snprintf(link, sizeof(link), "%s\\LinkDir", base);
#else
    snprintf(ncd_dir, sizeof(ncd_dir), "%s/ncd", base);
    snprintf(target, sizeof(target), "%s/TargetDir", base);
    snprintf(link, sizeof(link), "%s/LinkDir", base);
#endif

    rm_rf(base);
    mkdir(base, 0755);
    mkdir(ncd_dir, 0755);
    mkdir(target, 0755);

    ASSERT_TRUE(create_test_db(ncd_dir, test_drive, base));

    char out[4096] = {0};
    char args[NCD_MAX_PATH * 2];
    snprintf(args, sizeof(args), "ln \"%s\" \"%s\" --json", target, link);
    int status = run_agent(base, args, out, sizeof(out));

    if (strstr(out, "EXE_NOT_FOUND")) {
        printf("SKIP: NCD executable not found\n");
        rm_rf(base);
        return 0;
    }

    ASSERT_EQ_INT(0, status);
    ASSERT_STR_CONTAINS(out, "\"result\":\"created\"");

#if NCD_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(link);
    ASSERT_TRUE(attrs != INVALID_FILE_ATTRIBUTES);
    ASSERT_TRUE((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0);
#else
    struct stat st;
    ASSERT_TRUE(lstat(link, &st) == 0 && S_ISLNK(st.st_mode));
#endif

    rm_rf(base);
    return 0;
}

static void kill_any_service(void) {
#if NCD_PLATFORM_WINDOWS
    system("taskkill /F /IM NCDService.exe >nul 2>nul");
#else
    system("pkill -9 -x NCDService 2>/dev/null; killall -9 NCDService 2>/dev/null");
#endif
}

void suite_agent_mv(void) {
    printf("\n=== Agent MV/LN Integration ===\n");
    RUN_TEST(mv_renames_directory);
    RUN_TEST(mv_updates_database);
    RUN_TEST(mv_force_overwrite_empty);
    RUN_TEST(ln_creates_symlink);
}

TEST_MAIN(
    kill_any_service();
    RUN_SUITE(agent_mv);
)
