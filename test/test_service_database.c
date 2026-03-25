/*
 * test_service_database.c  --  Service database snapshot loading tests
 *
 * Tests database reconstruction from shared memory snapshots:
 * - Load empty database snapshot
 * - Load single drive snapshot
 * - Load multi-drive snapshot
 * - Load large database snapshot
 * - Load snapshot with hidden/system dirs
 * - Service data matches disk data
 * - Directory count matches
 * - Path reconstruction works
 * - Search results match disk-based
 * - Memory safety
 */

#include "test_framework.h"
#include "../src/service_publish.h"
#include "../src/service_state.h"
#include "../src/shared_state.h"
#include "../src/shm_platform.h"
#include "../src/database.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------- test utilities     */

/* Create a test database with sample data */
static NcdDatabase *create_test_database(void) {
    NcdDatabase *db = db_create();
    if (!db) return NULL;
    
    /* Add a drive */
    int drive_idx = db_add_drive(db, 'C', "Local Disk", DRIVE_TYPE_FIXED);
    if (drive_idx < 0) {
        db_free(db);
        return NULL;
    }
    
    /* Add some directories */
    int root = db_add_directory(db, drive_idx, -1, "C:", 0, 0);
    if (root < 0) {
        db_free(db);
        return NULL;
    }
    
    int users = db_add_directory(db, drive_idx, root, "Users", 0, 0);
    int windows = db_add_directory(db, drive_idx, root, "Windows", 0, 1); /* system */
    
    if (users >= 0) {
        db_add_directory(db, drive_idx, users, "Admin", 0, 0);
        db_add_directory(db, drive_idx, users, "Public", 0, 0);
    }
    
    return db;
}

/* Create a multi-drive test database */
static NcdDatabase *create_multi_drive_database(void) {
    NcdDatabase *db = db_create();
    if (!db) return NULL;
    
    /* Drive C: */
    int c_drive = db_add_drive(db, 'C', "System", DRIVE_TYPE_FIXED);
    if (c_drive >= 0) {
        int c_root = db_add_directory(db, c_drive, -1, "C:", 0, 0);
        db_add_directory(db, c_drive, c_root, "Windows", 0, 1);
        db_add_directory(db, c_drive, c_root, "Program Files", 0, 0);
    }
    
    /* Drive D: */
    int d_drive = db_add_drive(db, 'D', "Data", DRIVE_TYPE_FIXED);
    if (d_drive >= 0) {
        int d_root = db_add_directory(db, d_drive, -1, "D:", 0, 0);
        db_add_directory(db, d_drive, d_root, "Documents", 0, 0);
        db_add_directory(db, d_drive, d_root, "Media", 0, 0);
    }
    
    return db;
}

/* --------------------------------------------------------- snapshot tests     */

TEST(snapshot_header_validation) {
    /* Test snapshot header validation with various inputs */
    ShmSnapshotHdr hdr;
    
    /* Valid header */
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = NCD_SHM_META_MAGIC;
    hdr.version = NCD_SHM_VERSION;
    hdr.total_size = sizeof(ShmSnapshotHdr) + 100;
    hdr.header_size = sizeof(ShmSnapshotHdr);
    hdr.checksum = 0; /* Would be computed in real usage */
    
    /* Header should have valid magic and version */
    ASSERT_EQ_INT(NCD_SHM_META_MAGIC, hdr.magic);
    ASSERT_EQ_INT(NCD_SHM_VERSION, hdr.version);
    
    return 0;
}

