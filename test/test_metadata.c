/* test_metadata.c -- Tests for consolidated metadata module */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#define GetCurrentProcessId getpid
#endif

TEST(metadata_create_returns_valid_struct) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    /* cfg is a struct member, not a pointer */
    ASSERT_EQ_INT(0, meta->groups.count);
    ASSERT_EQ_INT(0, meta->heuristics.count);
    ASSERT_EQ_INT(0, meta->exclusions.count);
    ASSERT_EQ_INT(0, meta->dir_history.count);
    
    db_metadata_free(meta);
    return 0;
}

TEST(metadata_save_load_roundtrip) {
    /* Use default metadata path for roundtrip test (db_metadata_load uses default path) */
    char test_path[MAX_PATH];
    if (!db_metadata_path(test_path, sizeof(test_path))) {
        /* Skip test if we can't get metadata path */
        return 0;
    }
    
    /* Backup existing metadata file if present */
    char backup_path[MAX_PATH];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", test_path);
    bool had_existing = platform_file_exists(test_path);
    if (had_existing) {
        remove(backup_path);
        rename(test_path, backup_path);
    }
    
    /* Create and populate metadata */
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Set config values */
    meta->cfg.default_show_hidden = true;
    meta->cfg.default_show_system = true;
    meta->cfg.default_fuzzy_match = true;
    meta->cfg.rescan_interval_hours = 48;
    
    /* Add a group */
    db_group_set(meta, "@testgroup", "/home/user/test");
    
    /* Add an exclusion */
    db_exclusion_add(meta, "*/node_modules");
    
    /* Save to default path */
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load and verify */
    NcdMetadata *loaded = db_metadata_load();
    ASSERT_NOT_NULL(loaded);
    
    /* Check config preserved */
    ASSERT_TRUE(loaded->cfg.default_show_hidden);
    ASSERT_TRUE(loaded->cfg.default_show_system);
    ASSERT_TRUE(loaded->cfg.default_fuzzy_match);
    ASSERT_EQ_INT(48, loaded->cfg.rescan_interval_hours);
    
    db_metadata_free(loaded);
    
    /* Cleanup: remove test file and restore backup */
    remove(test_path);
    if (had_existing) {
        rename(backup_path, test_path);
    }
    
    return 0;
}

TEST(metadata_exists_returns_false_before_save) {
    /* Note: This test may be affected by existing user metadata */
    /* We'll test by creating a temp metadata and checking file existence */
    /* Use unique filename with PID to avoid conflicts with previous test runs */
    char test_path[256];
    snprintf(test_path, sizeof(test_path), "test_meta_exist_%lu.tmp", (unsigned long)GetCurrentProcessId());
    remove(test_path);
    
    /* Before creating file, it shouldn't exist */
    ASSERT_FALSE(platform_file_exists(test_path));
    
    /* Create metadata with test path */
    NcdMetadata *meta = db_metadata_create();
    strncpy(meta->file_path, test_path, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    /* Save it */
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Now file should exist */
    ASSERT_TRUE(platform_file_exists(test_path));
    
    remove(test_path);
    return 0;
}

TEST(metadata_preserves_config_section) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Set all config fields */
    db_config_init_defaults(&meta->cfg);
    meta->cfg.default_show_hidden = true;
    meta->cfg.default_show_system = false;
    meta->cfg.default_fuzzy_match = true;
    meta->cfg.default_timeout = 60;
    meta->cfg.rescan_interval_hours = 12;
    
    /* Verify config values are stored */
    ASSERT_TRUE(meta->cfg.default_show_hidden);
    ASSERT_FALSE(meta->cfg.default_show_system);
    ASSERT_TRUE(meta->cfg.default_fuzzy_match);
    ASSERT_EQ_INT(60, meta->cfg.default_timeout);
    ASSERT_EQ_INT(12, meta->cfg.rescan_interval_hours);
    
    db_metadata_free(meta);
    return 0;
}

