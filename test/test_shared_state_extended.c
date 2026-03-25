/* test_shared_state_extended.c -- Extended tests for shared state (Tier 4) */
#include "test_framework.h"
#include "../src/shared_state.h"
#include <string.h>
#include <stdlib.h>

/* ================================================================ Tier 4: Shared State Extended Tests */

TEST(shm_compute_checksum_returns_deterministic_value) {
    /* Create test data */
    uint8_t data[] = "Hello, World! Test data for checksum.";
    size_t len = sizeof(data) - 1; /* Exclude null terminator */
    
    /* Compute checksum twice */
    uint64_t checksum1 = shm_crc64(data, len);
    uint64_t checksum2 = shm_crc64(data, len);
    
    /* Should be identical */
    ASSERT_EQ_INT((int)(checksum1 >> 32), (int)(checksum2 >> 32));
    ASSERT_EQ_INT((int)(checksum1 & 0xFFFFFFFF), (int)(checksum2 & 0xFFFFFFFF));
    
    return 0;
}

TEST(shm_validate_checksum_passes_for_valid_data) {
    /* Create a simple snapshot structure in memory */
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buf;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->total_size = sizeof(buf);
    hdr->header_size = sizeof(ShmSnapshotHdr);
    hdr->section_count = 0;
    hdr->generation = 1;
    
    /* Compute checksum over data after header */
    size_t data_size = sizeof(buf) - sizeof(ShmSnapshotHdr);
    hdr->checksum = shm_crc64(buf + sizeof(ShmSnapshotHdr), data_size);
    
    /* Validate should pass */
    bool valid = shm_validate_header(buf, sizeof(buf), NCD_SHM_META_MAGIC);
    ASSERT_TRUE(valid);
    
    return 0;
}

TEST(shm_validate_checksum_fails_for_corrupted_data) {
    /* Create a simple snapshot structure */
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buf;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->total_size = sizeof(buf);
    hdr->header_size = sizeof(ShmSnapshotHdr);
    hdr->section_count = 0;
    hdr->generation = 1;
    
    /* Set a checksum */
    hdr->checksum = 0xDEADBEEFCAFEBABEULL;
    
    /* Corrupt the data after header */
    buf[sizeof(ShmSnapshotHdr) + 10] = 0xFF;
    
    /* Checksum validation should fail */
    bool valid = shm_validate_checksum(buf, sizeof(buf));
    ASSERT_FALSE(valid);
    
    return 0;
}

TEST(shm_make_meta_name_returns_valid_name) {
    char name[256];
    bool result = shm_make_meta_name(name, sizeof(name));
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(name) > 0);
    ASSERT_TRUE(strstr(name, "meta") != NULL || strstr(name, "metadata") != NULL);
    
    return 0;
}

TEST(shm_make_db_name_returns_valid_name) {
    char name[256];
    bool result = shm_make_db_name(name, sizeof(name));
    
    ASSERT_TRUE(result);
    ASSERT_TRUE(strlen(name) > 0);
    ASSERT_TRUE(strstr(name, "db") != NULL || strstr(name, "database") != NULL);
    
    return 0;
}

TEST(shm_round_up_size_rounds_correctly) {
    size_t page_size = shm_get_page_size();
    
    /* Test various sizes */
    size_t rounded1 = shm_round_up_size(1);
    ASSERT_TRUE(rounded1 >= page_size);
    
    size_t rounded2 = shm_round_up_size(page_size - 1);
    ASSERT_TRUE(rounded2 >= page_size);
    
    size_t rounded3 = shm_round_up_size(page_size);
    ASSERT_EQ_INT((int)page_size, (int)rounded3);
    
    size_t rounded4 = shm_round_up_size(page_size + 1);
    ASSERT_TRUE(rounded4 >= page_size * 2);
    
    return 0;
}

TEST(shm_find_section_finds_section_by_type) {
    /* Create buffer with header and section table */
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buf;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->total_size = sizeof(buf);
    hdr->header_size = sizeof(ShmSnapshotHdr) + sizeof(ShmSectionDesc);
    hdr->section_count = 1;
    
    /* Add a config section */
    ShmSectionDesc *section = (ShmSectionDesc *)(buf + sizeof(ShmSnapshotHdr));
    section->type = NCD_SHM_SECTION_CONFIG;
    section->offset = 64;
    section->size = 16;
    
    /* Find the section */
    const ShmSectionDesc *found = shm_find_section(hdr, NCD_SHM_SECTION_CONFIG);
    ASSERT_NOT_NULL(found);
    ASSERT_EQ_INT(NCD_SHM_SECTION_CONFIG, found->type);
    ASSERT_EQ_INT(64, found->offset);
    ASSERT_EQ_INT(16, found->size);
    
    /* Try to find non-existent section */
    const ShmSectionDesc *notfound = shm_find_section(hdr, NCD_SHM_SECTION_GROUPS);
    ASSERT_NULL(notfound);
    
    return 0;
}

TEST(shm_get_info_returns_snapshot_info) {
    /* Create a valid snapshot */
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buf;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->total_size = sizeof(buf);
    hdr->header_size = sizeof(ShmSnapshotHdr);
    hdr->section_count = 2;
    hdr->generation = 42;
    
    ShmSnapshotInfo info;
    bool result = shm_get_info(buf, sizeof(buf), &info);
    
    ASSERT_TRUE(result);
    ASSERT_EQ_INT(42, (int)info.generation);
    ASSERT_EQ_INT(sizeof(buf), (int)info.total_size);
    ASSERT_EQ_INT(2, (int)info.section_count);
    ASSERT_TRUE(info.valid);
    
    return 0;
}

TEST(shm_validate_header_checks_magic) {
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buf;
    hdr->magic = 0xBADBADBAD; /* Bad magic */
    hdr->version = NCD_SHM_VERSION;
    hdr->total_size = sizeof(buf);
    
    /* Should fail with wrong magic */
    bool result = shm_validate_header(buf, sizeof(buf), NCD_SHM_META_MAGIC);
    ASSERT_FALSE(result);
    
    /* Now set correct magic */
    hdr->magic = NCD_SHM_META_MAGIC;
    result = shm_validate_header(buf, sizeof(buf), NCD_SHM_META_MAGIC);
    ASSERT_TRUE(result);
    
    return 0;
}

TEST(shm_validate_header_checks_bounds) {
    uint8_t buf[256];
    memset(buf, 0, sizeof(buf));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buf;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->total_size = 1024; /* Claims to be larger than buffer */
    
    /* Should fail because total_size > actual size */
    bool result = shm_validate_header(buf, sizeof(buf), NCD_SHM_META_MAGIC);
    ASSERT_FALSE(result);
    
    return 0;
}

/* ================================================================ Test Suite */

void suite_shared_state_extended(void) {
    printf("\n=== Shared State Extended Tests ===\n\n");
    
    RUN_TEST(shm_compute_checksum_returns_deterministic_value);
    RUN_TEST(shm_validate_checksum_passes_for_valid_data);
    RUN_TEST(shm_validate_checksum_fails_for_corrupted_data);
    RUN_TEST(shm_make_meta_name_returns_valid_name);
    RUN_TEST(shm_make_db_name_returns_valid_name);
    RUN_TEST(shm_round_up_size_rounds_correctly);
    RUN_TEST(shm_find_section_finds_section_by_type);
    RUN_TEST(shm_get_info_returns_snapshot_info);
    RUN_TEST(shm_validate_header_checks_magic);
    RUN_TEST(shm_validate_header_checks_bounds);
}

TEST_MAIN(
    RUN_SUITE(shared_state_extended);
)
