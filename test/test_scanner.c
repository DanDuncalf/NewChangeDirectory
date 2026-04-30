/* test_scanner.c -- Tests for scanner module */
#include "test_framework.h"
#include "../src/scanner.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#if NCD_PLATFORM_WINDOWS
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

/* Forward declarations for helpers defined later in this file */
static void rm_rf(const char *path);
static bool make_dir(const char *path);

TEST(scan_mount_populates_database) {
    /* Create a temporary directory to scan */
    const char *test_dir = "test_scan_temp";
    
    /* Clean up any previous test residue */
#if NCD_PLATFORM_WINDOWS
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
    /* Create test directory */
#if NCD_PLATFORM_WINDOWS
    if (_mkdir(test_dir) != 0) {
#else
    if (mkdir(test_dir, 0755) != 0) {
#endif
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create database and scan */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Scan the test directory */
    int count = scan_mount(db, test_dir, false, false, NULL, NULL, NULL);
    
    /* Should find at least the test directory itself */
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    
    /* Cleanup */
#if NCD_PLATFORM_WINDOWS
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
    return 0;
}

TEST(scan_mount_respects_hidden_flag) {
    const char *test_dir = "test_scan_hidden";
    
    /* Clean up */
#if NCD_PLATFORM_WINDOWS
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
    /* Create test directory */
#if NCD_PLATFORM_WINDOWS
    if (_mkdir(test_dir) != 0) {
#else
    if (mkdir(test_dir, 0755) != 0) {
#endif
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Scan without hidden flag */
    NcdDatabase *db_no_hidden = db_create();
    int count_no_hidden = scan_mount(db_no_hidden, test_dir, false, false, NULL, NULL, NULL);
    
    /* Scan with hidden flag */
    NcdDatabase *db_with_hidden = db_create();
    int count_with_hidden = scan_mount(db_with_hidden, test_dir, true, false, NULL, NULL, NULL);
    
    /* With hidden flag should find at least as many, possibly more */
    ASSERT_TRUE(count_with_hidden >= count_no_hidden);
    
    db_free(db_no_hidden);
    db_free(db_with_hidden);
    
    /* Cleanup */
#if NCD_PLATFORM_WINDOWS
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
    return 0;
}

TEST(scan_mount_applies_exclusions) {
    const char *test_dir = "test_scan_exclude";
    
#if NCD_PLATFORM_WINDOWS
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
#if NCD_PLATFORM_WINDOWS
    if (_mkdir(test_dir) != 0) {
#else
    if (mkdir(test_dir, 0755) != 0) {
#endif
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create exclusion list */
    NcdExclusionList exclusions;
    memset(&exclusions, 0, sizeof(exclusions));
    exclusions.entries = (NcdExclusionEntry *)ncd_malloc(sizeof(NcdExclusionEntry) * 2);
    exclusions.capacity = 2;
    
    /* Add exclusion pattern */
    strncpy(exclusions.entries[0].pattern, "*/exclude", sizeof(exclusions.entries[0].pattern));
    exclusions.entries[0].drive = 0;
    exclusions.entries[0].match_from_root = false;
    exclusions.entries[0].has_parent_match = false;
    exclusions.count = 1;
    
    /* Scan with exclusions */
    NcdDatabase *db = db_create();
    int count = scan_mount(db, test_dir, true, true, NULL, NULL, &exclusions);
    
    /* Should succeed */
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    free(exclusions.entries);
    
    /* Cleanup */
#if NCD_PLATFORM_WINDOWS
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
    return 0;
}

TEST(scan_subdirectory_preserves_sibling_directories) {
    char cwd[MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));

    char test_dir[MAX_PATH];
    path_join(test_dir, sizeof(test_dir), cwd, "test_scan_partial_keep");

    rm_rf(test_dir);

    char keep_dir[MAX_PATH];
    path_join(keep_dir, sizeof(keep_dir), test_dir, "keep_dir");
    char rescan_dir[MAX_PATH];
    path_join(rescan_dir, sizeof(rescan_dir), test_dir, "rescan_dir");

    if (!make_dir(test_dir) || !make_dir(keep_dir) || !make_dir(rescan_dir)) {
        rm_rf(test_dir);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }

    NcdDatabase *db = db_create();
    int count1 = scan_mount(db, test_dir, true, true, NULL, NULL, NULL);
    ASSERT_TRUE(count1 >= 2);
    ASSERT_EQ_INT(1, db->drive_count);
    char drive_letter = db->drives[0].letter;

    /* Verify both directories exist before partial rescan */
    bool found_keep = false, found_rescan = false;
    for (int i = 0; i < db->drives[0].dir_count; i++) {
        char fp[NCD_MAX_PATH];
        db_full_path(&db->drives[0], i, fp, sizeof(fp));
        if (strstr(fp, "keep_dir")) found_keep = true;
        if (strstr(fp, "rescan_dir")) found_rescan = true;
    }
    ASSERT_TRUE(found_keep);
    ASSERT_TRUE(found_rescan);

    /* Rescan just rescan_dir */
    int count2 = scan_subdirectory(db, drive_letter, rescan_dir, true, true, NULL);
    ASSERT_TRUE(count2 >= 0);

    /* Verify keep_dir still exists after partial rescan */
    found_keep = false; found_rescan = false;
    for (int i = 0; i < db->drives[0].dir_count; i++) {
        char fp[NCD_MAX_PATH];
        db_full_path(&db->drives[0], i, fp, sizeof(fp));
        if (strstr(fp, "keep_dir")) found_keep = true;
        if (strstr(fp, "rescan_dir")) found_rescan = true;
    }
    ASSERT_TRUE(found_keep);
    ASSERT_TRUE(found_rescan);

    db_free(db);
    rm_rf(test_dir);
    return 0;
}

TEST(scan_subdirectory_updates_existing_subtree) {
    char cwd[MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));

    char test_dir[MAX_PATH];
    path_join(test_dir, sizeof(test_dir), cwd, "test_scan_partial_update");

    rm_rf(test_dir);

    char keep_dir[MAX_PATH];
    path_join(keep_dir, sizeof(keep_dir), test_dir, "keep_dir");
    char nested_keep[MAX_PATH];
    path_join(nested_keep, sizeof(nested_keep), keep_dir, "nested_keep");
    char rescan_dir[MAX_PATH];
    path_join(rescan_dir, sizeof(rescan_dir), test_dir, "rescan_dir");
    char old_child[MAX_PATH];
    path_join(old_child, sizeof(old_child), rescan_dir, "old_child");

    if (!make_dir(test_dir) || !make_dir(keep_dir) || !make_dir(nested_keep) ||
        !make_dir(rescan_dir) || !make_dir(old_child)) {
        rm_rf(test_dir);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }

    NcdDatabase *db = db_create();
    int count1 = scan_mount(db, test_dir, true, true, NULL, NULL, NULL);
    ASSERT_TRUE(count1 >= 4);
    ASSERT_EQ_INT(1, db->drive_count);
    char drive_letter = db->drives[0].letter;

    /* Verify initial state */
    bool found_old = false, found_keep = false, found_nested = false;
    for (int i = 0; i < db->drives[0].dir_count; i++) {
        char fp[NCD_MAX_PATH];
        db_full_path(&db->drives[0], i, fp, sizeof(fp));
        if (strstr(fp, "old_child")) found_old = true;
        if (strstr(fp, "keep_dir")) found_keep = true;
        if (strstr(fp, "nested_keep")) found_nested = true;
    }
    ASSERT_TRUE(found_old);
    ASSERT_TRUE(found_keep);
    ASSERT_TRUE(found_nested);

    /* Modify filesystem: replace old_child with new_child */
#if NCD_PLATFORM_WINDOWS
    _rmdir(old_child);
#else
    rmdir(old_child);
#endif
    char new_child[MAX_PATH];
    path_join(new_child, sizeof(new_child), rescan_dir, "new_child");
    make_dir(new_child);

    /* Rescan just rescan_dir */
    int count2 = scan_subdirectory(db, drive_letter, rescan_dir, true, true, NULL);
    ASSERT_TRUE(count2 >= 0);

    /* Verify post-rescan state */
    found_old = false; found_keep = false; found_nested = false;
    bool found_new = false;
    for (int i = 0; i < db->drives[0].dir_count; i++) {
        char fp[NCD_MAX_PATH];
        db_full_path(&db->drives[0], i, fp, sizeof(fp));
        if (strstr(fp, "old_child")) found_old = true;
        if (strstr(fp, "new_child")) found_new = true;
        if (strstr(fp, "keep_dir")) found_keep = true;
        if (strstr(fp, "nested_keep")) found_nested = true;
    }
    ASSERT_FALSE(found_old);   /* old_child should be gone */
    ASSERT_TRUE(found_new);    /* new_child should be present */
    ASSERT_TRUE(found_keep);   /* keep_dir should still be there */
    ASSERT_TRUE(found_nested); /* nested_keep should still be there */

    db_free(db);
    rm_rf(test_dir);
    return 0;
}

TEST(scan_subdirectory_adds_new_subtree) {
    char cwd[MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));

    char test_dir[MAX_PATH];
    path_join(test_dir, sizeof(test_dir), cwd, "test_scan_partial_add");

    rm_rf(test_dir);

    char keep_dir[MAX_PATH];
    path_join(keep_dir, sizeof(keep_dir), test_dir, "keep_dir");

    if (!make_dir(test_dir) || !make_dir(keep_dir)) {
        rm_rf(test_dir);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }

    NcdDatabase *db = db_create();
    int count1 = scan_mount(db, test_dir, true, true, NULL, NULL, NULL);
    ASSERT_TRUE(count1 >= 1);
    ASSERT_EQ_INT(1, db->drive_count);
    char drive_letter = db->drives[0].letter;

    /* Verify keep_dir exists */
    bool found_keep = false;
    for (int i = 0; i < db->drives[0].dir_count; i++) {
        char fp[NCD_MAX_PATH];
        db_full_path(&db->drives[0], i, fp, sizeof(fp));
        if (strstr(fp, "keep_dir")) found_keep = true;
    }
    ASSERT_TRUE(found_keep);

    /* Create new_dir on filesystem */
    char new_dir[MAX_PATH];
    path_join(new_dir, sizeof(new_dir), test_dir, "new_dir");
    make_dir(new_dir);

    /* Rescan the new directory */
    int count2 = scan_subdirectory(db, drive_letter, new_dir, true, true, NULL);
    ASSERT_TRUE(count2 >= 0);

    /* Verify both keep_dir and new_dir exist */
    bool found_new = false;
    found_keep = false;
    for (int i = 0; i < db->drives[0].dir_count; i++) {
        char fp[NCD_MAX_PATH];
        db_full_path(&db->drives[0], i, fp, sizeof(fp));
        if (strstr(fp, "keep_dir")) found_keep = true;
        if (strstr(fp, "new_dir")) found_new = true;
    }
    ASSERT_TRUE(found_keep);
    ASSERT_TRUE(found_new);

    db_free(db);
    rm_rf(test_dir);
    return 0;
}

TEST(find_is_directory_returns_true_for_dirs) {
    /* Test the find_is_directory helper function */
    PlatformFindData fd;
    memset(&fd, 0, sizeof(fd));
    
    /* Set directory attribute */
#if NCD_PLATFORM_WINDOWS
    fd.attrs = FILE_ATTRIBUTE_DIRECTORY;
#else
    fd.attrs = (1UL << 0);
#endif
    
    ASSERT_TRUE(find_is_directory(&fd));
    
    /* Clear directory attribute */
    fd.attrs = 0;
    ASSERT_FALSE(find_is_directory(&fd));
    
    return 0;
}

TEST(find_is_hidden_detects_hidden_entries) {
    PlatformFindData fd;
    memset(&fd, 0, sizeof(fd));
    
    /* Set hidden attribute */
#if NCD_PLATFORM_WINDOWS
    fd.attrs = FILE_ATTRIBUTE_HIDDEN;
#else
    fd.attrs = (1UL << 1);
#endif
    
    ASSERT_TRUE(find_is_hidden(&fd));
    
    /* Without hidden attribute */
    fd.attrs = 0;
    ASSERT_FALSE(find_is_hidden(&fd));
    
    return 0;
}

TEST(find_is_reparse_detects_symlinks) {
    PlatformFindData fd;
    memset(&fd, 0, sizeof(fd));
    
    /* Set reparse/link attribute */
#if NCD_PLATFORM_WINDOWS
    fd.attrs = FILE_ATTRIBUTE_REPARSE_POINT;
#else
    fd.attrs = (1UL << 3);
#endif
    
    ASSERT_TRUE(find_is_reparse(&fd));
    
    /* Without reparse attribute */
    fd.attrs = 0;
    ASSERT_FALSE(find_is_reparse(&fd));
    
    return 0;
}

TEST(scan_mounts_handles_empty_mount_list) {
    /* Test scanning with empty mount list */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Pass empty mounts array */
    const char *mounts[] = {NULL};
    int count = scan_mounts(db, mounts, 0, true, true, 60, NULL);
    
    /* Should return 0 for empty mount list */
    ASSERT_EQ_INT(0, count);
    ASSERT_EQ_INT(0, db->drive_count);
    
    db_free(db);
    return 0;
}

/* Helper to recursively remove directory tree */
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

/* Helper to create directory */
static bool make_dir(const char *path) {
#if NCD_PLATFORM_WINDOWS
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0755) == 0;
#endif
}

TEST(scan_mount_excludes_directories_from_database) {
    const char *test_dir = "test_scan_exclusion_db";
    
    /* Clean up any previous test residue */
    rm_rf(test_dir);
    
    /* Create test directory structure:
     * test_scan_exclusion_db/
     *   include_dir/
     *     nested_include/
     *   exclude_dir/
     *     nested_exclude/
     */
    if (!make_dir(test_dir)) {
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/include_dir", test_dir);
    if (!make_dir(path)) {
        rm_rf(test_dir);
        printf("    SKIPPED (cannot create include_dir)\n");
        return 0;
    }
    
    snprintf(path, sizeof(path), "%s/include_dir/nested_include", test_dir);
    if (!make_dir(path)) {
        rm_rf(test_dir);
        printf("    SKIPPED (cannot create nested_include)\n");
        return 0;
    }
    
    snprintf(path, sizeof(path), "%s/exclude_dir", test_dir);
    if (!make_dir(path)) {
        rm_rf(test_dir);
        printf("    SKIPPED (cannot create exclude_dir)\n");
        return 0;
    }
    
    snprintf(path, sizeof(path), "%s/exclude_dir/nested_exclude", test_dir);
    if (!make_dir(path)) {
        rm_rf(test_dir);
        printf("    SKIPPED (cannot create nested_exclude)\n");
        return 0;
    }
    
    /* Create exclusion list that excludes "exclude_dir" */
    NcdExclusionList exclusions;
    memset(&exclusions, 0, sizeof(exclusions));
    exclusions.entries = (NcdExclusionEntry *)ncd_malloc(sizeof(NcdExclusionEntry) * 2);
    exclusions.capacity = 2;
    
    strncpy(exclusions.entries[0].pattern, "*/exclude_dir", sizeof(exclusions.entries[0].pattern));
    exclusions.entries[0].drive = 0;
    exclusions.entries[0].match_from_root = false;
    exclusions.entries[0].has_parent_match = false;
    exclusions.count = 1;
    
    /* Scan with exclusions */
    NcdDatabase *db = db_create();
    int count = scan_mount(db, test_dir, true, true, NULL, NULL, &exclusions);
    
    /* Should succeed and find some directories */
    ASSERT_TRUE(count > 0);
    
    /* Verify that the test drive was added */
    ASSERT_EQ_INT(1, db->drive_count);
    DriveData *drv = &db->drives[0];
    
    /* Build path map to check for specific directories */
    bool found_include_dir = false;
    bool found_nested_include = false;
    bool found_exclude_dir = false;
    bool found_nested_exclude = false;
    
    for (int i = 0; i < drv->dir_count; i++) {
        char full_path[NCD_MAX_PATH];
        db_full_path(drv, i, full_path, sizeof(full_path));
        
        /* Check if path contains our test directories */
        if (strstr(full_path, "include_dir")) {
            found_include_dir = true;
        }
        if (strstr(full_path, "nested_include")) {
            found_nested_include = true;
        }
        if (strstr(full_path, "exclude_dir")) {
            found_exclude_dir = true;
        }
        if (strstr(full_path, "nested_exclude")) {
            found_nested_exclude = true;
        }
    }
    
    /* Verify included directories ARE in database */
    ASSERT_TRUE(found_include_dir);
    ASSERT_TRUE(found_nested_include);
    
    /* Verify excluded directories are NOT in database */
    ASSERT_FALSE(found_exclude_dir);
    ASSERT_FALSE(found_nested_exclude);
    
    db_free(db);
    free(exclusions.entries);
    
    /* Cleanup */
    rm_rf(test_dir);
    
    return 0;
}

/* Test suites */
void suite_scanner(void) {
    RUN_TEST(scan_mount_populates_database);
    RUN_TEST(scan_mount_respects_hidden_flag);
    RUN_TEST(scan_mount_applies_exclusions);
    RUN_TEST(scan_mount_excludes_directories_from_database);
    RUN_TEST(scan_subdirectory_preserves_sibling_directories);
    RUN_TEST(scan_subdirectory_updates_existing_subtree);
    RUN_TEST(scan_subdirectory_adds_new_subtree);
    RUN_TEST(find_is_directory_returns_true_for_dirs);
    RUN_TEST(find_is_hidden_detects_hidden_entries);
    RUN_TEST(find_is_reparse_detects_symlinks);
    RUN_TEST(scan_mounts_handles_empty_mount_list);
}

/* Main */
TEST_MAIN(
    RUN_SUITE(scanner);
)