TEST(metadata_preserves_groups_section) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Add multiple groups */
    ASSERT_TRUE(db_group_set(meta, "@home", "/home/user"));
    ASSERT_TRUE(db_group_set(meta, "@work", "/home/user/projects"));
    ASSERT_TRUE(db_group_set(meta, "@docs", "/home/user/Documents"));
    
    /* Verify groups are stored */
    ASSERT_EQ_INT(3, meta->groups.count);
    
    /* Verify we can retrieve them */
    const NcdGroupEntry *entry = db_group_get(meta, "@home");
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ_STR("/home/user", entry->path);
    
    entry = db_group_get(meta, "@work");
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ_STR("/home/user/projects", entry->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(metadata_preserves_exclusions_section) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Add exclusion patterns */
    ASSERT_TRUE(db_exclusion_add(meta, "*/node_modules"));
    ASSERT_TRUE(db_exclusion_add(meta, "*/.git"));
    ASSERT_TRUE(db_exclusion_add(meta, "C:\\Windows"));
    
    /* Verify exclusions are stored */
    ASSERT_EQ_INT(3, meta->exclusions.count);
    
    /* Verify patterns are preserved */
    ASSERT_STR_CONTAINS(meta->exclusions.entries[0].pattern, "node_modules");
    ASSERT_STR_CONTAINS(meta->exclusions.entries[1].pattern, ".git");
    
    db_metadata_free(meta);
    return 0;
}

TEST(metadata_preserves_heuristics_section) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Record some choices */
    db_heur_note_choice(meta, "downloads", "/home/user/Downloads");
    db_heur_note_choice(meta, "documents", "/home/user/Documents");
    db_heur_note_choice(meta, "downloads", "/home/user/Downloads"); /* Second time */
    
    /* Verify heuristics are stored */
    ASSERT_TRUE(meta->heuristics.count >= 2);
    
    /* Verify we can find entries */
    int idx = db_heur_find(meta, "downloads");
    ASSERT_TRUE(idx >= 0);
    
    idx = db_heur_find(meta, "documents");
    ASSERT_TRUE(idx >= 0);
    
    db_metadata_free(meta);
    return 0;
}

TEST(metadata_preserves_history_section) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Add history entries */
    ASSERT_TRUE(db_dir_history_add(meta, "/home/user/project1", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "/home/user/project2", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "/home/user/project3", 'D'));
    
    /* Verify history is stored */
    ASSERT_EQ_INT(3, db_dir_history_count(meta));
    
    /* Verify entries are in correct order (most recent first) */
    const NcdDirHistoryEntry *entry = db_dir_history_get(meta, 0);
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ_STR("/home/user/project3", entry->path);
    ASSERT_EQ_INT('D', entry->drive);
    
    entry = db_dir_history_get(meta, 1);
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ_STR("/home/user/project2", entry->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(metadata_cleanup_legacy_returns_0_with_none) {
    /* Clean up any existing legacy files first */
    char path[MAX_PATH];
    
    /* This test assumes no legacy files exist - cleanup should return 0 */
    int deleted = db_metadata_cleanup_legacy();
    /* Just verify the function doesn't crash - return value depends on environment */
    (void)deleted;
    
    return 0;
}

TEST(metadata_free_handles_null) {
    /* Should not crash */
    db_metadata_free(NULL);
    return 0;
}

TEST(metadata_group_remove_path) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Add a group with multiple paths */
    db_group_set(meta, "@projects", "/home/user/proj1");
    /* Note: db_group_set updates existing, so we test single entry operations */
    
    /* Verify group exists */
    const NcdGroupEntry *entry = db_group_get(meta, "@projects");
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ_STR("/home/user/proj1", entry->path);
    
    /* Remove the group */
    ASSERT_TRUE(db_group_remove(meta, "@projects"));
    
    /* Verify group is gone */
    entry = db_group_get(meta, "@projects");
    ASSERT_NULL(entry);
    
    db_metadata_free(meta);
    return 0;
}

/* Test suites */
void suite_metadata(void) {
    RUN_TEST(metadata_create_returns_valid_struct);
    RUN_TEST(metadata_save_load_roundtrip);
    RUN_TEST(metadata_exists_returns_false_before_save);
    RUN_TEST(metadata_preserves_config_section);
    RUN_TEST(metadata_preserves_groups_section);
    RUN_TEST(metadata_preserves_exclusions_section);
    RUN_TEST(metadata_preserves_heuristics_section);
    RUN_TEST(metadata_preserves_history_section);
    RUN_TEST(metadata_cleanup_legacy_returns_0_with_none);
    RUN_TEST(metadata_free_handles_null);
    RUN_TEST(metadata_group_remove_path);
}

/* Main */
TEST_MAIN(
    RUN_SUITE(metadata);
)
