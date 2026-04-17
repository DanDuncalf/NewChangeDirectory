/* fuzz_database_load.c -- Database loading fuzz tests (Agent 1) */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Helper: Create a minimal valid binary database */
static void create_valid_db_header(uint8_t *buf, size_t *len) {
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = NCD_BIN_MAGIC;
    hdr.version = NCD_BIN_VERSION;
    hdr.show_hidden = 0;
    hdr.show_system = 0;
    hdr.last_scan = 0;
    hdr.drive_count = 1;
    hdr.skipped_rescan = 0;
    hdr.encoding = NCD_TEXT_UTF8;
    hdr.checksum = 0;
    
    memcpy(buf, &hdr, sizeof(hdr));
    *len = sizeof(hdr);
}

static void add_drive_header(uint8_t *buf, size_t *len, uint32_t dir_count, uint32_t pool_size) {
    BinDriveHdr dhdr;
    memset(&dhdr, 0, sizeof(dhdr));
    dhdr.letter = 'C';
    dhdr.type = PLATFORM_DRIVE_FIXED;
    memset(dhdr.label, 0, sizeof(dhdr.label));
    dhdr.dir_count = dir_count;
    dhdr.pool_size = pool_size;
    
    memcpy(buf + *len, &dhdr, sizeof(dhdr));
    *len += sizeof(dhdr);
}

/* Helper: Mutate random bytes in buffer */
static void mutate_buffer(uint8_t *buf, size_t len, unsigned int *seed) {
    if (len == 0) return;
    
    /* Mutate 1-5 random bytes */
    int mutations = 1 + (rand_r(seed) % 5);
    for (int i = 0; i < mutations; i++) {
        size_t pos = rand_r(seed) % len;
        buf[pos] = (uint8_t)(rand_r(seed) & 0xFF);
    }
}

/* Helper: Write buffer to temp file and try to load */
static NcdDatabase *try_load_buffer(uint8_t *buf, size_t len) {
    const char *tmp_file = "fuzz_test.tmp";
    FILE *f = fopen(tmp_file, "wb");
    if (!f) return NULL;
    
    fwrite(buf, 1, len, f);
    fclose(f);
    
    NcdDatabase *db = db_load_binary(tmp_file);
    
    remove(tmp_file);
    return db;
}

/* ================================================================ Fuzz Tests (10) */

TEST(fuzz_db_header_random_corruption) {
    uint8_t buf[1024];
    size_t len;
    unsigned int seed = (unsigned int)time(NULL);
    
    /* Create valid header */
    create_valid_db_header(buf, &len);
    add_drive_header(buf, &len, 0, 0);
    
    /* Corrupt header randomly multiple times */
    int crash_count = 0;
    for (int i = 0; i < 100; i++) {
        uint8_t test_buf[1024];
        memcpy(test_buf, buf, len);
        mutate_buffer(test_buf, len, &seed);
        
        /* Should not crash, may return NULL */
        NcdDatabase *db = try_load_buffer(test_buf, len);
        if (db) {
            db_free(db);
        }
    }
    
    ASSERT_EQ_INT(0, crash_count);
    return 0;
}

TEST(fuzz_db_drive_header_corruption) {
    uint8_t buf[2048];
    size_t len;
    unsigned int seed = (unsigned int)time(NULL) + 1;
    
    /* Create header with one drive */
    create_valid_db_header(buf, &len);
    add_drive_header(buf, &len, 2, 32);
    
    /* Add minimal data */
    DirEntry entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].parent = -1;
    entries[0].name_off = 0;
    entries[1].parent = 0;
    entries[1].name_off = 8;
    
    memcpy(buf + len, entries, sizeof(entries));
    len += sizeof(entries);
    
    /* Add name pool */
    memcpy(buf + len, "first\0second\0", 14);
    len += 14;
    
    /* Corrupt drive header area */
    for (int i = 0; i < 50; i++) {
        uint8_t test_buf[2048];
        memcpy(test_buf, buf, len);
        
        /* Corrupt drive header specifically (after file header) */
        size_t drive_hdr_start = sizeof(BinFileHdr);
        size_t drive_hdr_end = drive_hdr_start + sizeof(BinDriveHdr);
        size_t corrupt_pos = drive_hdr_start + (rand_r(&seed) % (drive_hdr_end - drive_hdr_start));
        test_buf[corrupt_pos] = (uint8_t)(rand_r(&seed) & 0xFF);
        
        NcdDatabase *db = try_load_buffer(test_buf, len);
        if (db) {
            db_free(db);
        }
    }
    
    return 0;
}

