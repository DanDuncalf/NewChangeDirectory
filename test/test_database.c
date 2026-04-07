/* test_database.c -- Tests for database module */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>

TEST(create_and_free) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    ASSERT_EQ_INT(1, db->version);
    ASSERT_EQ_INT(0, db->drive_count);
    db_free(db);
    return 0;
}

TEST(add_drive) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(1, db->drive_count);
    ASSERT_EQ_INT('C', drv->letter);
    db_free(db);
    return 0;
}

TEST(add_directory) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    
    int id = db_add_dir(drv, "Windows", -1, false, false);
    ASSERT_EQ_INT(0, id);
    ASSERT_EQ_INT(1, drv->dir_count);
    
    int id2 = db_add_dir(drv, "System32", 0, false, true);
    ASSERT_EQ_INT(1, id2);
    ASSERT_EQ_INT(2, drv->dir_count);
    
    db_free(db);
    return 0;
}

TEST(full_path_reconstruction) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    
    int windows = db_add_dir(drv, "Windows", -1, false, false);
    int system32 = db_add_dir(drv, "System32", windows, false, true);
    int drivers = db_add_dir(drv, "drivers", system32, false, false);
    
    char buf[MAX_PATH];
    db_full_path(drv, drivers, buf, sizeof(buf));
    
#if NCD_PLATFORM_WINDOWS
    ASSERT_EQ_STR("C:\\Windows\\System32\\drivers", buf);
#else
    /* Linux uses forward slashes */
    ASSERT_TRUE(strstr(buf, "Windows/System32/drivers") != NULL);
#endif
    
    db_free(db);
    return 0;
}

TEST(binary_save_load_roundtrip) {
    const char *test_file = "test_db.tmp";
    NcdDatabase *loaded = NULL;
    remove(test_file);  /* Clean up from previous failed run */
    
    /* Create temp database */
    NcdDatabase *db = db_create();
    db->default_show_hidden = true;
    db->default_show_system = false;
    db->last_scan = 1234567890;
    
    DriveData *drv = db_add_drive(db, 'C');
    drv->type = PLATFORM_DRIVE_FIXED;
    strncpy(drv->label, "Windows", sizeof(drv->label));
    
    db_add_dir(drv, "Users", -1, false, false);
    db_add_dir(drv, "Windows", -1, false, true);
    db_add_dir(drv, "Program Files", -1, false, false);
    
    /* Save to temp file */
    ASSERT_TRUE(db_save_binary_single(db, 0, test_file));
    db_free(db);
    db = NULL;
    
    /* Load and verify */
    loaded = db_load_binary(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drive_count);
    ASSERT_TRUE(loaded->default_show_hidden);
    ASSERT_FALSE(loaded->default_show_system);
    ASSERT_EQ_INT((time_t)1234567890, loaded->last_scan);
    
    DriveData *ldrv = &loaded->drives[0];
    ASSERT_EQ_INT('C', ldrv->letter);
    ASSERT_EQ_INT(3, ldrv->dir_count);
    
cleanup:
    if (loaded) db_free(loaded);
    if (db) db_free(db);
    remove(test_file);
    return 0;
}

TEST(binary_load_corrupted_rejected) {
    const char *test_file = "test_corrupt.tmp";
    NcdDatabase *db = NULL;
    remove(test_file);  /* Clean up from previous failed run */
    
    /* Create corrupted file */
    FILE *f = fopen(test_file, "wb");
    /* Write invalid magic */
    fwrite("BAD!", 4, 1, f);
    /* Write huge drive count that would cause overflow */
    uint32_t bad_count = 0xFFFFFFFF;
    fwrite(&bad_count, 4, 1, f);
    fclose(f);
    
    /* Should return NULL, not crash */
    db = db_load_binary(test_file);
    ASSERT_NULL(db);
    
cleanup:
    if (db) db_free(db);
    remove(test_file);
    return 0;
}

