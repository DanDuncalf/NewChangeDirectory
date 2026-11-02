/*
 * shared_state.c  --  Shared memory snapshot format implementation
 */

#include "shared_state.h"
#include "platform.h"
#include <string.h>

/* --------------------------------------------------------- validation          */

bool shm_validate_header(const void *base, size_t size, uint32_t expected_magic) {
    if (!base || size < sizeof(ShmSnapshotHdr)) {
        return false;
    }
    
    const ShmSnapshotHdr *hdr = (const ShmSnapshotHdr *)base;
    
    /* Check magic */
    if (hdr->magic != expected_magic) {
        return false;
    }
    
    /* Check version */
    if (hdr->version != NCD_SHM_VERSION) {
        return false;
    }
    
    /* Check total size consistency */
    if (hdr->total_size > size) {
        return false;
    }
    
    /* Check header size */
    if (hdr->header_size < sizeof(ShmSnapshotHdr)) {
        return false;
    }
    
    if (hdr->header_size > hdr->total_size) {
        return false;
    }
    
    /* Check section count */
    if (hdr->section_count > NCD_SHM_MAX_SECTIONS) {
        return false;
    }
    
    /* Calculate expected header size with sections */
    size_t expected_header = sizeof(ShmSnapshotHdr) + 
                             (hdr->section_count * sizeof(ShmSectionDesc));
    if (hdr->header_size < expected_header) {
        return false;
    }
    
    /* Check complete flag */
    if (!(hdr->flags & NCD_SHM_FLAG_COMPLETE)) {
        return false;
    }
    
    return true;
}

uint64_t shm_compute_checksum(const void *base, size_t size) {
    if (!base || size <= sizeof(ShmSnapshotHdr)) {
        return 0;
    }
    
    const ShmSnapshotHdr *hdr = (const ShmSnapshotHdr *)base;
    
    /* Compute checksum of data after header */
    const uint8_t *data_start = (const uint8_t *)base + hdr->header_size;
    size_t data_size = hdr->total_size - hdr->header_size;
    
    return (uint64_t)platform_crc64(data_start, data_size);
}

bool shm_validate_checksum(const void *base, size_t size) {
    if (!base || size < sizeof(ShmSnapshotHdr)) {
        return false;
    }
    
    const ShmSnapshotHdr *hdr = (const ShmSnapshotHdr *)base;
    uint64_t computed = shm_compute_checksum(base, size);
    
    return computed == hdr->checksum;
}

/* --------------------------------------------------------- section access      */

const ShmSectionDesc *shm_find_section(const ShmSnapshotHdr *hdr, uint16_t type) {
    if (!hdr || hdr->section_count == 0) {
        return NULL;
    }
    
    /* Section table follows header */
    const ShmSectionDesc *sections = (const ShmSectionDesc *)((const uint8_t *)hdr + sizeof(ShmSnapshotHdr));
    
    for (uint32_t i = 0; i < hdr->section_count; i++) {
        if (sections[i].type == type) {
            return &sections[i];
        }
    }
    
    return NULL;
}

const void *shm_get_section_ptr(const void *base, const ShmSectionDesc *desc) {
    if (!base || !desc) {
        return NULL;
    }
    
    return (const uint8_t *)base + desc->offset;
}

/* --------------------------------------------------------- snapshot info       */

bool shm_get_info(const void *base, size_t size, ShmSnapshotInfo *info) {
    if (!info) {
        return false;
    }
    
    memset(info, 0, sizeof(*info));
    
    if (!base || size < sizeof(ShmSnapshotHdr)) {
        return false;
    }
    
    const ShmSnapshotHdr *hdr = (const ShmSnapshotHdr *)base;
    
    info->generation = hdr->generation;
    info->total_size = hdr->total_size;
    info->section_count = hdr->section_count;
    
    /* Validate magic (accept either metadata or database magic) */
    if (hdr->magic != NCD_SHM_META_MAGIC && hdr->magic != NCD_SHM_DB_MAGIC) {
        info->valid = false;
        return false;
    }
    
    info->valid = true;
    return true;
}
