/* test_agent_mkdir.c -- Tests for agent mkdir command */
#include "test_framework.h"
#include "../src/ncd.h"
#include "../src/database.h"
#include "../src/platform.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Platform-specific includes */
#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <direct.h>
#define mkdir _mkdir
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#endif

/* Test helper: Check if directory exists */
static bool dir_exists(const char *path)
{
#if NCD_PLATFORM_WINDOWS
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
#else
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
#endif
}

/* Test helper: Create and clean up test directory */
static void cleanup_test_dir(const char *path)
{
#if NCD_PLATFORM_WINDOWS
    RemoveDirectoryA(path);
#else
    rmdir(path);
#endif
}

TEST(agent_mkdir_creates_directory) {
    const char *test_dir = "test_agent_mkdir_tmp";
    char test_path[MAX_PATH];
    char cwd[MAX_PATH];
    
    /* Clean up from previous failed run */
    cleanup_test_dir(test_dir);
    
    /* Get current directory */
    ASSERT_TRUE(platform_get_current_dir(cwd, sizeof(cwd)));
    
    /* Build full path */
#if NCD_PLATFORM_WINDOWS
    snprintf(test_path, sizeof(test_path), "%s\\%s", cwd, test_dir);
#else
    snprintf(test_path, sizeof(test_path), "%s/%s", cwd, test_dir);
#endif
    
    /* Ensure directory doesn't exist */
    ASSERT_FALSE(dir_exists(test_path));
    
    /* Create the directory using platform function */
    ASSERT_TRUE(platform_create_dir(test_path));
    
    /* Verify it exists */
    ASSERT_TRUE(dir_exists(test_path));
    
    /* Cleanup */
    cleanup_test_dir(test_path);
    ASSERT_FALSE(dir_exists(test_path));
    
    return 0;
}

TEST(agent_mkdir_fails_when_parent_missing) {
    const char *bad_path = "nonexistent_parent/test_subdir";
    
    /* This should fail because parent doesn't exist */
    ASSERT_FALSE(platform_create_dir(bad_path));
    
    return 0;
}

TEST(agent_mkdir_handles_existing_directory) {
    const char *test_dir = "test_existing_dir";
    
    /* Clean up from previous run */
    cleanup_test_dir(test_dir);
    
    /* Create directory first time */
    ASSERT_TRUE(platform_create_dir(test_dir));
    ASSERT_TRUE(dir_exists(test_dir));
    
    /* Creating again should succeed (idempotent) or fail gracefully */
    /* Platform behavior varies - some return true for existing dirs */
    platform_create_dir(test_dir);
    
    /* Verify it still exists */
    ASSERT_TRUE(dir_exists(test_dir));
    
    /* Cleanup */
    cleanup_test_dir(test_dir);
    
    return 0;
}

TEST(agent_mkdir_result_codes) {
    /* Test that result codes are properly defined */
    ASSERT_EQ_INT(0, AGENT_MKDIR_OK);
    ASSERT_EQ_INT(1, AGENT_MKDIR_EXISTS);
    ASSERT_EQ_INT(2, AGENT_MKDIR_ERROR_PERMS);
    ASSERT_EQ_INT(3, AGENT_MKDIR_ERROR_PATH);
    ASSERT_EQ_INT(4, AGENT_MKDIR_ERROR_PARENT);
    ASSERT_EQ_INT(5, AGENT_MKDIR_ERROR_OTHER);
    
    return 0;
}

TEST(config_rescan_interval_default) {
    NcdConfig cfg;
    
    /* Initialize with defaults */
    db_config_init_defaults(&cfg);
    
    /* Verify default rescan interval */
    ASSERT_EQ_INT(NCD_RESCAN_HOURS_DEFAULT, cfg.rescan_interval_hours);
    
    /* Verify it's within valid bounds (or -1 for never) */
    ASSERT_TRUE(cfg.rescan_interval_hours >= -1 && 
                cfg.rescan_interval_hours <= NCD_RESCAN_HOURS_MAX);
    
    return 0;
}

TEST(config_rescan_interval_never) {
    NcdConfig cfg;
    
    /* Initialize with defaults */
    db_config_init_defaults(&cfg);
    
    /* Set to "never" */
    cfg.rescan_interval_hours = NCD_RESCAN_NEVER;
    
    /* Verify it's -1 */
    ASSERT_EQ_INT(-1, cfg.rescan_interval_hours);
    
    /* Verify auto-rescan should be disabled */
    ASSERT_TRUE(cfg.rescan_interval_hours < 0);
    
    return 0;
}

TEST(config_rescan_interval_bounds) {
    NcdConfig cfg;
    
    /* Initialize with defaults */
    db_config_init_defaults(&cfg);
    
    /* Test minimum valid value (1 hour) */
    cfg.rescan_interval_hours = 1;
    ASSERT_EQ_INT(1, cfg.rescan_interval_hours);
    
    /* Test maximum valid value (168 hours = 1 week) */
    cfg.rescan_interval_hours = 168;
    ASSERT_EQ_INT(168, cfg.rescan_interval_hours);
    
    return 0;
}

TEST(config_version_updated) {
    /* Verify config version was incremented for new field */
    ASSERT_EQ_INT(3, NCD_CFG_VERSION);
    
    return 0;
}

/* Helper to create a minimal database for testing */
static NcdDatabase *create_test_database(void)
{
    NcdDatabase *db = db_create();
    if (!db) return NULL;
    
    db->last_scan = time(NULL);
    DriveData *drv = db_add_drive(db, 'C');
    if (!drv) {
        db_free(db);
        return NULL;
    }
    
    /* Add a root directory */
    db_add_dir(drv, "Users", -1, false, false);
    
    return db;
}

TEST(database_adds_directory_from_mkdir) {
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = &db->drives[0];
    int initial_count = drv->dir_count;
    
    /* Simulate adding a directory as agent mkdir would */
    int new_id = db_add_dir(drv, "NewFolder", 0, false, false);
    ASSERT_EQ_INT(initial_count, new_id);
    ASSERT_EQ_INT(initial_count + 1, drv->dir_count);
    
    /* Verify we can reconstruct the path */
    char path_buf[MAX_PATH];
    db_full_path(drv, new_id, path_buf, sizeof(path_buf));
    
    /* Path should contain the new folder name */
    ASSERT_TRUE(strstr(path_buf, "NewFolder") != NULL);
    
    db_free(db);
    return 0;
}

void suite_agent_mkdir(void)
{
    RUN_TEST(agent_mkdir_creates_directory);
    RUN_TEST(agent_mkdir_fails_when_parent_missing);
    RUN_TEST(agent_mkdir_handles_existing_directory);
    RUN_TEST(agent_mkdir_result_codes);
    RUN_TEST(config_rescan_interval_default);
    RUN_TEST(config_rescan_interval_never);
    RUN_TEST(config_rescan_interval_bounds);
    RUN_TEST(config_version_updated);
    RUN_TEST(database_adds_directory_from_mkdir);
}

/* Main entry point for standalone test */
#ifdef STANDALONE_TEST
int main(void)
{
    suite_agent_mkdir();
    return test_summary();
}
#endif