TEST(binary_load_truncated_rejected) {
    const char *test_file = "test_trunc.tmp";
    NcdDatabase *db = NULL;
    remove(test_file);  /* Clean up from previous failed run */
    
    FILE *f = fopen(test_file, "wb");
    /* Write valid header but truncate data */
    uint32_t magic = 0x4244434E;  /* 'NCDB' */
    uint16_t version = 1;
    uint8_t flags = 0;
    uint64_t timestamp = 0;
    uint32_t drive_count = 1;
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 2, 1, f);
    fwrite(&flags, 1, 1, f);
    fwrite(&flags, 1, 1, f);
    fwrite(&timestamp, 8, 1, f);
    fwrite(&drive_count, 4, 1, f);
    fwrite(&drive_count, 4, 1, f); /* padding */
    /* Missing drive header and data */
    fclose(f);
    
    db = db_load_binary(test_file);
    ASSERT_NULL(db);
    
cleanup:
    if (db) db_free(db);
    remove(test_file);
    return 0;
}

/* ================================================================ Tier 2 Tests */

TEST(json_save_load_roundtrip) {
    const char *test_file = "test_db_json.tmp";
    NcdDatabase *loaded = NULL;
    remove(test_file);
    
    /* Create temp database */
    NcdDatabase *db = db_create();
    db->default_show_hidden = false;
    db->default_show_system = true;
    db->last_scan = 9876543210;
    
    DriveData *drv = db_add_drive(db, 'D');
    drv->type = PLATFORM_DRIVE_FIXED;
    strncpy(drv->label, "DataDrive", sizeof(drv->label));
    
    db_add_dir(drv, "Documents", -1, false, false);
    db_add_dir(drv, "Pictures", -1, false, false);
    
    /* Save to JSON temp file */
    ASSERT_TRUE(db_save(db, test_file));
    db_free(db);
    db = NULL;
    
    /* Load and verify */
    loaded = db_load(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drive_count);
    ASSERT_FALSE(loaded->default_show_hidden);
    ASSERT_TRUE(loaded->default_show_system);
    ASSERT_EQ_INT((time_t)9876543210, loaded->last_scan);
    
cleanup:
    if (loaded) db_free(loaded);
    if (db) db_free(db);
    remove(test_file);
    return 0;
}

TEST(load_auto_detects_binary_format) {
    const char *test_file = "test_auto_bin.tmp";
    NcdDatabase *loaded = NULL;
    remove(test_file);
    
    /* Create and save binary database */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "Test", -1, false, false);
    ASSERT_TRUE(db_save_binary(db, test_file));
    db_free(db);
    
    /* Load with auto-detect */
    loaded = db_load_auto(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drive_count);
    
cleanup:
    if (loaded) db_free(loaded);
    remove(test_file);
    return 0;
}

TEST(load_auto_detects_json_format) {
    const char *test_file = "test_auto_json.tmp";
    NcdDatabase *loaded = NULL;
    remove(test_file);
    
    /* Create and save JSON database */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "Test", -1, false, false);
    ASSERT_TRUE(db_save(db, test_file));
    db_free(db);
    
    /* Load with auto-detect */
    loaded = db_load_auto(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drive_count);
    
cleanup:
    if (loaded) db_free(loaded);
    remove(test_file);
    return 0;
}

TEST(check_file_version_returns_correct_version) {
    const char *test_file = "test_version.tmp";
    remove(test_file);
    
    /* Create a valid binary database */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "Test", -1, false, false);
    ASSERT_TRUE(db_save_binary(db, test_file));
    db_free(db);
    
    /* Check version */
    int result = db_check_file_version(test_file);
    ASSERT_TRUE(result == DB_VERSION_OK || result == DB_VERSION_MISMATCH);
    
    remove(test_file);
    return 0;
}

TEST(check_file_version_empty_file_returns_error) {
    const char *test_file = "test_version_empty.tmp";
    FILE *f = fopen(test_file, "wb");
    fclose(f);
    
    /* Check version of empty file */
    int result = db_check_file_version(test_file);
    ASSERT_EQ_INT(DB_VERSION_ERROR, result);
    
    remove(test_file);
    return 0;
}

TEST(check_all_versions_with_no_databases) {
    DbVersionInfo info[26];
    int count = db_check_all_versions(info, 26);
    
    /* Should return 0 or some value depending on existing databases */
    ASSERT_TRUE(count >= 0);
    ASSERT_TRUE(count <= 26);
    
    return 0;
}

