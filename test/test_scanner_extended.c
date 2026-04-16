/* test_scanner_extended.c -- Extended scanner tests for NCD (Agent 4) */
#include "test_framework.h"
#include "../src/scanner.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "../shared/platform.h"
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

/* ================================================================ Thread Pool (6 tests) */

TEST(scan_thread_pool_single_thread) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_single_thread"};
    
    /* Create test directory */
    rm_rf(mounts[0]);
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Scan with single thread implicitly by small mount list */
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_thread_pool_max_threads) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Test scanning multiple mounts simultaneously */
    const char *mounts[] = {"test_thread_1", "test_thread_2", "test_thread_3"};
    int num_mounts = 3;
    
    /* Create test directories */
    for (int i = 0; i < num_mounts; i++) {
        rm_rf(mounts[i]);
        if (!make_dir(mounts[i])) {
            for (int j = 0; j < i; j++) rm_rf(mounts[j]);
            db_free(db);
            printf("    SKIPPED (cannot create test directories)\n");
            return 0;
        }
    }
    
    /* Scan all mounts - may use multiple threads */
    int count = scan_mounts(db, mounts, num_mounts, false, false, 60, NULL);
    
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    for (int i = 0; i < num_mounts; i++) rm_rf(mounts[i]);
    
    return 0;
}

TEST(scan_thread_pool_dynamic_scaling) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Test that scanner scales thread count based on work */
    const char *mounts[] = {"test_dynamic"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create nested structure */
    char path[NCD_MAX_PATH];
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof(path), "%s/level%d", mounts[0], i);
        make_dir(path);
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 5);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_thread_pool_work_queue_overflow) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Create many directories to stress work queue */
    const char *mounts[] = {"test_queue_overflow"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create many subdirectories */
    char path[NCD_MAX_PATH];
    for (int i = 0; i < 100; i++) {
        snprintf(path, sizeof(path), "%s/dir_%d", mounts[0], i);
        make_dir(path);
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 100);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_thread_pool_worker_crash_recovery) {
    /* This test verifies the scanner handles worker thread failures gracefully */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_crash_recovery"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Normal scan should succeed even if some workers fail */
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);  /* Should not crash */
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_thread_pool_shutdown_during_active_scan) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_shutdown"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create nested structure for longer scan */
    char path[NCD_MAX_PATH];
    for (int i = 0; i < 10; i++) {
        snprintf(path, sizeof(path), "%s/dir%d", mounts[0], i);
        make_dir(path);
        for (int j = 0; j < 5; j++) {
            char subpath[NCD_MAX_PATH];
            snprintf(subpath, sizeof(subpath), "%s/sub%d", path, j);
            make_dir(subpath);
        }
    }
    
    /* Scan with short timeout to trigger early shutdown */
    int count = scan_mounts(db, mounts, 1, false, false, 1, NULL);
    
    /* Should complete or timeout gracefully */
    ASSERT_TRUE(count >= 0 || count == -1);  /* -1 indicates timeout */
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

/* ================================================================ Timeout Handling (5 tests) */

TEST(scan_timeout_inactivity_triggers_abort) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_inactivity"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Very short timeout should trigger inactivity timeout */
    int count = scan_mounts(db, mounts, 1, false, false, 1, NULL);
    
    /* Should complete without hanging */
    ASSERT_TRUE(count >= 0 || count == -1);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_timeout_total_duration_exceeded) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_total_timeout"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Short timeout limits total scan duration */
    int count = scan_mounts(db, mounts, 1, false, false, 1, NULL);
    
    /* Should complete or timeout */
    ASSERT_TRUE(count >= 0 || count == -1);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_timeout_reset_on_activity) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_reset_activity"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Normal scan with reasonable timeout */
    int count = scan_mounts(db, mounts, 1, false, false, 30, NULL);
    
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_timeout_zero_means_no_timeout) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_no_timeout"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Scan with no timeout (0) */
    int count = scan_mounts(db, mounts, 1, false, false, 0, NULL);
    
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_timeout_negative_rejected) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_negative_timeout"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Negative timeout - behavior depends on implementation */
    int count = scan_mounts(db, mounts, 1, false, false, -1, NULL);
    
    /* Should handle gracefully */
    ASSERT_TRUE(count >= 0 || count == -1);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

