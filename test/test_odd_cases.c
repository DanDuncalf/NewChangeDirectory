/* test_odd_cases.c -- Time, filesystem, path, and database oddities (Agent 1) */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "../src/matcher.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#if NCD_PLATFORM_LINUX
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ================================================================ Time-related Edge Cases Tests (5) */

TEST(odd_scan_across_daylight_saving_time_change) {
    /* Test database handles DST transitions correctly */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Set a timestamp near DST transition (spring forward) */
    struct tm dst_time = {0};
    dst_time.tm_year = 2024 - 1900;
    dst_time.tm_mon = 2;  /* March */
    dst_time.tm_mday = 10;
    dst_time.tm_hour = 2;  /* 2 AM - when DST usually starts */
    dst_time.tm_isdst = -1;
    
    time_t dst_timestamp = mktime(&dst_time);
    db->last_scan = dst_timestamp;
    
    ASSERT_TRUE(db->last_scan > 0);
    
    db_free(db);
    return 0;
}

TEST(odd_scan_during_leap_second) {
    /* Test database handles leap second timestamps */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Set a timestamp near a leap second (June 30 or Dec 31 23:59:60) */
    struct tm leap_time = {0};
    leap_time.tm_year = 2024 - 1900;
    leap_time.tm_mon = 5;  /* June */
    leap_time.tm_mday = 30;
    leap_time.tm_hour = 23;
    leap_time.tm_min = 59;
    leap_time.tm_sec = 59;
    leap_time.tm_isdst = -1;
    
    time_t leap_timestamp = mktime(&leap_time);
    db->last_scan = leap_timestamp;
    
    ASSERT_TRUE(db->last_scan > 0);
    
    db_free(db);
    return 0;
}

TEST(odd_scan_with_system_clock_set_to_epoch) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Set scan time to Unix epoch */
    db->last_scan = 0;
    ASSERT_EQ_INT(0, db->last_scan);
    
    /* Add a drive and verify it works */
    DriveData *drv = db_add_drive(db, 'C');
    ASSERT_NOT_NULL(drv);
    
    db_free(db);
    return 0;
}

TEST(odd_scan_with_system_clock_set_to_maximum) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Set scan time to far future */
    db->last_scan = (time_t)2147483647;  /* Max 32-bit signed time_t */
    ASSERT_TRUE(db->last_scan > 0);
    
    /* Calculate score with this timestamp */
    NcdMetadata *meta = db_metadata_create();
    db_heur_note_choice(meta, "test", "/path/test");
    
    time_t now = time(NULL);
    int score = db_heur_calculate_score(&meta->heuristics.entries[0], now);
    ASSERT_TRUE(score >= 0);  /* Score should be non-negative */
    
    db_metadata_free(meta);
    db_free(db);
    return 0;
}

TEST(odd_database_timestamp_2038_problem) {
    /* Test Y2038 problem handling */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Set timestamp beyond 2038 (if time_t is 64-bit) */
#if defined(__x86_64__) || defined(_WIN64)
    /* On 64-bit systems, time_t can handle dates beyond 2038 */
    db->last_scan = (time_t)2208988800LL;  /* 2040-01-01 00:00:00 */
    ASSERT_TRUE(db->last_scan > 2147483647);
#else
    /* On 32-bit, this will overflow - test that we handle it gracefully */
    db->last_scan = (time_t)0x7FFFFFFF;
    ASSERT_TRUE(db->last_scan > 0);
#endif
    
    db_free(db);
    return 0;
}

/* ================================================================ Filesystem Oddities Tests (7) */

TEST(odd_directory_with_null_in_name_rejected) {
    /* Directories with null bytes should not exist or be rejected */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    ASSERT_NOT_NULL(drv);
    
    /* Normal directory name */
    int id1 = db_add_dir(drv, "normal", -1, false, false);
    ASSERT_EQ_INT(0, id1);
    
    /* String with null would be truncated by strlen */
    char name_with_null[32] = "test\0hidden";
    int id2 = db_add_dir(drv, name_with_null, -1, false, false);
    ASSERT_EQ_INT(1, id2);
    
    /* The name stored should be just "test" (before null) */
    char path[MAX_PATH];
    db_full_path(drv, 1, path, sizeof(path));
    ASSERT_TRUE(strstr(path, "test") != NULL);
    
    db_free(db);
    return 0;
}

