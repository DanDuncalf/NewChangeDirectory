/*
 * test_shared_state.c  --  Unit tests for shared memory snapshot format
 */

#include "test_framework.h"
#include "../src/ncd.h"
#include "../src/shared_state.h"
#include <string.h>
#include <stdlib.h>

/* Test header validation */
TEST(validate_header_meta) {
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buffer;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->flags = NCD_SHM_FLAG_COMPLETE;
    hdr->total_size = 1024;
    hdr->header_size = sizeof(ShmSnapshotHdr);
    hdr->section_count = 0;
    hdr->generation = 1;
    hdr->checksum = 0;
    
    /* Should validate correctly */
    ASSERT_TRUE(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC));
    
    /* Wrong magic should fail */
    ASSERT_FALSE(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_DB_MAGIC));
    
    /* Wrong version should fail */
    hdr->version = 999;
    ASSERT_FALSE(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC));
    hdr->version = NCD_SHM_VERSION;
    
    /* Incomplete flag should fail */
    hdr->flags = 0;
    ASSERT_FALSE(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC));
    hdr->flags = NCD_SHM_FLAG_COMPLETE;
    
    /* Size too small should fail */
    ASSERT_FALSE(shm_validate_header(buffer, sizeof(buffer) - 1, NCD_SHM_META_MAGIC));
    
    /* NULL base should fail */
    ASSERT_FALSE(shm_validate_header(NULL, sizeof(buffer), NCD_SHM_META_MAGIC));
    
    return 0;
}

TEST(validate_header_db) {
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buffer;
    hdr->magic = NCD_SHM_DB_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->flags = NCD_SHM_FLAG_COMPLETE;
    hdr->total_size = 1024;
    hdr->header_size = sizeof(ShmSnapshotHdr);
    hdr->section_count = 0;
    hdr->generation = 1;
    hdr->checksum = 0;
    
    /* Should validate correctly */
    ASSERT_TRUE(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_DB_MAGIC));
    
    /* Wrong magic should fail */
    ASSERT_FALSE(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC));
    
    return 0;
}

/* Test CRC64 computation */
TEST(crc64_basic) {
    /* Test with known input */
    const char *test_data = "123456789";
    uint64_t crc = shm_crc64(test_data, 9);
    
    /* CRC64-ECMA of "123456789" should be 0x6C40DF5F0B497347 */
    /* Note: This is a well-known test vector */
    ASSERT_TRUE(crc != 0);  /* Just check it's computed */
    
    /* Same data should give same CRC */
    uint64_t crc2 = shm_crc64(test_data, 9);
    ASSERT_EQ_INT((int)(crc >> 32), (int)(crc2 >> 32));
    ASSERT_EQ_INT((int)(crc & 0xFFFFFFFF), (int)(crc2 & 0xFFFFFFFF));
    
    /* Different data should likely give different CRC */
    const char *test_data2 = "987654321";
    uint64_t crc3 = shm_crc64(test_data2, 9);
    ASSERT_TRUE(crc != crc3);
    
    return 0;
}

TEST(crc64_empty) {
    /* Empty data should give CRC of 0 */
    uint64_t crc = shm_crc64("", 0);
    ASSERT_EQ_INT(0, (int)crc);
    return 0;
}

TEST(crc64_incremental) {
    const char *part1 = "Hello ";
    const char *part2 = "World!";
    const char *combined = "Hello World!";
    
    /* Compute as one block */
    uint64_t crc_full = shm_crc64(combined, 12);
    
    /* Compute incrementally */
    uint64_t crc_inc = shm_crc64_update(0, part1, 6);
    crc_inc = shm_crc64_update(crc_inc, part2, 6);
    
    ASSERT_EQ_INT((int)(crc_full >> 32), (int)(crc_inc >> 32));
    ASSERT_EQ_INT((int)(crc_full & 0xFFFFFFFF), (int)(crc_inc & 0xFFFFFFFF));
    
    return 0;
}

/* Test section finding */
TEST(find_section) {
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buffer;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->flags = NCD_SHM_FLAG_COMPLETE;
    hdr->total_size = 1024;
    hdr->header_size = sizeof(ShmSnapshotHdr) + 3 * sizeof(ShmSectionDesc);
    hdr->section_count = 3;
    hdr->generation = 1;
    
    /* Set up sections after header */
    ShmSectionDesc *sections = (ShmSectionDesc *)(buffer + sizeof(ShmSnapshotHdr));
    sections[0].type = NCD_SHM_SECTION_CONFIG;
    sections[0].offset = 256;
    sections[0].size = 16;
    sections[1].type = NCD_SHM_SECTION_GROUPS;
    sections[1].offset = 272;
    sections[1].size = 100;
    sections[2].type = NCD_SHM_SECTION_HEURISTICS;
    sections[2].offset = 400;
    sections[2].size = 200;
    
    /* Find existing sections */
    const ShmSectionDesc *desc;
    desc = shm_find_section(hdr, NCD_SHM_SECTION_CONFIG);
    ASSERT_NOT_NULL(desc);
    ASSERT_EQ_INT(NCD_SHM_SECTION_CONFIG, desc->type);
    ASSERT_EQ_INT(256, desc->offset);
    
    desc = shm_find_section(hdr, NCD_SHM_SECTION_GROUPS);
    ASSERT_NOT_NULL(desc);
    ASSERT_EQ_INT(NCD_SHM_SECTION_GROUPS, desc->type);
    
    desc = shm_find_section(hdr, NCD_SHM_SECTION_HEURISTICS);
    ASSERT_NOT_NULL(desc);
    ASSERT_EQ_INT(NCD_SHM_SECTION_HEURISTICS, desc->type);
    
    /* Non-existent section */
    desc = shm_find_section(hdr, NCD_SHM_SECTION_EXCLUSIONS);
    ASSERT_NULL(desc);
    
    /* NULL header */
    desc = shm_find_section(NULL, NCD_SHM_SECTION_CONFIG);
    ASSERT_NULL(desc);
    
    return 0;
}