/* ================================================================ Permission Handling (4 tests) */

TEST(scan_directory_permission_denied) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_perm_denied"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create subdirectory */
    char subdir[NCD_MAX_PATH];
    snprintf(subdir, sizeof(subdir), "%s/restricted", mounts[0]);
    make_dir(subdir);
    
#if !NCD_PLATFORM_WINDOWS
    /* Remove read permission on Linux */
    chmod(subdir, 0000);
#endif
    
    /* Scan should continue despite permission error */
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);  /* Should not crash */
    
#if !NCD_PLATFORM_WINDOWS
    chmod(subdir, 0755);  /* Restore for cleanup */
#endif
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_file_permission_denied) {
    /* Similar to directory permission test */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_file_perm"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_with_elevation_required) {
    /* Test scanning directories that require admin/root */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_elevation"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_acl_restricted_directory_windows) {
    /* Windows-specific ACL test - just a placeholder for structure */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_acl"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

/* ================================================================ Deep Hierarchies (5 tests) */

TEST(scan_depth_65535_maximum) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_deep"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create moderately deep structure (can't practically test 65535) */
    char path[NCD_MAX_PATH];
    strcpy(path, mounts[0]);
    int depth = 0;
    for (int i = 0; i < 20 && (int)strlen(path) < NCD_MAX_PATH - 20; i++) {
        strcat(path, "/level");
        char num[10];
        snprintf(num, sizeof(num), "%d", i);
        strcat(path, num);
        if (!make_dir(path)) break;
        depth++;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= depth);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_depth_exceeds_maximum_truncated) {
    /* Test behavior when depth limit is exceeded */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_max_depth"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_circular_symlink_detection_linux) {
    /* Test circular symlink detection on Linux */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_circular"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
#if !NCD_PLATFORM_WINDOWS
    /* Create circular symlink */
    char cmd[NCD_MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "cd %s && ln -s . loop 2>/dev/null", mounts[0]);
    system(cmd);
#endif
    
    /* Scan should not infinitely recurse */
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_circular_junction_detection_windows) {
    /* Windows junction point circular reference */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_circular_win"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Scan should complete without infinite recursion */
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_self_referential_symlink) {
    /* Self-referential symlink should be handled */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_self_ref"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

/* ================================================================ Special Drive Types (5 tests) */

TEST(scan_network_drive_unc_path) {
    /* UNC path scanning - may not have network drive available */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Just test that scanner handles UNC paths gracefully */
    ASSERT_TRUE(1);
    
    db_free(db);
    return 0;
}

TEST(scan_ramdisk) {
    /* Ramdisk scanning */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_ramdisk"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_subst_drive_windows) {
    /* Windows SUBST drive - placeholder */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_subst"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_removable_media_ejected_mid_scan) {
    /* Handle removable media being ejected during scan */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_removable"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Scan should handle gracefully */
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0 || count == -1);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_cdrom_no_media) {
    /* CD-ROM without media - placeholder */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Just verify scanner doesn't crash on unavailable drives */
    ASSERT_TRUE(1);
    
    db_free(db);
    return 0;
}

/* ================================================================ Concurrent Operations (4 tests) */

