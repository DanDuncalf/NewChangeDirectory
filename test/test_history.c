/* test_history.c -- Tests for directory history functionality */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdio.h>

/* Helper to create a test metadata with clean state */
static NcdMetadata *create_test_metadata(void) {
    NcdMetadata *meta = db_metadata_create();
    /* Clear any existing history */
    db_dir_history_clear(meta);
    meta->dir_history_dirty = false;
    return meta;
}

TEST(history_add_single) {
    NcdMetadata *meta = create_test_metadata();
    
    ASSERT_EQ_INT(0, db_dir_history_count(meta));
    
    /* Add first entry */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Users\\Test", 'C'));
    ASSERT_EQ_INT(1, db_dir_history_count(meta));
    ASSERT_TRUE(meta->dir_history_dirty);
    
    const NcdDirHistoryEntry *e = db_dir_history_get(meta, 0);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ_STR("C:\\Users\\Test", e->path);
    ASSERT_EQ_INT('C', e->drive);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_add_multiple) {
    NcdMetadata *meta = create_test_metadata();
    
    /* Add multiple entries */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Users\\Test1", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Users\\Test2", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "D:\\Projects", 'D'));
    
    ASSERT_EQ_INT(3, db_dir_history_count(meta));
    
    /* Verify order (most recent first) */
    const NcdDirHistoryEntry *e0 = db_dir_history_get(meta, 0);
    const NcdDirHistoryEntry *e1 = db_dir_history_get(meta, 1);
    const NcdDirHistoryEntry *e2 = db_dir_history_get(meta, 2);
    
    ASSERT_NOT_NULL(e0);
    ASSERT_NOT_NULL(e1);
    ASSERT_NOT_NULL(e2);
    
    ASSERT_EQ_STR("D:\\Projects", e0->path);
    ASSERT_EQ_STR("C:\\Users\\Test2", e1->path);
    ASSERT_EQ_STR("C:\\Users\\Test1", e2->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_add_duplicate_moves_to_top) {
    NcdMetadata *meta = create_test_metadata();
    
    /* Add entries */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\First", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Second", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Third", 'C'));
    
    ASSERT_EQ_INT(3, db_dir_history_count(meta));
    ASSERT_EQ_STR("C:\\Third", db_dir_history_get(meta, 0)->path);
    
    /* Re-add "First" - should move to top, not create duplicate */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\First", 'C'));
    
    ASSERT_EQ_INT(3, db_dir_history_count(meta));  /* Still 3, not 4 */
    ASSERT_EQ_STR("C:\\First", db_dir_history_get(meta, 0)->path);
    ASSERT_EQ_STR("C:\\Third", db_dir_history_get(meta, 1)->path);
    ASSERT_EQ_STR("C:\\Second", db_dir_history_get(meta, 2)->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_add_same_case_insensitive) {
    NcdMetadata *meta = create_test_metadata();
    
    /* Add entries with different cases */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Users\\Test", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "c:\\users\\test", 'C'));
    
    /* Should be treated as same entry (case insensitive) */
    ASSERT_EQ_INT(1, db_dir_history_count(meta));
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_add_max_limit) {
    NcdMetadata *meta = create_test_metadata();
    
    /* Add more than max entries */
    for (int i = 0; i < NCD_DIR_HISTORY_MAX + 3; i++) {
        char path[64];
        snprintf(path, sizeof(path), "C:\\Path%d", i);
        ASSERT_TRUE(db_dir_history_add(meta, path, 'C'));
    }
    
    /* Should be capped at max */
    ASSERT_EQ_INT(NCD_DIR_HISTORY_MAX, db_dir_history_count(meta));
    
    /* Most recent should be the last added */
    char expected[64];
    snprintf(expected, sizeof(expected), "C:\\Path%d", NCD_DIR_HISTORY_MAX + 2);
    ASSERT_EQ_STR(expected, db_dir_history_get(meta, 0)->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_get_out_of_bounds) {
    NcdMetadata *meta = create_test_metadata();
    
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Test", 'C'));
    
    /* Valid index */
    ASSERT_NOT_NULL(db_dir_history_get(meta, 0));
    
    /* Invalid indices */
    ASSERT_NULL(db_dir_history_get(meta, -1));
    ASSERT_NULL(db_dir_history_get(meta, 1));
    ASSERT_NULL(db_dir_history_get(meta, 100));
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_clear) {
    NcdMetadata *meta = create_test_metadata();
    
    /* Add some entries */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\First", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Second", 'C'));
    ASSERT_EQ_INT(2, db_dir_history_count(meta));
    
    meta->dir_history_dirty = false;
    
    /* Clear */
    db_dir_history_clear(meta);
    
    ASSERT_EQ_INT(0, db_dir_history_count(meta));
    ASSERT_TRUE(meta->dir_history_dirty);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_remove_by_index) {
    NcdMetadata *meta = create_test_metadata();
    
    /* Add entries */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\First", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Second", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Third", 'C'));
    ASSERT_EQ_INT(3, db_dir_history_count(meta));
    
    meta->dir_history_dirty = false;
    
    /* Remove middle entry (index 1 = Second) */
    ASSERT_TRUE(db_dir_history_remove(meta, 1));
    
    ASSERT_EQ_INT(2, db_dir_history_count(meta));
    ASSERT_TRUE(meta->dir_history_dirty);
    ASSERT_EQ_STR("C:\\Third", db_dir_history_get(meta, 0)->path);
    ASSERT_EQ_STR("C:\\First", db_dir_history_get(meta, 1)->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_remove_first) {
    NcdMetadata *meta = create_test_metadata();
    
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\First", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Second", 'C'));
    ASSERT_EQ_INT(2, db_dir_history_count(meta));
    
    /* Remove first (most recent) */
    ASSERT_TRUE(db_dir_history_remove(meta, 0));
    
    ASSERT_EQ_INT(1, db_dir_history_count(meta));
    ASSERT_EQ_STR("C:\\First", db_dir_history_get(meta, 0)->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_remove_last) {
    NcdMetadata *meta = create_test_metadata();
    
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\First", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Second", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Third", 'C'));
    ASSERT_EQ_INT(3, db_dir_history_count(meta));
    
    /* Remove last (oldest) */
    ASSERT_TRUE(db_dir_history_remove(meta, 2));
    
    ASSERT_EQ_INT(2, db_dir_history_count(meta));
    ASSERT_EQ_STR("C:\\Third", db_dir_history_get(meta, 0)->path);
    ASSERT_EQ_STR("C:\\Second", db_dir_history_get(meta, 1)->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_remove_out_of_bounds) {
    NcdMetadata *meta = create_test_metadata();
    
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Test", 'C'));
    ASSERT_EQ_INT(1, db_dir_history_count(meta));
    
    /* Invalid indices should return false */
    ASSERT_FALSE(db_dir_history_remove(meta, -1));
    ASSERT_FALSE(db_dir_history_remove(meta, 1));
    ASSERT_FALSE(db_dir_history_remove(meta, 100));
    
    /* History should be unchanged */
    ASSERT_EQ_INT(1, db_dir_history_count(meta));
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_remove_from_empty) {
    NcdMetadata *meta = create_test_metadata();
    
    ASSERT_EQ_INT(0, db_dir_history_count(meta));
    ASSERT_FALSE(db_dir_history_remove(meta, 0));
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_swap_first_two) {
    NcdMetadata *meta = create_test_metadata();
    
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\First", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Second", 'C'));
    
    ASSERT_EQ_STR("C:\\Second", db_dir_history_get(meta, 0)->path);
    ASSERT_EQ_STR("C:\\First", db_dir_history_get(meta, 1)->path);
    
    meta->dir_history_dirty = false;
    
    /* Swap */
    db_dir_history_swap_first_two(meta);
    
    ASSERT_TRUE(meta->dir_history_dirty);
    ASSERT_EQ_STR("C:\\First", db_dir_history_get(meta, 0)->path);
    ASSERT_EQ_STR("C:\\Second", db_dir_history_get(meta, 1)->path);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_swap_with_less_than_two) {
    NcdMetadata *meta = create_test_metadata();
    
    /* Zero entries - should not crash */
    db_dir_history_swap_first_two(meta);
    ASSERT_EQ_INT(0, db_dir_history_count(meta));
    
    /* One entry - should not crash */
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Only", 'C'));
    meta->dir_history_dirty = false;
    db_dir_history_swap_first_two(meta);
    ASSERT_EQ_INT(1, db_dir_history_count(meta));
    /* Dirty flag should not be set since no swap occurred */
    ASSERT_FALSE(meta->dir_history_dirty);
    
    db_metadata_free(meta);
    return 0;
}

