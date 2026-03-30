/*
 * shm_types.h  --  Shared memory types for NCD state service
 *
 * This header defines the offset-based shared memory structures that enable
 * zero-copy client access. All structures use offsets instead of pointers,
 * and platform-native character encoding (UTF-16 on Windows, UTF-8 on Linux).
 *
 * Design principles:
 * - Offsets relative to structure start enable direct memory mapping
 * - Platform-specific text encoding preserves international characters
 * - No hardcoded offsets - all calculated using sizeof()/offsetof()
 * - Two shared memory segments: metadata + database
 */

#ifndef NCD_SHM_TYPES_H
#define NCD_SHM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "ncd.h"

/* --------------------------------------------------------- magic & version  */

#define NCD_SHM_TYPES_VERSION   2       /* Version for new offset-based format */

/* Legacy compatibility version number */
#define NCD_SHM_VERSION         NCD_SHM_TYPES_VERSION

/* Metadata snapshot magic: 'NCMS' = NCD Metadata Snapshot */
#ifndef NCD_SHM_META_MAGIC
#define NCD_SHM_META_MAGIC      0x534D434EU
#endif

/* Database snapshot magic: 'NCDB' = NCD Database Snapshot */
#ifndef NCD_SHM_DB_MAGIC
#define NCD_SHM_DB_MAGIC        0x4244434EU
#endif

/* --------------------------------------------------------- encoding flags   */

#define NCD_SHM_ENCODING_UTF8       1
#define NCD_SHM_ENCODING_UTF16LE    2

/* --------------------------------------------------------- access macros    */

/* Get pointer to type at offset from base */
#define SHM_PTR(base, offset, type) \
    ((type *)((uint8_t *)(base) + (offset)))

/* Get pointer to array of type at offset */
#define SHM_ARRAY(base, offset, type) \
    ((type *)((uint8_t *)(base) + (offset)))

/* Get string pointer at offset (returns NcdShmChar*) */
#define SHM_STR(base, offset) \
    ((const NcdShmChar *)((uint8_t *)(base) + (offset)))

/* --------------------------------------------------------- metadata header  */

/*
 * ShmMetadataHeader  --  Header for metadata shared memory segment
 */
typedef struct {
    uint32_t magic;              /* NCD_SHM_META_MAGIC */
    uint32_t version;            /* NCD_SHM_TYPES_VERSION */
    uint64_t generation;         /* Monotonic generation counter */
    uint64_t checksum;           /* CRC64 of data after header */
    uint32_t total_size;         /* Total bytes in snapshot */
    uint32_t header_size;        /* sizeof(ShmMetadataHeader) + section table */
    uint32_t section_count;      /* Number of sections */
    uint32_t text_encoding;      /* NCD_SHM_ENCODING_* */
    uint32_t flags;              /* Feature flags (NCD_SHM_FLAG_*) */
    uint32_t reserved;           /* Padding for 8-byte alignment */
} ShmMetadataHeader;             /* 48 bytes */

/* Section types for metadata */
#define NCD_SHM_SECTION_CONFIG      0x01
#define NCD_SHM_SECTION_GROUPS      0x02
#define NCD_SHM_SECTION_HEURISTICS  0x03
#define NCD_SHM_SECTION_EXCLUSIONS  0x04
#define NCD_SHM_SECTION_DIR_HISTORY 0x05

/*
 * ShmSectionDesc  --  Section descriptor (12 bytes)
 */
typedef struct {
    uint32_t type;               /* Section type */
    uint32_t offset;             /* Offset from metadata base */
    uint32_t size;               /* Size in bytes */
} ShmSectionDesc;

/* --------------------------------------------------------- config section   */

/*
 * ShmConfigSection  --  Configuration data in shared memory
 * Mirrors NcdConfig but packed for SHM
 */
