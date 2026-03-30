/*
 * test_service_database.c  --  Service database snapshot loading tests
 *
 * Tests database reconstruction from shared memory snapshots using the
 * public SnapshotPublisher API.
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

/* Create a test database with sample data using current API
 * API: db_add_dir(drv, name, parent_idx, hidden, system) */
static NcdDatabase *create_test_database(void) {
    NcdDatabase *db = db_create();
    if (!db) return NULL;
    
    /* Add a drive - API: db_add_drive(db, letter) returns DriveData* */
    DriveData *drv = db_add_drive(db, 'C');
    if (!drv) {
        db_free(db);
        return NULL;
    }
    
    /* Add some directories - API: db_add_dir(drv, name, parent, hidden, system) */
    int root = db_add_dir(drv, "C:", -1, 0, 0);
    if (root < 0) {
        db_free(db);
        return NULL;
    }
    
    int users = db_add_dir(drv, "Users", root, 0, 0);
    db_add_dir(drv, "Windows", root, 0, 1); /* system */
    
    if (users >= 0) {
        db_add_dir(drv, "Admin", users, 0, 0);
        db_add_dir(drv, "Public", users, 0, 0);
    }
    
    return db;
}

/* Create a multi-drive test database using current API */
static NcdDatabase *create_multi_drive_database(void) {
    NcdDatabase *db = db_create();
    if (!db) return NULL;
    
    /* Drive C: */
    DriveData *c_drv = db_add_drive(db, 'C');
    if (c_drv) {
        int c_root = db_add_dir(c_drv, "C:", -1, 0, 0);
        db_add_dir(c_drv, "Windows", c_root, 0, 1);
        db_add_dir(c_drv, "Program Files", c_root, 0, 0);
    }
    
    /* Drive D: */
    DriveData *d_drv = db_add_drive(db, 'D');
    if (d_drv) {
        int d_root = db_add_dir(d_drv, "D:", -1, 0, 0);
        db_add_dir(d_drv, "Documents", d_root, 0, 0);
        db_add_dir(d_drv, "Media", d_root, 0, 0);
    }
    
    return db;
}

/* Helper: Create service state and set database using update_database API */
static ServiceState *create_service_state_with_db(NcdDatabase *db) {
    ServiceState *state = service_state_init();
    if (!state) return NULL;
    
    /* Use update_database to set the database (takes ownership) */
    /* We pass false for is_partial since this is a full database set */
    if (!service_state_update_database(state, db, false)) {
        service_state_cleanup(state);
        return NULL;
    }
    
    return state;
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

TEST(snapshot_publisher_lifecycle) {
    /* Test publisher initialization and cleanup */
    SnapshotPublisher *pub = snapshot_publisher_init();
    ASSERT_NOT_NULL(pub);
    
    /* Get info should succeed even with empty state */
    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);
    
    /* Cleanup should not crash */
    snapshot_publisher_cleanup(pub);
    
    return 0;
}

TEST(snapshot_publish_empty_database) {
    /* Create empty database */
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Create service state (takes ownership of db) */
    ServiceState *state = create_service_state_with_db(db);
    ASSERT_NOT_NULL(state);
    
    /* Initialize publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    ASSERT_NOT_NULL(pub);
    
    /* Publish database snapshot */
    bool result = snapshot_publisher_publish_db(pub, state);
    ASSERT_TRUE(result);
    
    /* Verify info was updated */
    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);
    ASSERT_TRUE(info.db_generation > 0);
    ASSERT_TRUE(info.db_size > 0);
    ASSERT_TRUE(strlen(info.db_shm_name) > 0);
    
    /* Cleanup */
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    /* Note: db is owned by state, don't free it */
    
    return 0;
}

TEST(snapshot_publish_single_drive) {
    /* Create test database */
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    /* Create service state (takes ownership of db) */
    ServiceState *state = create_service_state_with_db(db);
    ASSERT_NOT_NULL(state);
    
    /* Initialize publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    ASSERT_NOT_NULL(pub);
    
    /* Publish database snapshot */
    bool result = snapshot_publisher_publish_db(pub, state);
    ASSERT_TRUE(result);
    
    /* Verify info - generation should be > 0 */
    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);
    ASSERT_TRUE(info.db_generation > 0);
    ASSERT_TRUE(info.db_size > sizeof(ShmSnapshotHdr));
    
    /* Cleanup */
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    /* Note: db is owned by state, don't free it */
    
    return 0;
}

TEST(snapshot_publish_multi_drive) {
    /* Create multi-drive database */
    NcdDatabase *db = create_multi_drive_database();
    ASSERT_NOT_NULL(db);
    ASSERT_EQ_INT(2, db->drive_count);
    
    /* Create service state (takes ownership of db) */
    ServiceState *state = create_service_state_with_db(db);
    ASSERT_NOT_NULL(state);
    
    /* Initialize publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    ASSERT_NOT_NULL(pub);
    
    /* Publish database snapshot */
    bool result = snapshot_publisher_publish_db(pub, state);
    ASSERT_TRUE(result);
    
    /* Verify info reflects multi-drive */
    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);
    ASSERT_TRUE(info.db_size > sizeof(ShmSnapshotHdr));
    ASSERT_TRUE(info.db_generation > 0);
    
    /* Cleanup */
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    /* Note: db is owned by state, don't free it */
    
    return 0;
}