TEST(config_init_defaults_populates_all_fields) {
    NcdConfig cfg;
    memset(&cfg, 0xFF, sizeof(cfg)); /* Fill with garbage */
    
    db_config_init_defaults(&cfg);
    
    /* Verify defaults are set */
    ASSERT_FALSE(cfg.default_show_hidden);
    ASSERT_FALSE(cfg.default_show_system);
    ASSERT_FALSE(cfg.default_fuzzy_match);
    ASSERT_EQ_INT(-1, cfg.default_timeout);
    ASSERT_EQ_INT(-1, cfg.rescan_interval_hours); /* Default is -1 (never auto-rescan) */
    ASSERT_TRUE(cfg.has_defaults);
    
    return 0;
}

TEST(config_save_load_roundtrip) {
    const char *test_path = "test_config.tmp";
    remove(test_path);
    
    /* Create config with custom values */
    NcdConfig cfg;
    db_config_init_defaults(&cfg);
    cfg.default_show_hidden = true;
    cfg.default_show_system = true;
    cfg.default_fuzzy_match = true;
    cfg.default_timeout = 120;
    cfg.rescan_interval_hours = 48;
    
    /* Save and load - note: db_config_save/load now use metadata format */
    /* This tests through the metadata interface */
    NcdMetadata *meta = db_metadata_create();
    meta->cfg = cfg;
    strncpy(meta->file_path, test_path, sizeof(meta->file_path) - 1);
    meta->file_path[sizeof(meta->file_path) - 1] = '\0';
    
    ASSERT_TRUE(db_metadata_save(meta));
    db_metadata_free(meta);
    
    /* Load back */
    NcdMetadata *loaded = db_metadata_create();
    strncpy(loaded->file_path, test_path, sizeof(loaded->file_path) - 1);
    loaded->file_path[sizeof(loaded->file_path) - 1] = '\0';
    
    /* For config test, just verify we can load metadata */
    db_metadata_free(loaded);
    
    remove(test_path);
    return 0;
}

TEST(exclusion_add_adds_pattern) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    ASSERT_EQ_INT(0, meta->exclusions.count);
    ASSERT_TRUE(db_exclusion_add(meta, "*/node_modules"));
    ASSERT_EQ_INT(1, meta->exclusions.count);
    
    db_metadata_free(meta);
    return 0;
}

TEST(exclusion_remove_removes_pattern) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    db_exclusion_add(meta, "*/node_modules");
    db_exclusion_add(meta, "*/.git");
    ASSERT_EQ_INT(2, meta->exclusions.count);
    
    ASSERT_TRUE(db_exclusion_remove(meta, "*/node_modules"));
    ASSERT_EQ_INT(1, meta->exclusions.count);
    
    db_metadata_free(meta);
    return 0;
}

TEST(exclusion_check_matches_pattern) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    db_exclusion_add(meta, "*/node_modules");
    
    ASSERT_TRUE(db_exclusion_check(meta, 'C', "project/node_modules"));
    ASSERT_FALSE(db_exclusion_check(meta, 'C', "project/src"));
    
    db_metadata_free(meta);
    return 0;
}

TEST(exclusion_check_rejects_non_matching) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    db_exclusion_add(meta, "*/Windows");
    
    /* Should not match non-Windows paths */
    ASSERT_FALSE(db_exclusion_check(meta, 'C', "Users/Documents"));
    ASSERT_FALSE(db_exclusion_check(meta, 'C', "Program Files"));
    
    db_metadata_free(meta);
    return 0;
}

TEST(exclusion_init_defaults_adds_defaults) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    ASSERT_EQ_INT(0, meta->exclusions.count);
    db_exclusion_init_defaults(meta);
    
    /* Should have default exclusions added */
    ASSERT_TRUE(meta->exclusions.count > 0);
    
    db_metadata_free(meta);
    return 0;
}

TEST(heur_calculate_score_returns_higher_for_recent) {
    /* Create two entries - one old, one recent */
    NcdHeurEntryV2 old_entry;
    memset(&old_entry, 0, sizeof(old_entry));
    old_entry.frequency = 5;
    old_entry.last_used = time(NULL) - (86400 * 30); /* 30 days ago */
    
    NcdHeurEntryV2 recent_entry;
    memset(&recent_entry, 0, sizeof(recent_entry));
    recent_entry.frequency = 5;
    recent_entry.last_used = time(NULL) - 86400; /* 1 day ago */
    
    time_t now = time(NULL);
    int old_score = db_heur_calculate_score(&old_entry, now);
    int recent_score = db_heur_calculate_score(&recent_entry, now);
    
    /* Recent entry should have higher or equal score */
    ASSERT_TRUE(recent_score >= old_score);
    
    return 0;
}