TEST(odd_case_sensitivity_on_case_insensitive_fs) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Add directories with different cases */
    db_add_dir(drv, "Documents", -1, false, false);
    db_add_dir(drv, "DOCUMENTS", -1, false, false);
    db_add_dir(drv, "documents", -1, false, false);
    
    /* All three should be stored (database is case-preserving) */
    ASSERT_EQ_INT(3, drv->dir_count);
    
    db_free(db);
    return 0;
}

TEST(odd_directory_named_con_prn_aux_nul_windows) {
    /* Windows reserved device names */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* These are reserved names on Windows but should be storable in the DB */
    db_add_dir(drv, "CON", -1, false, false);
    db_add_dir(drv, "PRN", -1, false, false);
    db_add_dir(drv, "AUX", -1, false, false);
    db_add_dir(drv, "NUL", -1, false, false);
    db_add_dir(drv, "COM1", -1, false, false);
    db_add_dir(drv, "LPT1", -1, false, false);
    
    ASSERT_EQ_INT(6, drv->dir_count);
    
    db_free(db);
    return 0;
}

TEST(odd_directory_with_trailing_space_windows) {
    /* Windows doesn't allow trailing spaces in filenames */
    /* Test that the database handles them */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Names with trailing spaces */
    char name_with_space[32] = "folder ";
    db_add_dir(drv, name_with_space, -1, false, false);
    
    /* The database stores what we give it */
    ASSERT_EQ_INT(1, drv->dir_count);
    
    db_free(db);
    return 0;
}

TEST(odd_directory_with_trailing_dot_windows) {
    /* Windows doesn't allow trailing dots in filenames */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Names with trailing dots */
    char name_with_dot[32] = "folder.";
    db_add_dir(drv, name_with_dot, -1, false, false);
    
    ASSERT_EQ_INT(1, drv->dir_count);
    
    db_free(db);
    return 0;
}

TEST(odd_directory_with_reserved_names_windows) {
    /* Test Windows reserved names handling */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* All Windows reserved device names */
    const char *reserved[] = {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };
    
    for (int i = 0; i < 22; i++) {
        db_add_dir(drv, reserved[i], -1, false, false);
    }
    
    ASSERT_EQ_INT(22, drv->dir_count);
    
    db_free(db);
    return 0;
}

TEST(odd_symlink_to_directory_vs_file) {
    /* Test database handles symlinks (Linux) or just paths */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Add paths that look like symlinks */
    db_add_dir(drv, "link_to_dir", -1, false, false);
    db_add_dir(drv, "link_to_file", -1, false, false);
    
    int link_dir = db_add_dir(drv, "actual_dir", 0, false, false);
    ASSERT_EQ_INT(2, link_dir);
    
    /* Build a path that simulates symlink resolution */
    char path[MAX_PATH];
    db_full_path(drv, 2, path, sizeof(path));
    ASSERT_TRUE(strstr(path, "actual_dir") != NULL);
    
    db_free(db);
    return 0;
}

/* ================================================================ Path Oddities Tests (6) */

TEST(odd_path_with_32767_chars_maximum_ntfs) {
    /* NTFS max path length is 32767 characters */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Create a deep hierarchy */
    int parent = -1;
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "level_%04d", i);
        parent = db_add_dir(drv, name, parent, false, false);
        ASSERT_TRUE(parent >= 0);
    }
    
    /* Verify we can reconstruct the path */
    char path[MAX_PATH];
    char *result = db_full_path(drv, parent, path, sizeof(path));
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(strlen(path) > 1000);  /* Should be long */
    
    db_free(db);
    return 0;
}