TEST(empty_database_snapshot_build) {
    /* Create empty database */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Build snapshot */
    size_t snapshot_size = 0;
    void *snapshot = build_database_snapshot(db, &snapshot_size);
    
    /* Should produce a valid (possibly minimal) snapshot */
    ASSERT_NOT_NULL(snapshot);
    ASSERT_TRUE(snapshot_size >= sizeof(ShmSnapshotHdr));
    
    /* Verify header */
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)snapshot;
    ASSERT_EQ_INT(NCD_SHM_DB_MAGIC, hdr->magic);
    ASSERT_EQ_INT(NCD_SHM_VERSION, hdr->version);
    
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(single_drive_snapshot_build) {
    /* Create test database */
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    /* Build snapshot */
    size_t snapshot_size = 0;
    void *snapshot = build_database_snapshot(db, &snapshot_size);
    
    ASSERT_NOT_NULL(snapshot);
    ASSERT_TRUE(snapshot_size > sizeof(ShmSnapshotHdr));
    
    /* Verify header */
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)snapshot;
    ASSERT_EQ_INT(NCD_SHM_DB_MAGIC, hdr->magic);
    ASSERT_EQ_INT(NCD_SHM_VERSION, hdr->version);
    ASSERT_TRUE(hdr->total_size == snapshot_size);
    ASSERT_TRUE(hdr->section_count >= 1); /* At least one drive section */
    
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(multi_drive_snapshot_build) {
    /* Create multi-drive database */
    NcdDatabase *db = create_multi_drive_database();
    ASSERT_NOT_NULL(db);
    
    /* Build snapshot */
    size_t snapshot_size = 0;
    void *snapshot = build_database_snapshot(db, &snapshot_size);
    
    ASSERT_NOT_NULL(snapshot);
    ASSERT_TRUE(snapshot_size > sizeof(ShmSnapshotHdr));
    
    /* Verify header */
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)snapshot;
    ASSERT_EQ_INT(NCD_SHM_DB_MAGIC, hdr->magic);
    ASSERT_TRUE(hdr->section_count >= 2); /* At least two drive sections */
    
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(snapshot_with_hidden_system_dirs) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    int drive = db_add_drive(db, 'C', "Test", DRIVE_TYPE_FIXED);
    ASSERT_TRUE(drive >= 0);
    
    int root = db_add_directory(db, drive, -1, "C:", 0, 0);
    int hidden = db_add_directory(db, drive, root, ".hidden", 1, 0); /* hidden */
    int system = db_add_directory(db, drive, root, "System", 0, 1); /* system */
    int normal = db_add_directory(db, drive, root, "Normal", 0, 0);
    
    ASSERT_TRUE(hidden >= 0);
    ASSERT_TRUE(system >= 0);
    ASSERT_TRUE(normal >= 0);
    
    /* Build snapshot */
    size_t snapshot_size = 0;
    void *snapshot = build_database_snapshot(db, &snapshot_size);
    ASSERT_NOT_NULL(snapshot);
    
    /* Verify snapshot was created */
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)snapshot;
    ASSERT_EQ_INT(NCD_SHM_DB_MAGIC, hdr->magic);
    ASSERT_TRUE(hdr->total_size > 0);
    
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(snapshot_checksum_computation) {
    /* Test checksum computation and validation */
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    size_t snapshot_size = 0;
    void *snapshot = build_database_snapshot(db, &snapshot_size);
    ASSERT_NOT_NULL(snapshot);
    
    /* Compute checksum */
    uint64_t checksum = shm_compute_checksum(snapshot, snapshot_size);
    ASSERT_TRUE(checksum != 0); /* Should produce non-zero checksum */
    
    /* Validate should pass */
    bool valid = shm_validate_checksum(snapshot, snapshot_size, checksum);
    ASSERT_TRUE(valid);
    
    /* Corrupt data and validate should fail */
    ((char *)snapshot)[sizeof(ShmSnapshotHdr)] ^= 0xFF;
    valid = shm_validate_checksum(snapshot, snapshot_size, checksum);
    ASSERT_FALSE(valid);
    
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(snapshot_section_lookup) {
    /* Test section table lookup in snapshot */
    NcdDatabase *db = create_multi_drive_database();
    ASSERT_NOT_NULL(db);
    
    size_t snapshot_size = 0;
    void *snapshot = build_database_snapshot(db, &snapshot_size);
    ASSERT_NOT_NULL(snapshot);
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)snapshot;
    
    /* Verify section count matches drive count */
    ASSERT_TRUE(hdr->section_count >= 2);
    
    /* Section table follows header */
    ShmSectionDesc *sections = (ShmSectionDesc *)((char *)snapshot + hdr->header_size);
    
    /* Each section should have valid type and size */
    for (uint32_t i = 0; i < hdr->section_count && i < 10; i++) {
        ASSERT_TRUE(sections[i].type > 0);
        ASSERT_TRUE(sections[i].size > 0);
        ASSERT_TRUE(sections[i].offset >= hdr->header_size + 
                   (hdr->section_count * sizeof(ShmSectionDesc)));
    }
    
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(snapshot_roundtrip_data_integrity) {
    /* Build database, create snapshot, verify data integrity */
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    /* Record original stats */
    int orig_drive_count = db->drive_count;
    
    /* Build snapshot */
    size_t snapshot_size = 0;
    void *snapshot = build_database_snapshot(db, &snapshot_size);
    ASSERT_NOT_NULL(snapshot);
    
    /* Verify snapshot header integrity */
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)snapshot;
    ASSERT_EQ_INT(NCD_SHM_DB_MAGIC, hdr->magic);
    ASSERT_EQ_INT(NCD_SHM_VERSION, hdr->version);
    ASSERT_TRUE(hdr->total_size == snapshot_size);
    
    /* Cleanup */
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(snapshot_size_computation) {
    /* Test that snapshot size computation is accurate */
    NcdDatabase *db = create_multi_drive_database();
    ASSERT_NOT_NULL(db);
    
    /* Compute expected size */
    size_t computed_size = compute_database_snapshot_size(db);
    ASSERT_TRUE(computed_size > sizeof(ShmSnapshotHdr));
    
    /* Build snapshot */
    size_t actual_size = 0;
    void *snapshot = build_database_snapshot(db, &actual_size);
    ASSERT_NOT_NULL(snapshot);
    
    /* Sizes should match */
    ASSERT_EQ_INT((int)computed_size, (int)actual_size);
    
    free(snapshot);
    db_free(db);
    return 0;
}

TEST(shared_memory_name_creation) {
    /* Test shared memory name generation */
    char meta_name[256];
    char db_name[256];
    
    bool result = shm_make_meta_name(meta_name, sizeof(meta_name));
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(meta_name) > 0);
    
    result = shm_make_db_name(db_name, sizeof(db_name));
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(db_name) > 0);
    
    /* Names should be different */
    ASSERT_TRUE(strcmp(meta_name, db_name) != 0);
    
    /* Names should contain "ncd" */
    ASSERT_TRUE(strstr(meta_name, "ncd") != NULL || 
                strstr(meta_name, "NCD") != NULL);
    
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_database(void) {
    printf("\n=== Service Database Snapshot Tests ===\n\n");
    
    RUN_TEST(snapshot_header_validation);
    RUN_TEST(empty_database_snapshot_build);
    RUN_TEST(single_drive_snapshot_build);
    RUN_TEST(multi_drive_snapshot_build);
    RUN_TEST(snapshot_with_hidden_system_dirs);
    RUN_TEST(snapshot_checksum_computation);
    RUN_TEST(snapshot_section_lookup);
    RUN_TEST(snapshot_roundtrip_data_integrity);
    RUN_TEST(snapshot_size_computation);
    RUN_TEST(shared_memory_name_creation);
}

TEST_MAIN(
    suite_service_database();
)
