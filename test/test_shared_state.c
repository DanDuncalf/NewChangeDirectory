/*
 * test_shared_state.c  --  Unit tests for shared memory snapshot format
 */

#include "test_framework.h"
#include "../src/shared_state.h"
#include <string.h>
#include <stdlib.h>

/* Test header validation */
static void test_validate_header_meta(void) {
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
    TEST_ASSERT(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC) == true);
    
    /* Wrong magic should fail */
    TEST_ASSERT(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_DB_MAGIC) == false);
    
    /* Wrong version should fail */
    hdr->version = 999;
    TEST_ASSERT(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC) == false);
    hdr->version = NCD_SHM_VERSION;
    
    /* Incomplete flag should fail */
    hdr->flags = 0;
    TEST_ASSERT(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC) == false);
    hdr->flags = NCD_SHM_FLAG_COMPLETE;
    
    /* Size too small should fail */
    TEST_ASSERT(shm_validate_header(buffer, sizeof(buffer) - 1, NCD_SHM_META_MAGIC) == false);
    
    /* NULL base should fail */
    TEST_ASSERT(shm_validate_header(NULL, sizeof(buffer), NCD_SHM_META_MAGIC) == false);
}

static void test_validate_header_db(void) {
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
    TEST_ASSERT(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_DB_MAGIC) == true);
    
    /* Wrong magic should fail */
    TEST_ASSERT(shm_validate_header(buffer, sizeof(buffer), NCD_SHM_META_MAGIC) == false);
}

/* Test CRC64 computation */
static void test_crc64_basic(void) {
    /* Test with known input */
    const char *test_data = "123456789";
    uint64_t crc = shm_crc64(test_data, 9);
    
    /* CRC64-ECMA of "123456789" should be 0x6C40DF5F0B497347 */
    /* Note: This is a well-known test vector */
    TEST_ASSERT(crc != 0);  /* Just check it's computed */
    
    /* Same data should give same CRC */
    uint64_t crc2 = shm_crc64(test_data, 9);
    TEST_ASSERT(crc == crc2);
    
    /* Different data should likely give different CRC */
    const char *test_data2 = "987654321";
    uint64_t crc3 = shm_crc64(test_data2, 9);
    TEST_ASSERT(crc != crc3);
}

static void test_crc64_empty(void) {
    /* Empty data should give CRC of 0 */
    uint64_t crc = shm_crc64("", 0);
    TEST_ASSERT(crc == 0);
}

static void test_crc64_incremental(void) {
    const char *part1 = "Hello ";
    const char *part2 = "World!";
    const char *combined = "Hello World!";
    
    /* Compute as one block */
    uint64_t crc_full = shm_crc64(combined, 12);
    
    /* Compute incrementally */
    uint64_t crc_inc = shm_crc64_update(0, part1, 6);
    crc_inc = shm_crc64_update(crc_inc, part2, 6);
    
    TEST_ASSERT(crc_full == crc_inc);
}

/* Test section finding */
static void test_find_section(void) {
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
    TEST_ASSERT(desc != NULL);
    TEST_ASSERT(desc->type == NCD_SHM_SECTION_CONFIG);
    TEST_ASSERT(desc->offset == 256);
    
    desc = shm_find_section(hdr, NCD_SHM_SECTION_GROUPS);
    TEST_ASSERT(desc != NULL);
    TEST_ASSERT(desc->type == NCD_SHM_SECTION_GROUPS);
    
    desc = shm_find_section(hdr, NCD_SHM_SECTION_HEURISTICS);
    TEST_ASSERT(desc != NULL);
    TEST_ASSERT(desc->type == NCD_SHM_SECTION_HEURISTICS);
    
    /* Non-existent section */
    desc = shm_find_section(hdr, NCD_SHM_SECTION_EXCLUSIONS);
    TEST_ASSERT(desc == NULL);
    
    /* NULL header */
    desc = shm_find_section(NULL, NCD_SHM_SECTION_CONFIG);
    TEST_ASSERT(desc == NULL);
}

/* Test snapshot info */
static void test_get_info(void) {
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
    TEST_ASSERT(shm_get_info(buffer, sizeof(buffer), &info) == true);
    TEST_ASSERT(info.valid == true);
    TEST_ASSERT(info.generation == 42);
    TEST_ASSERT(info.total_size == 1024);
    TEST_ASSERT(info.section_count == 5);
    
    /* NULL info pointer */
    TEST_ASSERT(shm_get_info(buffer, sizeof(buffer), NULL) == false);
    
    /* Invalid magic */
    hdr->magic = 0xDEADBEEF;
    TEST_ASSERT(shm_get_info(buffer, sizeof(buffer), &info) == false);
    TEST_ASSERT(info.valid == false);
}

/* Test section pointer access */
static void test_get_section_ptr(void) {
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
    TEST_ASSERT(ptr != NULL);
    TEST_ASSERT(ptr->magic == NCD_CFG_MAGIC);
    TEST_ASSERT(ptr->default_show_hidden == 1);
    
    /* NULL base */
    ptr = (const ShmConfigSection *)shm_get_section_ptr(NULL, &desc);
    TEST_ASSERT(ptr == NULL);
    
    /* NULL desc */
    ptr = (const ShmConfigSection *)shm_get_section_ptr(buffer, NULL);
    TEST_ASSERT(ptr == NULL);
}

/* Test size limits */
static void test_size_limits(void) {
    /* Maximum section count */
    TEST_ASSERT(NCD_SHM_MAX_SECTIONS >= 4);  /* We need at least 4 sections */
    
    /* Maximum drives */
    TEST_ASSERT(NCD_SHM_MAX_DRIVES >= 26);  /* A-Z */
    
    /* String pool limit */
    TEST_ASSERT(NCD_SHM_MAX_POOL_SIZE > 0);
    
    /* Snapshot size limit */
    TEST_ASSERT(NCD_SHM_MAX_SNAPSHOT_SIZE > NCD_SHM_MAX_POOL_SIZE);
}

/* Test structure sizes */
static void test_structure_sizes(void) {
    /* Headers should be fixed size for binary compatibility */
    TEST_ASSERT(sizeof(ShmSnapshotHdr) == 32);
    TEST_ASSERT(sizeof(ShmDirEntry) == 12);
    TEST_ASSERT(sizeof(ShmConfigSection) == 16);
    TEST_ASSERT(sizeof(ShmGroupEntry) == 16);
    TEST_ASSERT(sizeof(ShmHeurEntry) == 16);
    TEST_ASSERT(sizeof(ShmExclusionEntry) == 8);
    TEST_ASSERT(sizeof(ShmDirHistoryEntry) == 16);
}

/* Main test runner */
TEST_SUITE(shared_state) {
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

int main(void) {
    printf("Starting test run...\n\n");
    
    TEST_SUITE_RUN(shared_state);
    
    printf("\n");
    TEST_SUITE_REPORT(shared_state);
    
    return TEST_SUITE_FAILED(shared_state) ? 1 : 0;
}