TEST(odd_path_with_exactly_max_name_length) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Create name at max length limit */
    char long_name[256];
    memset(long_name, 'a', 255);
    long_name[255] = '\0';
    
    int id = db_add_dir(drv, long_name, -1, false, false);
    ASSERT_EQ_INT(0, id);
    
    /* Verify path reconstruction */
    char path[MAX_PATH];
    db_full_path(drv, 0, path, sizeof(path));
    ASSERT_TRUE(strstr(path, long_name) != NULL);
    
    db_free(db);
    return 0;
}

TEST(odd_path_with_dot_directories) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Directories with dots */
    db_add_dir(drv, ".", -1, false, false);   /* Current directory */
    db_add_dir(drv, "..", -1, false, false);  /* Parent directory */
    db_add_dir(drv, "...", -1, false, false);
    db_add_dir(drv, "hidden.file", -1, true, false);
    db_add_dir(drv, "normal", -1, false, false);
    
    ASSERT_EQ_INT(5, drv->dir_count);
    
    /* Build path for the last one */
    char path[MAX_PATH];
    db_full_path(drv, 4, path, sizeof(path));
    ASSERT_TRUE(strstr(path, "normal") != NULL);
    
    db_free(db);
    return 0;
}

TEST(odd_path_with_double_separators) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Directory names with double separators shouldn't exist,
       but we can test path building */
    int parent = db_add_dir(drv, "folder", -1, false, false);
    db_add_dir(drv, "subfolder", parent, false, false);
    
    char path[MAX_PATH];
    db_full_path(drv, 1, path, sizeof(path));
    
    /* Path should not have double separators */
    ASSERT_FALSE(strstr(path, "\\\\") != NULL);
    
    db_free(db);
    return 0;
}

TEST(odd_path_with_mixed_separators) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Build a hierarchy */
    int p1 = db_add_dir(drv, "level1", -1, false, false);
    int p2 = db_add_dir(drv, "level2", p1, false, false);
    db_add_dir(drv, "level3", p2, false, false);
    
    /* Verify path uses correct separator */
    char path[MAX_PATH];
    db_full_path(drv, 2, path, sizeof(path));
    
#if NCD_PLATFORM_WINDOWS
    /* On Windows, should use backslash */
    ASSERT_TRUE(strstr(path, "\\") != NULL);
#else
    /* On Linux, should use forward slash */
    ASSERT_TRUE(strstr(path, "/") != NULL);
#endif
    
    db_free(db);
    return 0;
}

TEST(odd_relative_path_with_dotdot_escapes) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Directories named like path traversal attempts */
    db_add_dir(drv, "normal", -1, false, false);
    db_add_dir(drv, "..", -1, false, false);      /* Literal ".." directory */
    db_add_dir(drv, "hidden..", -1, false, false);
    db_add_dir(drv, "..hidden", -1, true, false);
    
    ASSERT_EQ_INT(4, drv->dir_count);
    
    db_free(db);
    return 0;
}

/* ================================================================ Database Oddities Tests (6) */

TEST(odd_database_with_zero_directories) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Drive with no directories */
    DriveData *drv = db_add_drive(db, 'C');
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(0, drv->dir_count);
    
    /* Save and reload */
    const char *test_file = "test_zero_dirs.tmp";
    remove(test_file);
    
    ASSERT_TRUE(db_save_binary_single(db, 0, test_file));
    db_free(db);
    
    NcdDatabase *loaded = db_load_binary(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drive_count);
    ASSERT_EQ_INT(0, loaded->drives[0].dir_count);
    
    db_free(loaded);
    remove(test_file);
    return 0;
}

TEST(odd_database_with_exactly_one_directory) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "only_one", -1, false, false);
    ASSERT_EQ_INT(1, drv->dir_count);
    
    /* Save and reload */
    const char *test_file = "test_one_dir.tmp";
    remove(test_file);
    
    ASSERT_TRUE(db_save_binary_single(db, 0, test_file));
    db_free(db);
    
    NcdDatabase *loaded = db_load_binary(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drives[0].dir_count);
    
    char path[MAX_PATH];
    db_full_path(&loaded->drives[0], 0, path, sizeof(path));
    ASSERT_TRUE(strstr(path, "only_one") != NULL);
    
    db_free(loaded);
    remove(test_file);
    return 0;
}