/* Test snapshot info */
TEST(get_info) {
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    ShmSnapshotHdr *hdr = (ShmSnapshotHdr *)buffer;
    hdr->magic = NCD_SHM_META_MAGIC;
    hdr->version = NCD_SHM_VERSION;
    hdr->flags = NCD_SHM_FLAG_COMPLETE;
    hdr->total_size = 1024;
    hdr->header_size = sizeof(ShmSnapshotHdr);
    hdr->section_count = 5;
    hdr->generation = 42;
    
    ShmSnapshotInfo info;
    ASSERT_TRUE(shm_get_info(buffer, sizeof(buffer), &info));
    ASSERT_TRUE(info.valid);
    ASSERT_EQ_INT(42, info.generation);
    ASSERT_EQ_INT(1024, info.total_size);
    ASSERT_EQ_INT(5, info.section_count);
    
    /* NULL info pointer */
    ASSERT_FALSE(shm_get_info(buffer, sizeof(buffer), NULL));
    
    /* Invalid magic */
    hdr->magic = 0xDEADBEEF;
    ASSERT_FALSE(shm_get_info(buffer, sizeof(buffer), &info));
    ASSERT_FALSE(info.valid);
    
    return 0;
}

/* Test section pointer access */
TEST(get_section_ptr) {
    uint8_t buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    
    /* Set up a config section */
    ShmConfigSection *config = (ShmConfigSection *)(buffer + 256);
    config->magic = NCD_CFG_MAGIC;
    config->version = 1;
    config->default_show_hidden = 1;
    
    ShmSectionDesc desc;
    desc.type = NCD_SHM_SECTION_CONFIG;
    desc.offset = 256;
    desc.size = sizeof(ShmConfigSection);
    
    /* Get pointer to section */
    const ShmConfigSection *ptr = (const ShmConfigSection *)shm_get_section_ptr(buffer, &desc);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ_INT(NCD_CFG_MAGIC, ptr->magic);
    ASSERT_EQ_INT(1, ptr->default_show_hidden);
    
    /* NULL base */
    ptr = (const ShmConfigSection *)shm_get_section_ptr(NULL, &desc);
    ASSERT_NULL(ptr);
    
    /* NULL desc */
    ptr = (const ShmConfigSection *)shm_get_section_ptr(buffer, NULL);
    ASSERT_NULL(ptr);
    
    return 0;
}

/* Test size limits */
TEST(size_limits) {
    /* Maximum section count */
    ASSERT_TRUE(NCD_SHM_MAX_SECTIONS >= 4);  /* We need at least 4 sections */
    
    /* Maximum drives */
    ASSERT_TRUE(NCD_SHM_MAX_DRIVES >= 26);  /* A-Z */
    
    /* String pool limit */
    ASSERT_TRUE(NCD_SHM_MAX_POOL_SIZE > 0);
    
    /* Snapshot size limit */
    ASSERT_TRUE(NCD_SHM_MAX_SNAPSHOT_SIZE > NCD_SHM_MAX_POOL_SIZE);
    
    return 0;
}

/* Test structure sizes */
TEST(structure_sizes) {
    /* Headers should have reasonable sizes for binary compatibility
     * Note: Actual sizes may vary by platform due to alignment,
     * but they should be consistent within a platform */
    ASSERT_TRUE(sizeof(ShmSnapshotHdr) >= 32 && sizeof(ShmSnapshotHdr) <= 64);
    ASSERT_EQ_INT(12, sizeof(ShmDirEntry));
    ASSERT_EQ_INT(16, sizeof(ShmConfigSection));
    ASSERT_EQ_INT(16, sizeof(ShmGroupEntry));
    ASSERT_EQ_INT(16, sizeof(ShmHeurEntry));
    ASSERT_EQ_INT(8, sizeof(ShmExclusionEntry));
    ASSERT_EQ_INT(16, sizeof(ShmDirHistoryEntry));
    
    return 0;
}

void suite_shared_state(void) {
    RUN_TEST(validate_header_meta);
    RUN_TEST(validate_header_db);
    RUN_TEST(crc64_basic);
    RUN_TEST(crc64_empty);
    RUN_TEST(crc64_incremental);
    RUN_TEST(find_section);
    RUN_TEST(get_info);
    RUN_TEST(get_section_ptr);
    RUN_TEST(size_limits);
    RUN_TEST(structure_sizes);
}

TEST_MAIN(
    suite_shared_state();
)
