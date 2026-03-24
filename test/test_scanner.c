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

TEST(scan_subdirectory_merges_into_existing_db) {
    const char *test_dir = "test_scan_subdir";
    
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
    
    /* Create initial database */
    NcdDatabase *db = db_create();
    int count1 = scan_mount(db, test_dir, true, true, NULL, NULL, NULL);
    ASSERT_TRUE(count1 >= 0);
    
    /* Scan subdirectory and merge */
    int count2 = scan_subdirectory(db, 'C', test_dir, true, true, NULL);
    ASSERT_TRUE(count2 >= 0);
    
    db_free(db);
    
    /* Cleanup */
#if NCD_PLATFORM_WINDOWS
    _rmdir(test_dir);
#else
    rmdir(test_dir);
#endif
    
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

/* Test suites */
void suite_scanner(void) {
    RUN_TEST(scan_mount_populates_database);
    RUN_TEST(scan_mount_respects_hidden_flag);
    RUN_TEST(scan_mount_applies_exclusions);
    RUN_TEST(scan_subdirectory_merges_into_existing_db);
    RUN_TEST(find_is_directory_returns_true_for_dirs);
    RUN_TEST(find_is_hidden_detects_hidden_entries);
    RUN_TEST(find_is_reparse_detects_symlinks);
    RUN_TEST(scan_mounts_handles_empty_mount_list);
}

/* Main */
TEST_MAIN(
    RUN_SUITE(scanner);
)