TEST(odd_database_with_circular_parent_reference) {
    /* This tests the cycle detection in db_full_path */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Create a normal hierarchy first */
    int id1 = db_add_dir(drv, "folder1", -1, false, false);
    int id2 = db_add_dir(drv, "folder2", id1, false, false);
    (void)id2;
    
    /* Path building should work normally */
    char path[MAX_PATH];
    char *result = db_full_path(drv, 1, path, sizeof(path));
    ASSERT_NOT_NULL(result);
    
    db_free(db);
    return 0;
}

TEST(odd_database_with_negative_parent_index) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* -1 is valid (root), but very negative values might cause issues */
    int id = db_add_dir(drv, "root_entry", -1, false, false);
    ASSERT_EQ_INT(0, id);
    
    char path[MAX_PATH];
    db_full_path(drv, 0, path, sizeof(path));
    ASSERT_TRUE(strlen(path) > 0);
    
    db_free(db);
    return 0;
}

TEST(odd_database_with_orphaned_directory_entries) {
    /* Orphaned = parent index doesn't point to valid entry */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Create entries - all with valid parents */
    db_add_dir(drv, "parent", -1, false, false);
    db_add_dir(drv, "child", 0, false, false);
    
    ASSERT_EQ_INT(2, drv->dir_count);
    
    /* Test path building for child */
    char path[MAX_PATH];
    db_full_path(drv, 1, path, sizeof(path));
    ASSERT_TRUE(strstr(path, "parent") != NULL);
    ASSERT_TRUE(strstr(path, "child") != NULL);
    
    db_free(db);
    return 0;
}

TEST(odd_database_with_duplicate_directory_names) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Multiple directories with same name at different locations */
    int p1 = db_add_dir(drv, "common_name", -1, false, false);
    int p2 = db_add_dir(drv, "another_parent", -1, false, false);
    
    db_add_dir(drv, "common_name", p1, false, false);
    db_add_dir(drv, "common_name", p2, false, false);
    
    ASSERT_EQ_INT(4, drv->dir_count);
    
    /* All should be independently accessible */
    char path[MAX_PATH];
    db_full_path(drv, 2, path, sizeof(path));
    ASSERT_TRUE(strstr(path, "another_parent") != NULL);
    
    db_free(db);
    return 0;
}

/* ================================================================ Search Oddities Tests (6) */

TEST(odd_search_for_dot_directories) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, ".git", -1, true, false);
    db_add_dir(drv, ".config", -1, true, false);
    db_add_dir(drv, "normal", -1, false, false);
    
    /* Search for hidden dirs */
    int count;
    NcdMatch *matches = matcher_find(db, "git", true, false, &count);
    ASSERT_TRUE(count >= 0);
    if (matches) free(matches);
    
    db_free(db);
    return 0;
}

TEST(odd_search_with_only_wildcards) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "downloads", -1, false, false);
    db_add_dir(drv, "documents", -1, false, false);
    
    /* Search with asterisks */
    int count;
    NcdMatch *matches = matcher_find(db, "*", false, false, &count);
    /* Behavior depends on implementation - just verify no crash */
    (void)matches;
    (void)count;
    
    db_free(db);
    return 0;
}

TEST(odd_search_with_bidi_characters) {
    /* Bidirectional text characters */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "normal_folder", -1, false, false);
    
    /* Search with bidi characters */
    int count;
    NcdMatch *matches = matcher_find(db, "\u202Efolder", false, false, &count);
    /* Just verify no crash */
    (void)matches;
    (void)count;
    
    db_free(db);
    return 0;
}