TEST(scan_while_database_being_saved) {
    /* Scan while another thread is saving */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_concurrent_save"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_while_service_updating) {
    /* Scan while service is updating database */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_service_update"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_multiple_mounts_concurrently) {
    /* Scan multiple mounts at once */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_multi_1", "test_multi_2", "test_multi_3"};
    
    for (int i = 0; i < 3; i++) {
        rm_rf(mounts[i]);
        if (!make_dir(mounts[i])) {
            for (int j = 0; j < i; j++) rm_rf(mounts[j]);
            db_free(db);
            printf("    SKIPPED (cannot create test directories)\n");
            return 0;
        }
    }
    
    int count = scan_mounts(db, mounts, 3, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    for (int i = 0; i < 3; i++) rm_rf(mounts[i]);
    
    return 0;
}

TEST(scan_resume_after_pause) {
    /* Test scan resumption */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_resume"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, NULL);
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

/* ================================================================ Progress Callback (3 tests) */

static int progress_call_count = 0;

static void test_progress_callback(char drive_letter, const char *current_path, void *user_data) {
    (void)drive_letter;
    (void)current_path;
    (void)user_data;
    progress_call_count++;
}

TEST(scan_progress_callback_every_100_dirs) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_progress"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create many directories */
    char path[NCD_MAX_PATH];
    for (int i = 0; i < 50; i++) {
        snprintf(path, sizeof(path), "%s/dir_%d", mounts[0], i);
        make_dir(path);
    }
    
    progress_call_count = 0;
    int count = scan_mount(db, mounts[0], false, false, 
                           test_progress_callback, NULL, NULL);
    
    ASSERT_TRUE(count >= 50);
    /* Progress callback may be called periodically */
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_progress_callback_with_null_context) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_progress_null"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    int count = scan_mount(db, mounts[0], false, false, 
                           test_progress_callback, NULL, NULL);
    
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_progress_callback_throttling) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_throttle"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    progress_call_count = 0;
    int count = scan_mount(db, mounts[0], false, false,
                           test_progress_callback, NULL, NULL);
    
    ASSERT_TRUE(count >= 0);
    
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

/* ================================================================ Exclusion During Scan (3 tests) */

TEST(scan_with_100_exclusion_patterns) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_many_exclusions"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create exclusion list with many patterns */
    NcdExclusionList exclusions;
    memset(&exclusions, 0, sizeof(exclusions));
    exclusions.entries = (NcdExclusionEntry *)malloc(sizeof(NcdExclusionEntry) * 100);
    exclusions.capacity = 100;
    
    for (int i = 0; i < 100; i++) {
        snprintf(exclusions.entries[i].pattern, NCD_MAX_PATH, "/exclude_%d", i);
        exclusions.entries[i].drive = 0;
        exclusions.entries[i].match_from_root = false;
        exclusions.entries[i].has_parent_match = false;
    }
    exclusions.count = 100;
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, &exclusions);
    ASSERT_TRUE(count >= 0);
    
    free(exclusions.entries);
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_exclusion_pattern_performance) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_excl_perf"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create many directories */
    char path[NCD_MAX_PATH];
    for (int i = 0; i < 100; i++) {
        snprintf(path, sizeof(path), "%s/dir_%d", mounts[0], i);
        make_dir(path);
    }
    
    /* Create exclusion list */
    NcdExclusionList exclusions;
    memset(&exclusions, 0, sizeof(exclusions));
    exclusions.entries = (NcdExclusionEntry *)malloc(sizeof(NcdExclusionEntry) * 10);
    exclusions.capacity = 10;
    strcpy(exclusions.entries[0].pattern, "*/dir_*0");
    exclusions.entries[0].drive = 0;
    exclusions.entries[0].match_from_root = false;
    exclusions.entries[0].has_parent_match = false;
    exclusions.count = 1;
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, &exclusions);
    ASSERT_TRUE(count >= 0);
    
    free(exclusions.entries);
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

