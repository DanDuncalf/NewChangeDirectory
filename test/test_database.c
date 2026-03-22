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

void suite_database(void) {
    RUN_TEST(create_and_free);
    RUN_TEST(add_drive);
    RUN_TEST(add_directory);
    RUN_TEST(full_path_reconstruction);
    RUN_TEST(binary_save_load_roundtrip);
    RUN_TEST(binary_load_corrupted_rejected);
    RUN_TEST(binary_load_truncated_rejected);
}

TEST_MAIN(
    RUN_SUITE(database);
)
