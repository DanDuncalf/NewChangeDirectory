/*
 * shared_state.h  --  Shared memory snapshot format for NCD state service
 *
 * This header defines the canonical read-only snapshot format used for
 * shared memory communication between the NCD resident service and clients.
 *
 * Design principles:
 * - All data in shared memory uses offsets, not pointers
 * - Snapshots are versioned and checksummed
 * - Service is the only writer, clients are read-only
 * - Two separate snapshots: metadata and database
 * - Generation numbers for cache coherency
 */

#ifndef NCD_SHARED_STATE_H
#define NCD_SHARED_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --------------------------------------------------------- magic numbers    */

/* Metadata snapshot magic: 'NCMS' = NCD Metadata Snapshot */
#define NCD_SHM_META_MAGIC      0x534D434EU

/* Database snapshot magic: 'NCDB' = NCD Database Snapshot */
#define NCD_SHM_DB_MAGIC        0x4244434EU

/* Control channel magic for validation */
#define NCD_SHM_CTL_MAGIC       0x4C54434EU

/* Current snapshot format version */
#define NCD_SHM_VERSION         1

/* --------------------------------------------------------- fixed limits     */

/* Maximum sections in a snapshot */
#define NCD_SHM_MAX_SECTIONS    8

/* Maximum drives in database snapshot */
#define NCD_SHM_MAX_DRIVES      64

/* Maximum string pool size (256 MB) */
#define NCD_SHM_MAX_POOL_SIZE   (256ULL * 1024 * 1024)

/* Maximum snapshot size (1 GB) */
#define NCD_SHM_MAX_SNAPSHOT_SIZE (1ULL * 1024 * 1024 * 1024)

/* --------------------------------------------------------- section types    */

/* Metadata section types */
#define NCD_SHM_SECTION_CONFIG      0x01
#define NCD_SHM_SECTION_GROUPS      0x02
#define NCD_SHM_SECTION_HEURISTICS  0x03
#define NCD_SHM_SECTION_EXCLUSIONS  0x04
#define NCD_SHM_SECTION_DIR_HISTORY 0x05

/* --------------------------------------------------------- header structures */

/*
 * ShmSnapshotHdr  --  Common header for all snapshots
 *
 * Every snapshot starts with this 32-byte header.
 */
typedef struct {
    uint32_t magic;              /* NCD_SHM_META_MAGIC or NCD_SHM_DB_MAGIC */
    uint16_t version;            /* NCD_SHM_VERSION */
    uint16_t flags;              /* Feature flags (see below) */
    uint32_t total_size;         /* Total bytes in snapshot */
    uint32_t header_size;        /* Size of header (including section table) */
    uint32_t section_count;      /* Number of sections */
    uint64_t generation;         /* Monotonic generation number */
    uint64_t checksum;           /* CRC64 of all data after header */
} ShmSnapshotHdr;                /* 32 bytes */

/* Flags */
#define NCD_SHM_FLAG_READONLY   0x0001  /* Snapshot is read-only */
#define NCD_SHM_FLAG_COMPLETE   0x0002  /* Snapshot is complete/valid */

/*
 * ShmSectionDesc  --  Section descriptor (8 bytes)
 *
 * Describes one section within the snapshot.
 */
typedef struct {
    uint16_t type;               /* Section type (NCD_SHM_SECTION_*) */
    uint16_t reserved;           /* Padding for alignment */
    uint32_t offset;             /* Offset from snapshot base */
    uint32_t size;               /* Size in bytes */
} ShmSectionDesc;                /* 12 bytes (padded to 12) */

/* --------------------------------------------------------- metadata sections */

/*
 * ShmConfigSection  --  Configuration data
 *
 * Mirrors NcdConfig structure but packed for shared memory.
 */
typedef struct {
    uint32_t magic;              /* NCD_CFG_MAGIC or NCD_META_MAGIC */
    uint16_t version;            /* Config version */
    uint8_t  default_show_hidden;
    uint8_t  default_show_system;
    uint8_t  default_fuzzy_match;
    int8_t   default_timeout;    /* -1 = not set */
    uint8_t  has_defaults;
    uint8_t  service_retry_count;/* Max retries for service busy (0=default)*/
    uint8_t  pad;
} ShmConfigSection;              /* 16 bytes */

/*
 * ShmGroupEntry  --  Single group entry
 */
typedef struct {
    uint32_t name_off;           /* Offset to name in string pool */
    uint32_t path_off;           /* Offset to path in string pool */
    int64_t  created;            /* Unix timestamp */
} ShmGroupEntry;                 /* 16 bytes */

/*
 * ShmGroupsSection  --  Groups database section
 */
typedef struct {
    uint32_t entry_count;        /* Number of groups */
    uint32_t entries_off;        /* Offset to ShmGroupEntry array */
    uint32_t pool_off;           /* Offset to string pool */
    uint32_t pool_size;          /* String pool size */
} ShmGroupsSection;              /* 16 bytes */

/*
 * ShmHeurEntry  --  Single heuristics entry
 */
typedef struct {
    uint32_t search_off;         /* Offset to search term in pool */
    uint32_t target_off;         /* Offset to target path in pool */
    uint32_t frequency;          /* Usage frequency */
    int32_t  last_used;          /* Days since epoch (compact) */
} ShmHeurEntry;                  /* 16 bytes */

/*
 * ShmHeuristicsSection  --  Heuristics database section
 */
typedef struct {
    uint32_t entry_count;        /* Number of entries */
    uint32_t entries_off;        /* Offset to ShmHeurEntry array */
    uint32_t pool_off;           /* Offset to string pool */
    uint32_t pool_size;          /* String pool size */
} ShmHeuristicsSection;          /* 16 bytes */

/*
 * ShmExclusionEntry  --  Single exclusion entry
 */
