/*
 * shared_state.h  --  Shared memory snapshot format for NCD state service
 *
 * This header is now a compatibility wrapper around shm_types.h.
 * All shared memory type definitions are in shm_types.h.
 */

#ifndef NCD_SHARED_STATE_H
#define NCD_SHARED_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Include all shared memory type definitions */
#include "shm_types.h"

/* --------------------------------------------------------- compatibility      */

/*
 * Legacy compatibility: ShmSnapshotHdr matches ShmMetadataHeader layout
 * for version 1 snapshots.
 */
typedef ShmMetadataHeader ShmSnapshotHdr;

/* Legacy section IDs (same values as in shm_types.h) */
#ifndef NCD_SHM_SECTION_CONFIG
#define NCD_SHM_SECTION_CONFIG      0x01
#define NCD_SHM_SECTION_GROUPS      0x02
#define NCD_SHM_SECTION_HEURISTICS  0x03
#define NCD_SHM_SECTION_EXCLUSIONS  0x04
#define NCD_SHM_SECTION_DIR_HISTORY 0x05
#endif

/* Legacy magic numbers */
#ifndef NCD_SHM_META_MAGIC
#define NCD_SHM_META_MAGIC      0x534D434EU
#define NCD_SHM_DB_MAGIC        0x4244434EU
#define NCD_SHM_CTL_MAGIC       0x4C54434EU
#define NCD_SHM_VERSION         1
#endif

/* Legacy flags */
#define NCD_SHM_FLAG_READONLY   0x0001
#define NCD_SHM_FLAG_COMPLETE   0x0002

/* Legacy limits */
#ifndef NCD_SHM_MAX_SECTIONS
#define NCD_SHM_MAX_SECTIONS    8
#define NCD_SHM_MAX_DRIVES      64
#define NCD_SHM_MAX_POOL_SIZE   (256ULL * 1024 * 1024)
#define NCD_SHM_MAX_SNAPSHOT_SIZE (1ULL * 1024 * 1024 * 1024)
#endif

/* Legacy ShmDriveSection for backward compatibility */
typedef struct {
    char     letter;
    char     label[64];
    uint32_t type;
    uint32_t dir_count;
    uint32_t dirs_off;
    uint32_t pool_off;
    uint32_t pool_size;
    uint32_t reserved;
} ShmDriveSection;

/* Legacy ShmDatabaseSection for backward compatibility */
typedef struct {
    uint32_t drive_count;
    uint32_t drives_off;
    int64_t  last_scan;
    uint8_t  show_hidden;
    uint8_t  show_system;
    uint8_t  pad[6];
} ShmDatabaseSection;

/* --------------------------------------------------------- snapshot info      */

/*
 * ShmSnapshotInfo  --  Information about a mapped snapshot
 */
typedef struct {
    uint64_t generation;
    uint32_t total_size;
    uint32_t section_count;
    bool     valid;
} ShmSnapshotInfo;

/* --------------------------------------------------------- validation         */

bool shm_validate_header(const void *base, size_t size, uint32_t expected_magic);
bool shm_validate_checksum(const void *base, size_t size);
uint64_t shm_compute_checksum(const void *base, size_t size);

/* --------------------------------------------------------- section access     */

const ShmSectionDesc *shm_find_section(const ShmMetadataHeader *hdr, uint16_t type);
const void *shm_get_section_ptr(const void *base, const ShmSectionDesc *desc);

/* --------------------------------------------------------- snapshot info      */

bool shm_get_info(const void *base, size_t size, ShmSnapshotInfo *info);

/* --------------------------------------------------------- utility            */

uint64_t shm_crc64(const void *data, size_t len);
uint64_t shm_crc64_update(uint64_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NCD_SHARED_STATE_H */