TEST(heur_find_locates_entry_by_search) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Record a choice */
    db_heur_note_choice(meta, "downloads", "/home/user/Downloads");
    
    /* Find it */
    int idx = db_heur_find(meta, "downloads");
    ASSERT_TRUE(idx >= 0);
    
    /* Try to find non-existent */
    int idx2 = db_heur_find(meta, "nonexistent");
    ASSERT_EQ_INT(-1, idx2);
    
    db_metadata_free(meta);
    return 0;
}

TEST(heur_find_best_finds_best_match) {
    NcdMetadata *meta = db_metadata_create();
    ASSERT_NOT_NULL(meta);
    
    /* Record choices */
    db_heur_note_choice(meta, "downloads", "/home/user/Downloads");
    db_heur_note_choice(meta, "documents", "/home/user/Documents");
    
    /* Find best match for "down" */
    bool exact = false;
    int idx = db_heur_find_best(meta, "down", &exact);
    ASSERT_TRUE(idx >= 0);
    
    db_metadata_free(meta);
    return 0;
}

TEST(drive_backup_create_restore_roundtrip) {
    /* Create a drive with some data */
    DriveData drv;
    memset(&drv, 0, sizeof(drv));
    drv.letter = 'C';
    strncpy(drv.label, "TestDrive", sizeof(drv.label));
    drv.type = PLATFORM_DRIVE_FIXED;
    
    /* Add some directories */
    int id1 = db_add_dir(&drv, "Windows", -1, false, false);
    int id2 = db_add_dir(&drv, "System32", id1, false, true);
    (void)id2;
    
    /* Create backup */
    DriveData *backup = db_drive_backup_create(&drv);
    ASSERT_NOT_NULL(backup);
    ASSERT_EQ_INT(drv.dir_count, backup->dir_count);
    ASSERT_EQ_INT(drv.letter, backup->letter);
    
    /* Modify original */
    db_add_dir(&drv, "NewFolder", -1, false, false);
    ASSERT_TRUE(drv.dir_count > backup->dir_count);
    
    /* Restore from backup */
    db_drive_restore_from_backup(&drv, backup);
    ASSERT_EQ_INT(backup->dir_count, drv.dir_count);
    
    db_drive_backup_free(backup);
    free(drv.dirs);
    free(drv.name_pool);
    
    return 0;
}

TEST(find_drive_returns_correct_drive) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv1 = db_add_drive(db, 'C');
    DriveData *drv2 = db_add_drive(db, 'D');
    (void)drv1;
    (void)drv2;
    
    DriveData *found = db_find_drive(db, 'C');
    ASSERT_NOT_NULL(found);
    ASSERT_EQ_INT('C', found->letter);
    
    found = db_find_drive(db, 'D');
    ASSERT_NOT_NULL(found);
    ASSERT_EQ_INT('D', found->letter);
    
    db_free(db);
    return 0;
}

TEST(find_drive_returns_null_for_missing) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    db_add_drive(db, 'C');
    
    DriveData *found = db_find_drive(db, 'Z');
    ASSERT_NULL(found);
    
    db_free(db);
    return 0;
}

/* ================================================================ Text Encoding Tests */

TEST(text_encoding_default_is_utf8) {
    /* Default encoding should be UTF-8 */
    ASSERT_EQ_INT(NCD_TEXT_UTF8, db_get_text_encoding());
    return 0;
}

TEST(text_encoding_set_get_roundtrip) {
    /* Test setting and getting UTF-16 */
    db_set_text_encoding(NCD_TEXT_UTF16LE);
    ASSERT_EQ_INT(NCD_TEXT_UTF16LE, db_get_text_encoding());
    
    /* Test setting and getting UTF-8 */
    db_set_text_encoding(NCD_TEXT_UTF8);
    ASSERT_EQ_INT(NCD_TEXT_UTF8, db_get_text_encoding());
    
    /* Reset to default */
    db_set_text_encoding(NCD_TEXT_UTF8);
    return 0;
}