typedef struct {
    uint32_t pattern_off;        /* Offset to pattern in pool */
    char     drive;              /* Drive letter or 0 for all */
    uint8_t  match_from_root;
    uint8_t  has_parent_match;
    uint8_t  pad;
} ShmExclusionEntry;             /* 8 bytes */

/*
 * ShmExclusionsSection  --  Exclusions list section
 */
typedef struct {
    uint32_t entry_count;        /* Number of exclusions */
    uint32_t entries_off;        /* Offset to ShmExclusionEntry array */
    uint32_t pool_off;           /* Offset to string pool */
    uint32_t pool_size;          /* String pool size */
} ShmExclusionsSection;          /* 16 bytes */

/*
 * ShmDirHistoryEntry  --  Single directory history entry
 */
typedef struct {
    uint32_t path_off;           /* Offset to path in pool */
    char     drive;              /* Drive letter */
    uint8_t  pad[3];
    int64_t  timestamp;          /* Unix timestamp */
} ShmDirHistoryEntry;            /* 16 bytes */

/*
 * ShmDirHistorySection  --  Directory history section
 */
typedef struct {
    uint32_t entry_count;        /* Number of entries (max 9) */
    uint32_t entries_off;        /* Offset to ShmDirHistoryEntry array */
    uint32_t pool_off;           /* Offset to string pool */
    uint32_t pool_size;          /* String pool size */
} ShmDirHistorySection;          /* 16 bytes */

/* --------------------------------------------------------- database sections */

/*
 * ShmDirEntry  --  Directory entry for shared memory
 *
 * Matches DirEntry layout but explicit for shared memory.
 */
typedef struct {
    int32_t  parent;             /* Parent index (-1 for root) */
    uint32_t name_off;           /* Offset into name pool */
    uint8_t  is_hidden;
    uint8_t  is_system;
    uint8_t  pad[2];
} ShmDirEntry;                   /* 12 bytes */

/*
 * ShmDriveSection  --  One drive's data
 */
typedef struct {
    char     letter;             /* 'A'-'Z' */
    char     label[64];          /* Volume label */
    uint32_t type;               /* Drive type */
    uint32_t dir_count;          /* Number of directories */
    uint32_t dirs_off;           /* Offset to ShmDirEntry array */
    uint32_t pool_off;           /* Offset to name pool */
    uint32_t pool_size;          /* Name pool size */
    uint32_t reserved;
} ShmDriveSection;               /* 88 bytes (padded) */

/*
 * ShmDatabaseSection  --  Database header section
 */
typedef struct {
    uint32_t drive_count;        /* Number of drives */
    uint32_t drives_off;         /* Offset to ShmDriveSection array */
    int64_t  last_scan;          /* Unix timestamp */
    uint8_t  show_hidden;        /* Default show hidden */
    uint8_t  show_system;        /* Default show system */
    uint8_t  pad[6];
} ShmDatabaseSection;            /* 24 bytes */

/* --------------------------------------------------------- access helpers   */

/*
 * Offset-based access macros
 *
 * These convert offsets to pointers given a base address.
 * All offsets are relative to the snapshot base.
 */

/* Get pointer to type at offset from base */
#define SHM_PTR(base, offset, type) \
    ((type *)((uint8_t *)(base) + (offset)))

/* Get pointer to array of type at offset */
#define SHM_ARRAY(base, offset, type) \
    ((type *)((uint8_t *)(base) + (offset)))

/* Get string pointer at offset */
#define SHM_STR(base, offset) \
    ((const char *)((uint8_t *)(base) + (offset)))

/* --------------------------------------------------------- validation       */

/*
 * shm_validate_header  --  Validate snapshot header
 *
 * Checks magic, version, and bounds. Returns true if valid.
 */
bool shm_validate_header(const void *base, size_t size, uint32_t expected_magic);

/*
 * shm_validate_checksum  --  Verify snapshot checksum
 *
 * Recomputes CRC64 of data after header and compares.
 * Returns true if checksum matches.
 */
bool shm_validate_checksum(const void *base, size_t size);

/*
 * shm_compute_checksum  --  Compute CRC64 of snapshot data
 *
 * Computes checksum of all bytes after the header.
 */
uint64_t shm_compute_checksum(const void *base, size_t size);

/* --------------------------------------------------------- section access   */

/*
 * shm_find_section  --  Find section by type
 *
 * Returns pointer to section descriptor, or NULL if not found.
 */
const ShmSectionDesc *shm_find_section(const ShmSnapshotHdr *hdr, uint16_t type);

/*
 * shm_get_section_ptr  --  Get pointer to section data
 *
 * Returns pointer to section data given a section descriptor.
 */
const void *shm_get_section_ptr(const void *base, const ShmSectionDesc *desc);

/* --------------------------------------------------------- snapshot info    */

/*
 * ShmSnapshotInfo  --  Information about a mapped snapshot
 */
typedef struct {
    uint64_t generation;         /* Snapshot generation */
    uint32_t total_size;         /* Total size in bytes */
    uint32_t section_count;      /* Number of sections */
    bool     valid;              /* Passed validation */
} ShmSnapshotInfo;

/*
 * shm_get_info  --  Get information about a snapshot
 *
 * Fills info structure from header. Returns false if invalid.
 */
bool shm_get_info(const void *base, size_t size, ShmSnapshotInfo *info);

/* --------------------------------------------------------- utility          */

/*
 * shm_crc64  --  Compute CRC64 checksum
 *
 * Standard CRC64-ECMA implementation.
 */
uint64_t shm_crc64(const void *data, size_t len);

/*
 * shm_crc64_update  --  Update CRC64 with more data
 */
uint64_t shm_crc64_update(uint64_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NCD_SHARED_STATE_H */