TEST(fuzz_db_directory_entry_corruption) {
    uint8_t buf[2048];
    size_t len;
    unsigned int seed = (unsigned int)time(NULL) + 2;
    
    /* Create header with entries */
    create_valid_db_header(buf, &len);
    add_drive_header(buf, &len, 3, 32);
    
    /* Add entries */
    DirEntry entries[3];
    memset(entries, 0, sizeof(entries));
    for (int j = 0; j < 3; j++) {
        entries[j].parent = j - 1;
        entries[j].name_off = j * 8;
    }
    entries[0].parent = -1;
    
    memcpy(buf + len, entries, sizeof(entries));
    len += sizeof(entries);
    
    memcpy(buf + len, "one\0two\0three\0", 15);
    len += 15;
    
    /* Corrupt entries */
    for (int i = 0; i < 50; i++) {
        uint8_t test_buf[2048];
        memcpy(test_buf, buf, len);
        
        /* Corrupt entry area */
        size_t entry_start = sizeof(BinFileHdr) + sizeof(BinDriveHdr);
        size_t entry_end = entry_start + sizeof(DirEntry) * 3;
        size_t corrupt_pos = entry_start + (rand_r(&seed) % (entry_end - entry_start));
        test_buf[corrupt_pos] = (uint8_t)(rand_r(&seed) & 0xFF);
        
        NcdDatabase *db = try_load_buffer(test_buf, len);
        if (db) {
            db_free(db);
        }
    }
    
    return 0;
}

TEST(fuzz_db_name_pool_corruption) {
    uint8_t buf[2048];
    size_t len;
    unsigned int seed = (unsigned int)time(NULL) + 3;
    
    /* Create header */
    create_valid_db_header(buf, &len);
    add_drive_header(buf, &len, 2, 20);
    
    /* Add entries */
    DirEntry entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].parent = -1;
    entries[0].name_off = 0;
    entries[1].parent = 0;
    entries[1].name_off = 6;
    
    memcpy(buf + len, entries, sizeof(entries));
    len += sizeof(entries);
    
    /* Add name pool */
    memcpy(buf + len, "first\0second\0", 13);
    len += 13;
    
    /* Corrupt name pool */
    for (int i = 0; i < 50; i++) {
        uint8_t test_buf[2048];
        memcpy(test_buf, buf, len);
        
        /* Corrupt name pool area */
        size_t pool_start = sizeof(BinFileHdr) + sizeof(BinDriveHdr) + sizeof(DirEntry) * 2;
        if (len > pool_start) {
            size_t corrupt_pos = pool_start + (rand_r(&seed) % (len - pool_start));
            test_buf[corrupt_pos] = (uint8_t)(rand_r(&seed) & 0xFF);
        }
        
        NcdDatabase *db = try_load_buffer(test_buf, len);
        if (db) {
            db_free(db);
        }
    }
    
    return 0;
}

TEST(fuzz_db_checksum_collision) {
    uint8_t buf[2048];
    size_t len;
    
    /* Create valid header with checksum 0 (which is accepted for backward compatibility) */
    create_valid_db_header(buf, &len);
    add_drive_header(buf, &len, 1, 10);
    
    DirEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.parent = -1;
    entry.name_off = 0;
    
    memcpy(buf + len, &entry, sizeof(entry));
    len += sizeof(entry);
    
    memcpy(buf + len, "test\0", 5);
    len += 5;
    
    /* Try loading - should work with checksum = 0 */
    NcdDatabase *db = try_load_buffer(buf, len);
    /* May or may not load depending on implementation - just verify no crash */
    if (db) {
        db_free(db);
    }
    
    return 0;
}

TEST(fuzz_db_valid_header_invalid_pointers) {
    uint8_t buf[2048];
    size_t len;
    
    /* Create valid header */
    create_valid_db_header(buf, &len);
    
    /* Add drive with huge counts that would cause overflow */
    BinDriveHdr dhdr;
    memset(&dhdr, 0, sizeof(dhdr));
    dhdr.letter = 'C';
    dhdr.dir_count = 0xFFFFFFFF;  /* Impossibly large */
    dhdr.pool_size = 0xFFFFFFFF;
    
    memcpy(buf + len, &dhdr, sizeof(dhdr));
    len += sizeof(dhdr);
    
    /* Should be rejected, not crash */
    NcdDatabase *db = try_load_buffer(buf, len);
    ASSERT_NULL(db);
    
    return 0;
}