typedef struct {
    uint32_t magic;              /* NCD_CFG_MAGIC or NCD_META_MAGIC */
    uint16_t version;            /* Config version */
    uint8_t  default_show_hidden;
    uint8_t  default_show_system;
    uint8_t  default_fuzzy_match;
    int8_t   default_timeout;    /* -1 = not set */
    uint8_t  has_defaults;
    uint8_t  service_retry_count;
    int16_t  rescan_interval_hours; /* 1-168 or -1=never */
    uint8_t  text_encoding;      /* NCD_TEXT_UTF8 or NCD_TEXT_UTF16LE */
    uint8_t  pad[5];             /* Padding to 16 bytes */
} ShmConfigSection;              /* 16 bytes */

/* --------------------------------------------------------- groups section   */

/*
 * ShmGroupEntry  --  Single group entry in shared memory
 */
typedef struct {
    uint32_t name_off;           /* Offset to group name in string pool */
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
    uint32_t pool_size;          /* String pool size in bytes */
} ShmGroupsSection;              /* 16 bytes */

/* Access macros for groups */
#define SHM_GROUPS_ENTRIES(base, section) \
    SHM_ARRAY(base, (section)->entries_off, ShmGroupEntry)
#define SHM_GROUPS_POOL(base, section) \
    ((const char *)((base) + (section)->pool_off))

/* --------------------------------------------------------- heuristics section */

/*
 * ShmHeurEntry  --  Single heuristics entry in shared memory
 */