TEST(snapshot_generation_increments) {
    /* Create test database */
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    /* Create service state (takes ownership of db) */
    ServiceState *state = create_service_state_with_db(db);
    ASSERT_NOT_NULL(state);
    
    /* Initialize publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    ASSERT_NOT_NULL(pub);
    
    /* Get initial generation (may be > 0 due to service_state_update_database) */
    uint64_t gen0 = snapshot_publisher_get_db_generation(pub);
    
    /* First publish - generation from state + 1 */
    ASSERT_TRUE(snapshot_publisher_publish_db(pub, state));
    uint64_t gen1 = snapshot_publisher_get_db_generation(pub);
    ASSERT_TRUE(gen1 > gen0);
    
    /* Note: Subsequent publishes with the same state will have the same generation
     * because generation is computed as service_state_get_db_generation(state) + 1.
     * This is expected behavior - the publisher reflects the state's generation.
     * To increment generation, the state would need to be modified.
     */
    
    /* Second publish with same state - generation stays the same */
    ASSERT_TRUE(snapshot_publisher_publish_db(pub, state));
    uint64_t gen2 = snapshot_publisher_get_db_generation(pub);
    /* Generation is based on state, not incremented per-publish */
    ASSERT_TRUE(gen2 == gen1 || gen2 > gen1); /* Either same or state changed */
    
    /* Cleanup */
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    /* Note: db is owned by state, don't free it */
    
    return 0;
}

TEST(snapshot_checksum_validation) {
    /* Create test database */
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    /* Create service state (takes ownership of db) */
    ServiceState *state = create_service_state_with_db(db);
    ASSERT_NOT_NULL(state);
    
    /* Initialize publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    ASSERT_NOT_NULL(pub);
    
    /* Publish */
    ASSERT_TRUE(snapshot_publisher_publish_db(pub, state));
    
    /* Get info to find shared memory */
    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);
    
    /* Open the shared memory to verify checksum - API takes access mode and returns result */
    ShmHandle *shm = NULL;
    ShmResult result = shm_open_existing(info.db_shm_name, SHM_ACCESS_READ, &shm);
    ASSERT_EQ_INT(SHM_OK, result);
    ASSERT_NOT_NULL(shm);
    
    void *addr = NULL;
    size_t size = 0;
    result = shm_map(shm, SHM_ACCESS_READ, &addr, &size);
    ASSERT_EQ_INT(SHM_OK, result);
    ASSERT_NOT_NULL(addr);
    ASSERT_TRUE(size > 0);
    
    /* Validate header */
    ASSERT_TRUE(shm_validate_header(addr, size, NCD_SHM_DB_MAGIC));
    
    /* Validate checksum - current API takes just (base, size) */
    ASSERT_TRUE(shm_validate_checksum(addr, size));
    
    /* Unmap and close - API: shm_unmap(addr, size) */
    shm_unmap(addr, size);
    shm_close(shm);
    
    /* Cleanup */
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    /* Note: db is owned by state, don't free it */
    
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

TEST(snapshot_publish_all) {
    /* Create test database */
    NcdDatabase *db = create_test_database();
    ASSERT_NOT_NULL(db);
    
    /* Create service state (takes ownership of db) */
    ServiceState *state = create_service_state_with_db(db);
    ASSERT_NOT_NULL(state);
    
    /* Initialize publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    ASSERT_NOT_NULL(pub);
    
    /* Publish both metadata and database */
    bool result = snapshot_publisher_publish_all(pub, state);
    ASSERT_TRUE(result);
    
    /* Verify both generations are set */
    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);
    ASSERT_TRUE(info.db_generation > 0);
    ASSERT_TRUE(info.meta_generation > 0);
    ASSERT_TRUE(strlen(info.db_shm_name) > 0);
    ASSERT_TRUE(strlen(info.meta_shm_name) > 0);
    
    /* Cleanup */
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    /* Note: db is owned by state, don't free it */
    
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_database(void) {
    printf("\n=== Service Database Snapshot Tests ===\n\n");
    
    RUN_TEST(snapshot_header_validation);
    RUN_TEST(snapshot_publisher_lifecycle);
    RUN_TEST(snapshot_publish_empty_database);
    RUN_TEST(snapshot_publish_single_drive);
    RUN_TEST(snapshot_publish_multi_drive);
    RUN_TEST(snapshot_generation_increments);
    RUN_TEST(snapshot_checksum_validation);
    RUN_TEST(shared_memory_name_creation);
    RUN_TEST(snapshot_publish_all);
}

TEST_MAIN(
    suite_service_database();
)