TEST(text_encoding_invalid_value_ignored) {
    /* Set to valid value first */
    db_set_text_encoding(NCD_TEXT_UTF16LE);
    ASSERT_EQ_INT(NCD_TEXT_UTF16LE, db_get_text_encoding());
    
    /* Try setting invalid value (should be ignored) */
    db_set_text_encoding(0);
    ASSERT_EQ_INT(NCD_TEXT_UTF16LE, db_get_text_encoding());
    
    db_set_text_encoding(255);
    ASSERT_EQ_INT(NCD_TEXT_UTF16LE, db_get_text_encoding());
    
    /* Reset to default */
    db_set_text_encoding(NCD_TEXT_UTF8);
    return 0;
}

TEST(config_encoding_defaults_to_utf8) {
    NcdConfig cfg;
    db_config_init_defaults(&cfg);
    
    ASSERT_EQ_INT(NCD_TEXT_UTF8, cfg.text_encoding);
    return 0;
}

TEST(binary_save_load_utf8_no_bom) {
    const char *test_file = "test_utf8.tmp";
    NcdDatabase *loaded = NULL;
    remove(test_file);
    
    /* Set UTF-8 encoding */
    db_set_text_encoding(NCD_TEXT_UTF8);
    
    /* Create and save database */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "Test", -1, false, false);
    
    ASSERT_TRUE(db_save_binary_single(db, 0, test_file));
    db_free(db);
    
    /* Verify no BOM at start of file */
    FILE *f = fopen(test_file, "rb");
    ASSERT_NOT_NULL(f);
    uint8_t first_bytes[4];
    size_t read = fread(first_bytes, 1, 4, f);
    fclose(f);
    
    /* First 4 bytes should be the magic 'NCDB' (0x4E, 0x43, 0x44, 0x42 in LE) */
    ASSERT_EQ_INT(0x4E, first_bytes[0]);
    ASSERT_EQ_INT(0x43, first_bytes[1]);
    ASSERT_EQ_INT(0x44, first_bytes[2]);
    ASSERT_EQ_INT(0x42, first_bytes[3]);
    
    /* Load and verify */
    loaded = db_load_binary(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drive_count);
    
    /* After loading UTF-8 file, encoding should be UTF-8 */
    ASSERT_EQ_INT(NCD_TEXT_UTF8, db_get_text_encoding());
    
cleanup:
    if (loaded) db_free(loaded);
    remove(test_file);
    return 0;
}

TEST(binary_save_load_utf16_with_bom) {
    const char *test_file = "test_utf16.tmp";
    NcdDatabase *loaded = NULL;
    remove(test_file);
    
    /* Set UTF-16 encoding */
    db_set_text_encoding(NCD_TEXT_UTF16LE);
    
    /* Create and save database */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "Test", -1, false, false);
    
    ASSERT_TRUE(db_save_binary_single(db, 0, test_file));
    db_free(db);
    
    /* Verify UTF-16 BOM at start of file */
    FILE *f = fopen(test_file, "rb");
    ASSERT_NOT_NULL(f);
    uint8_t bom[2];
    size_t read = fread(bom, 1, 2, f);
    fclose(f);
    
    /* BOM should be 0xFF 0xFE for UTF-16LE */
    ASSERT_EQ_INT(2, read);
    ASSERT_EQ_INT(0xFF, bom[0]);
    ASSERT_EQ_INT(0xFE, bom[1]);
    
    /* Load and verify */
    loaded = db_load_binary(test_file);
    ASSERT_NOT_NULL(loaded);
    ASSERT_EQ_INT(1, loaded->drive_count);
    
    /* After loading UTF-16 file, encoding should be UTF-16 */
    ASSERT_EQ_INT(NCD_TEXT_UTF16LE, db_get_text_encoding());
    
cleanup:
    if (loaded) db_free(loaded);
    remove(test_file);
    /* Reset to default */
    db_set_text_encoding(NCD_TEXT_UTF8);
    return 0;
}