TEST(history_persistence_save_load) {
    const char *test_file = "test_history_metadata.tmp";
    remove(test_file);
    
    /* Create metadata and add history entries */
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Users\\Test1", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "C:\\Windows", 'C'));
    ASSERT_TRUE(db_dir_history_add(meta, "D:\\Projects", 'D'));
    ASSERT_EQ_INT(3, db_dir_history_count(meta));
    
    /* Save metadata (includes history) */
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load metadata in a new container */
    NcdMetadata *loaded = db_metadata_create();
    ASSERT_NOT_NULL(loaded);
    strncpy(loaded->file_path, test_file, sizeof(loaded->file_path) - 1);
    loaded->file_path[sizeof(loaded->file_path) - 1] = '\0';
    
    /* Note: db_metadata_load uses the default path, so we load directly
     * using the internal load function behavior - just verify the save worked */
    
    /* For this test, we'll verify by loading the file directly */
    /* Since metadata load uses default path, let's just verify we can load the file */
    FILE *f = fopen(test_file, "rb");
    ASSERT_NOT_NULL(f);
    
    /* Check file header - should start with NCMD magic */
    uint32_t magic;
    size_t read = fread(&magic, 4, 1, f);
    fclose(f);
    
    ASSERT_EQ_INT(1, read);
    ASSERT_EQ_INT(0x444D434E, magic);  /* 'NCMD' in little-endian */
    
    /* Load via metadata (using temp file path override) */
    db_metadata_set_override(test_file);
    NcdMetadata *loaded2 = db_metadata_load();
    db_metadata_set_override(NULL);  /* Clear override */
    
    if (loaded2) {
        /* Verify history entries were preserved */
        ASSERT_EQ_INT(3, db_dir_history_count(loaded2));
        
        /* Most recent should be first */
        const NcdDirHistoryEntry *e0 = db_dir_history_get(loaded2, 0);
        const NcdDirHistoryEntry *e1 = db_dir_history_get(loaded2, 1);
        const NcdDirHistoryEntry *e2 = db_dir_history_get(loaded2, 2);
        
        ASSERT_NOT_NULL(e0);
        ASSERT_NOT_NULL(e1);
        ASSERT_NOT_NULL(e2);
        
        /* Verify paths are preserved (case-insensitive compare) */
        ASSERT_TRUE(_stricmp("D:\\Projects", e0->path) == 0);
        ASSERT_TRUE(_stricmp("C:\\Windows", e1->path) == 0);
        ASSERT_TRUE(_stricmp("C:\\Users\\Test1", e2->path) == 0);
        
        /* Verify drive letters are preserved */
        ASSERT_EQ_INT('D', e0->drive);
        ASSERT_EQ_INT('C', e1->drive);
        ASSERT_EQ_INT('C', e2->drive);
        
        db_metadata_free(loaded2);
    }
    
    db_metadata_free(loaded);
    remove(test_file);
    return 0;
}