typedef struct {
    uint32_t search_off;         /* Offset to search term in pool */
    uint32_t target_off;         /* Offset to target path in pool */
    uint32_t frequency;          /* Usage frequency */
    int32_t  last_used;          /* Days since epoch */
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

/* Access macros for heuristics */
#define SHM_HEUR_ENTRIES(base, section) \
    SHM_ARRAY(base, (section)->entries_off, ShmHeurEntry)
#define SHM_HEUR_POOL(base, section) \
    ((const char *)((base) + (section)->pool_off))

/* --------------------------------------------------------- exclusions section */

/*
 * ShmExclusionEntry  --  Single exclusion entry in shared memory
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

/* Access macros for exclusions */
#define SHM_EXCL_ENTRIES(base, section) \
    SHM_ARRAY(base, (section)->entries_off, ShmExclusionEntry)
#define SHM_EXCL_POOL(base, section) \
    ((const char *)((base) + (section)->pool_off))

/* --------------------------------------------------------- dir history section */

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

/* Access macros for dir history */
#define SHM_HISTORY_ENTRIES(base, section) \
    SHM_ARRAY(base, (section)->entries_off, ShmDirHistoryEntry)
#define SHM_HISTORY_POOL(base, section) \
    ((const char *)((base) + (section)->pool_off))

/* --------------------------------------------------------- database header  */

/*
 * ShmDatabaseHeader  --  Header for database shared memory segment
 */
typedef struct {
    uint32_t magic;              /* NCD_SHM_DB_MAGIC */
    uint32_t version;            /* NCD_SHM_TYPES_VERSION */
    uint64_t generation;         /* Monotonic generation counter */
    uint64_t checksum;           /* CRC64 of data after header */
    uint32_t total_size;         /* Total bytes in snapshot */
    uint32_t header_size;        /* sizeof(ShmDatabaseHeader) + mount table */
    uint32_t mount_count;        /* Number of mounts (was drive_count) */
    uint32_t text_encoding;      /* NCD_SHM_ENCODING_* */
    int64_t  last_scan;          /* Unix timestamp of last scan */
    uint8_t  show_hidden;        /* Default show hidden */
    uint8_t  show_system;        /* Default show system */
    uint8_t  pad[6];             /* Padding */
} ShmDatabaseHeader;             /* 56 bytes */

/* --------------------------------------------------------- directory entry  */

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

/* --------------------------------------------------------- mount entry      */

/*
 * ShmMountEntry  --  One mount's data in shared memory
 * 
 * This structure is followed by the mount_point string in platform-native
 * encoding. The mount_point_len field tells you how many bytes to read.
 */
typedef struct {
    uint32_t dir_count;          /* Number of directories */
    uint32_t dirs_offset;        /* Offset to ShmDirEntry array */
    uint32_t pool_offset;        /* Offset to name pool */
    uint32_t pool_size;          /* Name pool size */
    uint32_t mount_point_off;    /* Offset to mount point string */
    uint32_t mount_point_len;    /* Length of mount point in bytes */
    uint32_t volume_label_off;   /* Offset to volume label */
    uint32_t volume_label_len;   /* Length of volume label in bytes */
    uint32_t type;               /* Drive type */
    uint32_t reserved;           /* Reserved */
} ShmMountEntry;                 /* 40 bytes */

/* Access macros for mount entries */
#define SHM_MOUNT_POINT(base, entry) \
    ((const NcdShmChar *)((uint8_t *)(base) + (entry)->mount_point_off))
#define SHM_VOLUME_LABEL(base, entry) \
    ((const NcdShmChar *)((uint8_t *)(base) + (entry)->volume_label_off))
#define SHM_MOUNT_DIRS(base, entry) \
    SHM_ARRAY(base, (entry)->dirs_offset, ShmDirEntry)
#define SHM_MOUNT_POOL(base, entry) \
    ((const char *)((uint8_t *)(base) + (entry)->pool_offset))

/* --------------------------------------------------------- layout calculation */

/*
 * ShmMetadataLayout  --  Calculated layout for metadata snapshot
 */
typedef struct {
    size_t header_size;          /* sizeof(ShmMetadataHeader) */
    size_t section_table_size;   /* section_count * sizeof(ShmSectionDesc) */
    size_t config_size;          /* sizeof(ShmConfigSection) */
    size_t groups_size;          /* sizeof(ShmGroupsSection) + entries + pool */
    size_t heuristics_size;      /* sizeof(ShmHeuristicsSection) + entries + pool */
    size_t exclusions_size;      /* sizeof(ShmExclusionsSection) + entries + pool */
    size_t dir_history_size;     /* sizeof(ShmDirHistorySection) + entries + pool */
    size_t total_size;
    
    /* Offsets to fill in */
    uint32_t sections_off;
    uint32_t config_off;
    uint32_t groups_off;
    uint32_t heuristics_off;
    uint32_t exclusions_off;
    uint32_t dir_history_off;
} ShmMetadataLayout;

/*
 * ShmDatabaseLayout  --  Calculated layout for database snapshot
 */
typedef struct {
    size_t header_size;          /* sizeof(ShmDatabaseHeader) */
    size_t mount_table_size;     /* mount_count * sizeof(ShmMountEntry) */
    size_t total_size;
    
    /* Per-mount offsets (variable length array) */
    uint32_t *dirs_offsets;      /* Offset to DirEntry array per mount */
    uint32_t *pool_offsets;      /* Offset to name pool per mount */
    uint32_t *mount_point_offsets; /* Offset to mount point string per mount */
    uint32_t *volume_label_offsets; /* Offset to volume label per mount */
} ShmDatabaseLayout;

/* --------------------------------------------------------- layout functions */

/*
 * shm_metadata_compute_layout  --  Calculate layout for metadata snapshot
 *
 * Computes all offsets and sizes needed to create a metadata snapshot.
 * Returns true on success, false on error.
 */
bool shm_metadata_compute_layout(const NcdMetadata *meta, ShmMetadataLayout *layout);

/*
 * shm_database_compute_layout  --  Calculate layout for database snapshot
 *
 * Computes all offsets and sizes needed to create a database snapshot.
 * The layout struct must be zeroed before first call.
 * Returns true on success, false on error.
 */
bool shm_database_compute_layout(const NcdDatabase *db, ShmDatabaseLayout *layout);

/*
 * shm_database_layout_free  --  Free dynamically allocated layout
 *
 * Frees the offset arrays allocated by shm_database_compute_layout.
 */
void shm_database_layout_free(ShmDatabaseLayout *layout);

/* --------------------------------------------------------- alignment        */

/* Align size to 8-byte boundary */
#define SHM_ALIGN8(size) (((size) + 7) & ~7)

/*
 * shm_align_size  --  Align size to specified boundary
 */
static inline size_t shm_align_size(size_t size, size_t align) {
    return (size + align - 1) & ~(align - 1);
}

#ifdef __cplusplus
}
#endif

#endif /* NCD_SHM_TYPES_H */