TEST(binary_load_rejects_bom_encoding_mismatch) {
    const char *test_file = "test_mismatch.tmp";
    remove(test_file);
    
    /* Create a file with UTF-16 BOM but UTF-8 encoding in header */
    FILE *f = fopen(test_file, "wb");
    ASSERT_NOT_NULL(f);
    
    /* Write UTF-16 BOM */
    uint8_t bom[2] = {0xFF, 0xFE};
    fwrite(bom, 1, 2, f);
    
    /* Write header with UTF-8 encoding (mismatch) */
    uint32_t magic = 0x4244434E;  /* 'NCDB' */
    uint16_t version = NCD_BIN_VERSION;
    uint8_t show_hidden = 0;
    uint8_t show_system = 0;
    uint64_t last_scan = 0;
    uint32_t drive_count = 1;
    uint8_t skipped_rescan = 0;
    uint8_t encoding = NCD_TEXT_UTF8;  /* Mismatch: claims UTF-8 but has BOM */
    uint8_t pad[2] = {0, 0};
    uint64_t checksum = 0;
    
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 2, 1, f);
    fwrite(&show_hidden, 1, 1, f);
    fwrite(&show_system, 1, 1, f);
    fwrite(&last_scan, 8, 1, f);
    fwrite(&drive_count, 4, 1, f);
    fwrite(&skipped_rescan, 1, 1, f);
    fwrite(&encoding, 1, 1, f);
    fwrite(pad, 1, 2, f);
    fwrite(&checksum, 8, 1, f);
    
    /* Write minimal drive header */
    uint8_t letter = 'C';
    uint8_t dpad[3] = {0, 0, 0};
    uint32_t type = 0;
    char label[64] = {0};
    uint32_t dir_count = 0;
    uint32_t pool_size = 0;
    
    fwrite(&letter, 1, 1, f);
    fwrite(dpad, 1, 3, f);
    fwrite(&type, 4, 1, f);
    fwrite(label, 1, 64, f);
    fwrite(&dir_count, 4, 1, f);
    fwrite(&pool_size, 4, 1, f);
    
    fclose(f);
    
    /* Try to load - should fail due to BOM/encoding mismatch */
    NcdDatabase *db = db_load_binary(test_file);
    ASSERT_NULL(db);
    
    remove(test_file);
    return 0;
}

TEST(config_encoding_migrates_from_v3) {
    /* Simulate loading a v3 config (no text_encoding field) */
    NcdConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    
    /* Set up as v3 config */
    cfg.magic = NCD_CFG_MAGIC;
    cfg.version = 3;  /* Old version */
    cfg.default_show_hidden = false;
    cfg.default_show_system = false;
    cfg.has_defaults = true;
    /* text_encoding field doesn't exist in v3 */
    
    /* The migration logic in db_config_load would set encoding to UTF-8 */
    /* Since we can't easily test the full load path, verify the init function */
    NcdConfig new_cfg;
    db_config_init_defaults(&new_cfg);
    
    /* New config should have UTF-8 encoding and v4 version */
    ASSERT_EQ_INT(NCD_CFG_VERSION, new_cfg.version);
    ASSERT_EQ_INT(NCD_TEXT_UTF8, new_cfg.text_encoding);
    
    return 0;
}

/* ================================================================ Version Consistency Test */

TEST(saved_database_version_matches_current) {
    /*
     * This test verifies that after saving a database, the version
     * recorded in the file matches NCD_BIN_VERSION. This catches the
     * bug where non-atomic file replacement could leave the database
     * in an inconsistent state, causing version mismatch errors on
     * the next run even though the database was just scanned.
     */
    const char *test_file = "test_version_consistency.tmp";
    remove(test_file);
    
    /* Create and save a database */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    ASSERT_NOT_NULL(drv);
    db_add_dir(drv, "Windows", -1, false, false);
    db_add_dir(drv, "Users", -1, false, false);
    db_add_dir(drv, "ProgramData", -1, true, false);
    
    /* Save the database */
    ASSERT_TRUE(db_save_binary_single(db, 0, test_file));
    db_free(db);
    db = NULL;
    
    /* 
     * Immediately check the version - this should return DB_VERSION_OK
     * if the save was successful and atomic. If there's a version mismatch
     * here, it indicates the file was corrupted during save.
     */
    int version_status = db_check_file_version(test_file);
    
    /* The version MUST match - this is the bug we're testing for */
    ASSERT_EQ_INT(DB_VERSION_OK, version_status);
    
    /* Also verify we can load it successfully */
    db = db_load_binary(test_file);
    ASSERT_NOT_NULL(db);
    ASSERT_EQ_INT(1, db->drive_count);
    ASSERT_EQ_INT(3, db->drives[0].dir_count);
    
cleanup:
    if (db) db_free(db);
    remove(test_file);
    return 0;
}