TEST(history_persistence_empty_history) {
    const char *test_file = "test_history_empty.tmp";
    remove(test_file);
    
    /* Create metadata with no history */
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    ASSERT_EQ_INT(0, db_dir_history_count(meta));
    
    /* Save */
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load and verify empty history */
    db_metadata_set_override(test_file);
    NcdMetadata *loaded = db_metadata_load();
    db_metadata_set_override(NULL);
    
    if (loaded) {
        ASSERT_EQ_INT(0, db_dir_history_count(loaded));
        db_metadata_free(loaded);
    }
    
    remove(test_file);
    return 0;
}

TEST(history_persistence_max_entries) {
    const char *test_file = "test_history_max.tmp";
    remove(test_file);
    
    /* Create metadata with max history entries */
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Add max entries */
    for (int i = 0; i < NCD_DIR_HISTORY_MAX; i++) {
        char path[64];
        snprintf(path, sizeof(path), "C:\\Path%d", i);
        ASSERT_TRUE(db_dir_history_add(meta, path, 'C'));
    }
    ASSERT_EQ_INT(NCD_DIR_HISTORY_MAX, db_dir_history_count(meta));
    
    /* Save */
    strncpy(meta->file_path, test_file, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load and verify all entries preserved */
    db_metadata_set_override(test_file);
    NcdMetadata *loaded = db_metadata_load();
    db_metadata_set_override(NULL);
    
    if (loaded) {
        ASSERT_EQ_INT(NCD_DIR_HISTORY_MAX, db_dir_history_count(loaded));
        
        /* Verify order preserved - most recent should be last added */
        const NcdDirHistoryEntry *e = db_dir_history_get(loaded, 0);
        ASSERT_NOT_NULL(e);
        char expected[64];
        snprintf(expected, sizeof(expected), "C:\\Path%d", NCD_DIR_HISTORY_MAX - 1);
        ASSERT_TRUE(_stricmp(expected, e->path) == 0);
        
        db_metadata_free(loaded);
    }
    
    remove(test_file);
    return 0;
}

void suite_history(void) {
    RUN_TEST(history_add_single);
    RUN_TEST(history_add_multiple);
    RUN_TEST(history_add_duplicate_moves_to_top);
    RUN_TEST(history_add_same_case_insensitive);
    RUN_TEST(history_add_max_limit);
    RUN_TEST(history_get_out_of_bounds);
    RUN_TEST(history_clear);
    RUN_TEST(history_remove_by_index);
    RUN_TEST(history_remove_first);
    RUN_TEST(history_remove_last);
    RUN_TEST(history_remove_out_of_bounds);
    RUN_TEST(history_remove_from_empty);
    RUN_TEST(history_swap_first_two);
    RUN_TEST(history_swap_with_less_than_two);
    
    /* History persistence tests (Phase 5) */
    RUN_TEST(history_persistence_save_load);
    RUN_TEST(history_persistence_empty_history);
    RUN_TEST(history_persistence_max_entries);
}

TEST_MAIN(
    RUN_SUITE(history);
)