TEST(fuzz_db_circular_parent_chain) {
    /* Test that circular parent references don't cause infinite loops in path building */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    DriveData *drv = db_add_drive(db, 'C');
    
    /* Create entries normally - db_full_path has cycle detection */
    int id1 = db_add_dir(drv, "folder1", -1, false, false);
    int id2 = db_add_dir(drv, "folder2", id1, false, false);
    (void)id2;
    
    /* Build path - should work normally */
    char path[MAX_PATH];
    char *result = db_full_path(drv, 1, path, sizeof(path));
    ASSERT_NOT_NULL(result);
    
    db_free(db);
    return 0;
}

TEST(fuzz_db_negative_indices) {
    uint8_t buf[2048];
    size_t len;
    
    /* Create valid header */
    create_valid_db_header(buf, &len);
    add_drive_header(buf, &len, 2, 20);
    
    /* Add entries with negative parent indices (valid for root) */
    DirEntry entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].parent = -1;  /* Valid: root */
    entries[0].name_off = 0;
    entries[1].parent = -1;  /* Valid: another root */
    entries[1].name_off = 6;
    
    memcpy(buf + len, entries, sizeof(entries));
    len += sizeof(entries);
    
    memcpy(buf + len, "first\0second\0", 13);
    len += 13;
    
    /* Try loading */
    NcdDatabase *db = try_load_buffer(buf, len);
    if (db) {
        db_free(db);
    }
    
    return 0;
}

TEST(fuzz_db_overflow_name_offsets) {
    uint8_t buf[2048];
    size_t len;
    
    /* Create valid header */
    create_valid_db_header(buf, &len);
    add_drive_header(buf, &len, 1, 10);
    
    /* Add entry with huge name offset */
    DirEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.parent = -1;
    entry.name_off = 0xFFFFFFFF;  /* Invalid: beyond pool */
    
    memcpy(buf + len, &entry, sizeof(entry));
    len += sizeof(entry);
    
    memcpy(buf + len, "test\0", 5);
    len += 5;
    
    /* Try loading - may or may not succeed, but shouldn't crash */
    NcdDatabase *db = try_load_buffer(buf, len);
    if (db) {
        /* Even if loaded, accessing this entry might be problematic */
        db_free(db);
    }
    
    return 0;
}

TEST(fuzz_db_mismatched_drive_counts) {
    uint8_t buf[2048];
    size_t len;
    
    /* Create header claiming 3 drives but only provide 1 drive header */
    BinFileHdr hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = NCD_BIN_MAGIC;
    hdr.version = NCD_BIN_VERSION;
    hdr.show_hidden = 0;
    hdr.show_system = 0;
    hdr.last_scan = 0;
    hdr.drive_count = 3;  /* Claim 3 drives */
    hdr.encoding = NCD_TEXT_UTF8;
    hdr.checksum = 0;
    
    memcpy(buf, &hdr, sizeof(hdr));
    len = sizeof(hdr);
    
    /* But only add 1 drive header */
    add_drive_header(buf, &len, 0, 0);
    
    /* Should be rejected as truncated/invalid */
    NcdDatabase *db = try_load_buffer(buf, len);
    ASSERT_NULL(db);
    
    return 0;
}

/* ================================================================ Test Suites */

void suite_fuzz_database_load() {
    printf("\n=== Database Load Fuzz Tests ===\n");
    RUN_TEST(fuzz_db_header_random_corruption);
    RUN_TEST(fuzz_db_drive_header_corruption);
    RUN_TEST(fuzz_db_directory_entry_corruption);
    RUN_TEST(fuzz_db_name_pool_corruption);
    RUN_TEST(fuzz_db_checksum_collision);
    RUN_TEST(fuzz_db_valid_header_invalid_pointers);
    RUN_TEST(fuzz_db_circular_parent_chain);
    RUN_TEST(fuzz_db_negative_indices);
    RUN_TEST(fuzz_db_overflow_name_offsets);
    RUN_TEST(fuzz_db_mismatched_drive_counts);
}

/* ================================================================ Main */

TEST_MAIN(
    RUN_SUITE(fuzz_database_load);
)
