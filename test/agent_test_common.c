/* agent_test_common.c -- Shared agent test utilities (implementations) */

#include "agent_test_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>

/* ==================================================================
 * Temp directory helpers
 * ================================================================== */

void agent_get_temp_dir(char *buf, size_t size, const char *suffix) {
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\ncd_agent_%s", tmp, suffix);
#else
    snprintf(buf, size, "%s/ncd_agent_%s", tmp, suffix);
#endif
}

void agent_build_ncd_dir(char *buf, size_t size, const char *base) {
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\NCD", base);
#else
    snprintf(buf, size, "%s/ncd", base);
#endif
}

void agent_rm_rf(const char *path) {
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

bool agent_dir_exists(const char *path) {
#if NCD_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

/* ==================================================================
 * NCD executable discovery
 * ================================================================== */

const char *agent_find_exe(void) {
#if NCD_PLATFORM_WINDOWS
    if (GetFileAttributesA("..\\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES)
        return "..\\NewChangeDirectory.exe";
    if (GetFileAttributesA(".\\NewChangeDirectory.exe") != INVALID_FILE_ATTRIBUTES)
        return ".\\NewChangeDirectory.exe";
#else
    if (access("../NewChangeDirectory", X_OK) == 0) return "../NewChangeDirectory";
    if (access("./NewChangeDirectory", X_OK) == 0) return "./NewChangeDirectory";
#endif
    return NULL;
}

/* ==================================================================
 * Agent subprocess execution
 * ================================================================== */

void agent_build_db_path(char *buf, size_t size, const char *ncd_dir, char drive_letter) {
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\ncd_%c.database", ncd_dir, toupper((unsigned char)drive_letter));
#else
    snprintf(buf, size, "%s/ncd_%02x.database", ncd_dir, (unsigned char)drive_letter);
#endif
}

int agent_run(const char *ncd_dir, const char *agent_args, char *out, size_t out_size,
              const char *db_path) {
    const char *exe = agent_find_exe();
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
    FILE *fp = AGENT_POPEN(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    return AGENT_PCLOSE(fp);
}

void agent_kill_any_service(void) {
#if NCD_PLATFORM_WINDOWS
    system("taskkill /F /IM NCDService.exe >nul 2>nul");
#else
    system("pkill -9 -x NCDService 2>/dev/null; pkill -9 -x ncd_service 2>/dev/null; killall -9 NCDService 2>/dev/null; killall -9 ncd_service 2>/dev/null");
#endif
}

/* ==================================================================
 * Test database helpers
 * ================================================================== */

char agent_test_drive_letter(void) {
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

void agent_build_test_drive_root(char *buf, size_t size) {
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%c:\\", agent_test_drive_letter());
#else
    char drive = agent_test_drive_letter();
    if (platform_build_mount_path(drive, buf, size)) {
        return;
    }
    platform_strncpy_s(buf, size, "/");
#endif
}

bool agent_create_test_db(const char *ncd_dir, char drive_letter) {
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
    /* Build a known test tree */
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

void agent_make_fs_tree(const char *base) {
    char path[NCD_MAX_PATH];
    AGENT_MKDIR(base, 0755);
#if NCD_PLATFORM_WINDOWS
    snprintf(path, sizeof(path), "%s\\subdir", base);
    AGENT_MKDIR(path, 0755);
    snprintf(path, sizeof(path), "%s\\subdir\\nested", base);
    AGENT_MKDIR(path, 0755);
    snprintf(path, sizeof(path), "%s\\file1.txt", base);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
    snprintf(path, sizeof(path), "%s\\subdir\\file2.log", base);
    f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
#else
    snprintf(path, sizeof(path), "%s/subdir", base);
    AGENT_MKDIR(path, 0755);
    snprintf(path, sizeof(path), "%s/subdir/nested", base);
    AGENT_MKDIR(path, 0755);
    snprintf(path, sizeof(path), "%s/file1.txt", base);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
    snprintf(path, sizeof(path), "%s/subdir/file2.log", base);
    f = fopen(path, "w");
    if (f) { fprintf(f, "test"); fclose(f); }
#endif
}