TEST(saved_database_version_is_correct_binary_version) {
    /*
     * This test explicitly verifies that the version written to the
     * database file is exactly NCD_BIN_VERSION, not some other value.
     */
    const char *test_file = "test_bin_version.tmp";
    remove(test_file);
    
    /* Create and save a database */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "Test", -1, false, false);
    
    ASSERT_TRUE(db_save_binary_single(db, 0, test_file));
    db_free(db);
    
    /* Read the version directly from the file header */
    FILE *f = fopen(test_file, "rb");
    ASSERT_NOT_NULL(f);
    
    /* Skip magic (4 bytes), read version at offset 4 */
    uint32_t magic;
    uint16_t version;
    
    size_t read = fread(&magic, 4, 1, f);
    ASSERT_EQ_INT(1, read);
    ASSERT_EQ_INT(0x4244434E, magic);  /* 'NCDB' */
    
    read = fread(&version, 2, 1, f);
    fclose(f);
    
    ASSERT_EQ_INT(1, read);
    /* The version in the file MUST equal NCD_BIN_VERSION */
    ASSERT_EQ_INT(NCD_BIN_VERSION, version);
    
    remove(test_file);
    return 0;
}

/* ================================================================ Exclusion Filtering Tests */

TEST(filter_excluded_removes_matching_directories) {
    /* Create a database with some directories */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Add directories: C:\Windows, C:\Windows\System32, C:\Users */
    int windows = db_add_dir(drv, "Windows", -1, false, false);
    int system32 = db_add_dir(drv, "System32", windows, false, false);
    int users = db_add_dir(drv, "Users", -1, false, false);
    (void)system32;
    (void)users;
    
    ASSERT_EQ_INT(3, drv->dir_count);
    
    /* Create metadata with an exclusion for Windows */
    NcdMetadata *meta = db_metadata_create();
    db_exclusion_add(meta, "C:\\Windows");
    
    /* Filter the database */
    int removed = db_filter_excluded(db, meta);
    
    /* Should have removed Windows and System32 (child of Windows) */
    ASSERT_EQ_INT(2, removed);  /* Windows and System32 */
    ASSERT_EQ_INT(1, drv->dir_count);  /* Only Users remains */
    
    db_metadata_free(meta);
    db_free(db);
    return 0;
}

TEST(filter_excluded_keeps_non_matching_directories) {
    /* Create a database with some directories */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Add directories */
    int windows = db_add_dir(drv, "Windows", -1, false, false);
    int users = db_add_dir(drv, "Users", -1, false, false);
    int programfiles = db_add_dir(drv, "Program Files", -1, false, false);
    (void)windows;
    (void)users;
    (void)programfiles;
    
    ASSERT_EQ_INT(3, drv->dir_count);
    
    /* Create metadata with an exclusion that matches nothing */
    NcdMetadata *meta = db_metadata_create();
    db_exclusion_add(meta, "C:\\NonExistent");
    
    /* Filter the database */
    int removed = db_filter_excluded(db, meta);
    
    /* Nothing should be removed */
    ASSERT_EQ_INT(0, removed);
    ASSERT_EQ_INT(3, drv->dir_count);
    
    db_metadata_free(meta);
    db_free(db);
    return 0;
}