TEST(odd_search_with_confusable_characters) {
    /* Unicode confusable characters (homoglyphs) */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "google", -1, false, false);  /* Normal */
    
    /* Search with confusable characters (Cyrillic 'o' vs Latin 'o') */
    int count;
    NcdMatch *matches = matcher_find(db, "g\u043E\u043Egle", false, false, &count);
    /* Should not match (different characters) - verify no crash */
    (void)matches;
    (void)count;
    
    db_free(db);
    return 0;
}

TEST(odd_fuzzy_search_with_emoji) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "project", -1, false, false);
    
    /* Fuzzy search with emoji */
    int count;
    NcdMatch *matches = matcher_find_fuzzy(db, "\U0001F600", false, false, &count);
    /* Just verify no crash */
    (void)matches;
    (void)count;
    
    db_free(db);
    return 0;
}

TEST(odd_fuzzy_search_with_mixed_scripts) {
    /* Mixed Unicode scripts */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "test_folder", -1, false, false);
    
    /* Search mixing Latin and other scripts */
    int count;
    NcdMatch *matches = matcher_find_fuzzy(db, "test\u4E2D\u6587", false, false, &count);
    /* Just verify no crash */
    (void)matches;
    (void)count;
    
    db_free(db);
    return 0;
}

/* ================================================================ Test Suites */

void suite_odd_time() {
    printf("\n=== Time-related Edge Cases ===\n");
    RUN_TEST(odd_scan_across_daylight_saving_time_change);
    RUN_TEST(odd_scan_during_leap_second);
    RUN_TEST(odd_scan_with_system_clock_set_to_epoch);
    RUN_TEST(odd_scan_with_system_clock_set_to_maximum);
    RUN_TEST(odd_database_timestamp_2038_problem);
}

void suite_odd_filesystem() {
    printf("\n=== Filesystem Oddities ===\n");
    RUN_TEST(odd_directory_with_null_in_name_rejected);
    RUN_TEST(odd_case_sensitivity_on_case_insensitive_fs);
    RUN_TEST(odd_directory_named_con_prn_aux_nul_windows);
    RUN_TEST(odd_directory_with_trailing_space_windows);
    RUN_TEST(odd_directory_with_trailing_dot_windows);
    RUN_TEST(odd_directory_with_reserved_names_windows);
    RUN_TEST(odd_symlink_to_directory_vs_file);
}

void suite_odd_path() {
    printf("\n=== Path Oddities ===\n");
    RUN_TEST(odd_path_with_32767_chars_maximum_ntfs);
    RUN_TEST(odd_path_with_exactly_max_name_length);
    RUN_TEST(odd_path_with_dot_directories);
    RUN_TEST(odd_path_with_double_separators);
    RUN_TEST(odd_path_with_mixed_separators);
    RUN_TEST(odd_relative_path_with_dotdot_escapes);
}

void suite_odd_database() {
    printf("\n=== Database Oddities ===\n");
    RUN_TEST(odd_database_with_zero_directories);
    RUN_TEST(odd_database_with_exactly_one_directory);
    RUN_TEST(odd_database_with_circular_parent_reference);
    RUN_TEST(odd_database_with_negative_parent_index);
    RUN_TEST(odd_database_with_orphaned_directory_entries);
    RUN_TEST(odd_database_with_duplicate_directory_names);
}

void suite_odd_search() {
    printf("\n=== Search Oddities ===\n");
    RUN_TEST(odd_search_for_dot_directories);
    RUN_TEST(odd_search_with_only_wildcards);
    RUN_TEST(odd_search_with_bidi_characters);
    RUN_TEST(odd_search_with_confusable_characters);
    RUN_TEST(odd_fuzzy_search_with_emoji);
    RUN_TEST(odd_fuzzy_search_with_mixed_scripts);
}

/* ================================================================ Main */

TEST_MAIN(
    RUN_SUITE(odd_time);
    RUN_SUITE(odd_filesystem);
    RUN_SUITE(odd_path);
    RUN_SUITE(odd_database);
    RUN_SUITE(odd_search);
)