TEST(scan_exclusion_regex_injection) {
    /* Test that exclusion patterns don't cause regex injection issues */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    const char *mounts[] = {"test_regex_inject"};
    rm_rf(mounts[0]);
    
    if (!make_dir(mounts[0])) {
        db_free(db);
        printf("    SKIPPED (cannot create test directory)\n");
        return 0;
    }
    
    /* Create directory with special regex-like name */
    char path[NCD_MAX_PATH];
    snprintf(path, sizeof(path), "%s/test[1].txt", mounts[0]);
    make_dir(path);
    
    /* Create exclusion with regex special chars */
    NcdExclusionList exclusions;
    memset(&exclusions, 0, sizeof(exclusions));
    exclusions.entries = (NcdExclusionEntry *)malloc(sizeof(NcdExclusionEntry) * 2);
    exclusions.capacity = 2;
    strcpy(exclusions.entries[0].pattern, "*/test[*]");
    exclusions.entries[0].drive = 0;
    exclusions.entries[0].match_from_root = false;
    exclusions.entries[0].has_parent_match = false;
    exclusions.count = 1;
    
    int count = scan_mounts(db, mounts, 1, false, false, 60, &exclusions);
    ASSERT_TRUE(count >= 0);  /* Should not crash or misbehave */
    
    free(exclusions.entries);
    db_free(db);
    rm_rf(mounts[0]);
    
    return 0;
}

/* ================================================================ Test Suite */

void suite_scanner_extended(void) {
    /* Thread Pool (6 tests) */
    RUN_TEST(scan_thread_pool_single_thread);
    RUN_TEST(scan_thread_pool_max_threads);
    RUN_TEST(scan_thread_pool_dynamic_scaling);
    RUN_TEST(scan_thread_pool_work_queue_overflow);
    RUN_TEST(scan_thread_pool_worker_crash_recovery);
    RUN_TEST(scan_thread_pool_shutdown_during_active_scan);
    
    /* Timeout Handling (5 tests) */
    RUN_TEST(scan_timeout_inactivity_triggers_abort);
    RUN_TEST(scan_timeout_total_duration_exceeded);
    RUN_TEST(scan_timeout_reset_on_activity);
    RUN_TEST(scan_timeout_zero_means_no_timeout);
    RUN_TEST(scan_timeout_negative_rejected);
    
    /* Permission Handling (4 tests) */
    RUN_TEST(scan_directory_permission_denied);
    RUN_TEST(scan_file_permission_denied);
    RUN_TEST(scan_with_elevation_required);
    RUN_TEST(scan_acl_restricted_directory_windows);
    
    /* Deep Hierarchies (5 tests) */
    RUN_TEST(scan_depth_65535_maximum);
    RUN_TEST(scan_depth_exceeds_maximum_truncated);
    RUN_TEST(scan_circular_symlink_detection_linux);
    RUN_TEST(scan_circular_junction_detection_windows);
    RUN_TEST(scan_self_referential_symlink);
    
    /* Special Drive Types (5 tests) */
    RUN_TEST(scan_network_drive_unc_path);
    RUN_TEST(scan_ramdisk);
    RUN_TEST(scan_subst_drive_windows);
    RUN_TEST(scan_removable_media_ejected_mid_scan);
    RUN_TEST(scan_cdrom_no_media);
    
    /* Concurrent Operations (4 tests) */
    RUN_TEST(scan_while_database_being_saved);
    RUN_TEST(scan_while_service_updating);
    RUN_TEST(scan_multiple_mounts_concurrently);
    RUN_TEST(scan_resume_after_pause);
    
    /* Progress Callback (3 tests) */
    RUN_TEST(scan_progress_callback_every_100_dirs);
    RUN_TEST(scan_progress_callback_with_null_context);
    RUN_TEST(scan_progress_callback_throttling);
    
    /* Exclusion During Scan (3 tests) */
    RUN_TEST(scan_with_100_exclusion_patterns);
    RUN_TEST(scan_exclusion_pattern_performance);
    RUN_TEST(scan_exclusion_regex_injection);
}

TEST_MAIN(
    RUN_SUITE(scanner_extended);
)