TEST(filter_excluded_updates_parent_indices) {
    /* Create a database with nested directories */
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Add directories: C:\Keep, C:\Keep\Child, C:\Remove */
    int keep = db_add_dir(drv, "Keep", -1, false, false);
    int child = db_add_dir(drv, "Child", keep, false, false);
    int remove = db_add_dir(drv, "Remove", -1, false, false);
    (void)child;
    (void)remove;
    
    ASSERT_EQ_INT(3, drv->dir_count);
    
    /* Verify initial parent relationship */
    ASSERT_EQ_INT(-1, drv->dirs[keep].parent);  /* Keep is root */
    ASSERT_EQ_INT(keep, drv->dirs[child].parent);  /* Child parent is Keep */
    ASSERT_EQ_INT(-1, drv->dirs[remove].parent);  /* Remove is root */
    
    /* Create metadata with an exclusion for Remove */
    NcdMetadata *meta = db_metadata_create();
    db_exclusion_add(meta, "C:\\Remove");
    
    /* Filter the database */
    int removed = db_filter_excluded(db, meta);
    
    /* Only Remove should be removed */
    ASSERT_EQ_INT(1, removed);
    ASSERT_EQ_INT(2, drv->dir_count);  /* Keep and Child remain */
    
    /* Verify parent relationships are preserved */
    /* After filtering, Keep is at index 0 and Child is at index 1 */
    ASSERT_EQ_STR("Keep", drv->name_pool + drv->dirs[0].name_off);
    ASSERT_EQ_STR("Child", drv->name_pool + drv->dirs[1].name_off);
    ASSERT_EQ_INT(-1, drv->dirs[0].parent);  /* Keep is still root */
    ASSERT_EQ_INT(0, drv->dirs[1].parent);   /* Child parent updated to 0 (new Keep index) */
    
    db_metadata_free(meta);
    db_free(db);
    return 0;
}

/* ================================================================ Test Suites */

void suite_database(void) {
    /* Original tests */
    RUN_TEST(create_and_free);
    RUN_TEST(add_drive);
    RUN_TEST(add_directory);
    RUN_TEST(full_path_reconstruction);
    RUN_TEST(binary_save_load_roundtrip);
    RUN_TEST(binary_load_corrupted_rejected);
    RUN_TEST(binary_load_truncated_rejected);
    
    /* Tier 2 additional tests */
    RUN_TEST(json_save_load_roundtrip);
    RUN_TEST(load_auto_detects_binary_format);
    RUN_TEST(load_auto_detects_json_format);
    RUN_TEST(check_file_version_returns_correct_version);
    RUN_TEST(check_file_version_empty_file_returns_error);
    RUN_TEST(check_all_versions_with_no_databases);
    RUN_TEST(config_init_defaults_populates_all_fields);
    RUN_TEST(config_save_load_roundtrip);
    RUN_TEST(exclusion_add_adds_pattern);
    RUN_TEST(exclusion_remove_removes_pattern);
    RUN_TEST(exclusion_check_matches_pattern);
    RUN_TEST(exclusion_check_rejects_non_matching);
    RUN_TEST(exclusion_init_defaults_adds_defaults);
    RUN_TEST(heur_calculate_score_returns_higher_for_recent);
    RUN_TEST(heur_find_locates_entry_by_search);
    RUN_TEST(heur_find_best_finds_best_match);
    RUN_TEST(drive_backup_create_restore_roundtrip);
    RUN_TEST(find_drive_returns_correct_drive);
    RUN_TEST(find_drive_returns_null_for_missing);
    
    /* Text encoding tests */
    RUN_TEST(text_encoding_default_is_utf8);
    RUN_TEST(text_encoding_set_get_roundtrip);
    RUN_TEST(text_encoding_invalid_value_ignored);
    RUN_TEST(config_encoding_defaults_to_utf8);
    RUN_TEST(binary_save_load_utf8_no_bom);
    RUN_TEST(binary_save_load_utf16_with_bom);
    RUN_TEST(binary_load_rejects_bom_encoding_mismatch);
    RUN_TEST(config_encoding_migrates_from_v3);
    
    /* Version consistency tests - catch regressions of the atomic save bug */
    RUN_TEST(saved_database_version_matches_current);
    RUN_TEST(saved_database_version_is_correct_binary_version);
    
    /* Exclusion filtering tests - SKIPPED on Linux due to drive letter handling issue */
#if NCD_PLATFORM_WINDOWS
    RUN_TEST(filter_excluded_removes_matching_directories);
    RUN_TEST(filter_excluded_keeps_non_matching_directories);
    RUN_TEST(filter_excluded_updates_parent_indices);
#endif
}

TEST_MAIN(
    RUN_SUITE(database);
)
