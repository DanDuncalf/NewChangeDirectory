/*
 * ncd.h  --  Common types, constants, and structures for NewChangeDirectory
 *
 * Communication model
 * -------------------
 * NewChangeDirectory.exe cannot change the parent shell's working directory
 * directly (OS limitation).  Results are written to a temporary batch file
 * %TEMP%\ncd_result.bat which the ncd.bat wrapper then executes (calls).
 *
 * The result file sets three environment variables:
 *   NCD_STATUS  -- "OK" or "ERROR"
 *   NCD_DRIVE   -- e.g. "C:"  (empty on error)
 *   NCD_PATH    -- e.g. "C:\Users\Scott\Downloads"  (empty on error)
 *   NCD_MESSAGE -- human-readable message (always present; detail on error)
 */

#ifndef NCD_H
#define NCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/*
 * Platform / architecture detection - from shared library
 */
#include "../shared/platform_detect.h"

#if NCD_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <limits.h>
#include <strings.h>
typedef unsigned int  UINT;
typedef unsigned long DWORD;
typedef long          LONG;
#ifndef MAX_PATH
#define MAX_PATH 4096
#endif
/* Case-insensitive string compare (MSVC vs POSIX name) */
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#endif

/* --------------------------------------------------------- binary format   */

/*
 * Magic value stored in the first 4 bytes of a binary database file.
 * Spells 'NCDB' in little-endian memory order.
 */
#define NCD_BIN_MAGIC    0x4244434EU   /* 'N'=0x4E,'C'=0x43,'D'=0x44,'B'=0x42 */
#define NCD_BIN_VERSION  2   /* Incremented: added CRC64 checksum field */

/* --------------------------------------------------------- metadata format  */

/*
 * Consolidated metadata file (ncd.metadata) replaces:
 *   - ncd.config (configuration)
 *   - ncd.groups (bookmarks)
 *   - ncd.history (heuristics)
 * 
 * Magic: 'NCMD' = NCD Metadata
 * Uses CRC64 for consistency with database format
 */
#define NCD_META_MAGIC      0x444D434EU  /* 'N' 'C' 'M' 'D' in LE */
#define NCD_META_VERSION    1
#define NCD_META_FILENAME   "ncd.metadata"

/*
 * MetaFileHdr  --  fixed-size header at offset 0 of a metadata file
 *
 * sizeof(MetaFileHdr) == 20; every field is naturally aligned.
 */
typedef struct {
    uint32_t magic;            /* NCD_META_MAGIC                              */
    uint16_t version;          /* NCD_META_VERSION                            */
    uint8_t  section_count;    /* number of sections that follow              */
    uint8_t  reserved;         /* always 0                                    */
    uint32_t reserved2;        /* always 0                                    */
    uint64_t checksum;         /* CRC64 of all data after this header         */
} MetaFileHdr;                 /* 20 bytes */

/* Section IDs in metadata file */
#define NCD_META_SECTION_CONFIG      0x01
#define NCD_META_SECTION_GROUPS      0x02
#define NCD_META_SECTION_HEURISTICS  0x03
#define NCD_META_SECTION_EXCLUSIONS  0x04
#define NCD_META_SECTION_DIR_HISTORY 0x05  /* Circular list of last 9 directories */

/* Enhanced heuristics constants */
#define NCD_HEUR_MAX_ENTRIES    100     /* Max history entries (was 20) */
#define NCD_HEUR_MAGIC          0x5255484EU  /* 'N' 'H' 'U' 'R' = NCD HeURistics */
#define NCD_HEUR_VERSION        1

/*
 * BinFileHdr  --  fixed-size header at offset 0 of a binary database file
 *
 * All integer fields are stored in host byte order (little-endian on x86).
 * sizeof(BinFileHdr) == 32; every field is naturally aligned.
 */
typedef struct {
    uint32_t magic;               /* NCD_BIN_MAGIC                              */
    uint16_t version;             /* NCD_BIN_VERSION                            */
    uint8_t  show_hidden;         /* 1 or 0                                     */
    uint8_t  show_system;         /* 1 or 0                                     */
    int64_t  last_scan;           /* Unix timestamp (8-byte aligned @ offset 8) */
    uint32_t drive_count;
    uint8_t  skipped_rescan; /* 1 if user declined rescan after format change */
    uint8_t  pad[3];              /* reserved, always 0                         */
    uint64_t checksum;            /* CRC64 of all data after this header        */
} BinFileHdr;                     /* 32 bytes */

/*
 * BinDriveHdr  --  per-drive header; drive_count of these follow BinFileHdr
 *
 * sizeof(BinDriveHdr) == 80; every field is naturally aligned.
 */
typedef struct {
    uint8_t  letter;       /* 'A'..'Z'                                   */
    uint8_t  pad[3];       /* reserved, always 0                         */
    uint32_t type;         /* DRIVE_FIXED / DRIVE_REMOVABLE / etc.       */
    char     label[64];    /* volume label (NUL-padded)                  */
    uint32_t dir_count;    /* number of DirEntry records                 */
    uint32_t pool_size;    /* bytes in the name pool                     */
} BinDriveHdr;             /* 80 bytes */

/* ---------------------------------------------------------------- limits  */

/*
 * NCD_MAX_NAME is the largest directory-component name we will store/load.
 * It is used only as a local stack-buffer size; names are held in a
 * per-drive string pool so DirEntry itself no longer carries a fixed array.
 */
#define NCD_MAX_NAME     256
#define NCD_MAX_GROUP_NAME 64
#define NCD_MAX_PATH     MAX_PATH

#define NCD_DB_FILENAME  "ncd.database"      /* in %LOCALAPPDATA%            */
#define NCD_DB_NEWFILE   "ncd.database.new"  /* written during rescan        */
#define NCD_DB_OLDFILE   "ncd.database.old"  /* previous version backup      */
#if NCD_PLATFORM_WINDOWS
#define NCD_RESULT_FILE  "ncd_result.bat"    /* written to %TEMP%            */
#define NCD_PATH_SEP     "\\"
#else
#define NCD_RESULT_FILE  "ncd_result.sh"     /* written to /tmp              */
#define NCD_PATH_SEP     "/"
#endif

#define NCD_VERSION       1

/* Rescan interval configuration */
#define NCD_RESCAN_HOURS_DEFAULT  24   /* default: trigger background rescan after 24h */
#define NCD_RESCAN_HOURS_MIN      1    /* minimum: 1 hour */
#define NCD_RESCAN_HOURS_MAX      168  /* maximum: 1 week (168 hours) */
#define NCD_RESCAN_NEVER          (-1) /* disable auto-rescan */

/* ================================================================ constants  */

/* Scanner constants */
#define NCD_SCAN_PROGRESS_INTERVAL  100     /* dirs between progress updates */
#define NCD_SCAN_TIMEOUT_MS         300000  /* 5 minute default timeout */
#define NCD_SCAN_INACTIVITY_MS      60000   /* 1 minute inactivity timeout */
#define NCD_SCAN_MAX_DEPTH          65536   /* maximum directory nesting */

/* UI constants */
#define NCD_UI_MAX_LIST_ITEMS       20      /* max visible items in selector */
#define NCD_UI_BOX_MAX_WIDTH        120     /* maximum UI box width */
#define NCD_UI_BOX_MIN_WIDTH        40      /* minimum UI box width */

/* Matcher constants */
#define NCD_MATCHER_MAX_PARTS       32      /* max search path components */
#define NCD_MATCHER_INITIAL_CAP     16      /* initial match array capacity */

/* Database constants */
#define NCD_NAME_POOL_INITIAL       4096    /* initial name pool size */
#define NCD_DIRS_INITIAL_CAP        64      /* initial directory array capacity */
#define NCD_DRIVES_INITIAL_CAP      8       /* initial drive array capacity */

/* Security limits */
#define NCD_MAX_DRIVES              64      /* Maximum drives in database */
#define NCD_MAX_DIRS_PER_DRIVE      10000000 /* 10 million dirs per drive */
#define NCD_MAX_POOL_SIZE           (1ULL << 30) /* 1GB name pool per drive */
#define NCD_MAX_TOTAL_DIRS          50000000 /* 50 million total dirs */
#define NCD_MAX_FILE_SIZE           (2ULL << 30) /* 2GB max database file */

/* ----------------------------------------------------------------- types  */

/*
 * DirEntry  --  one directory node in the flattened tree
 *
 * Names are NOT stored inline.  Instead each entry holds a byte offset
 * (name_off) into DriveData.name_pool, which is a single heap buffer
 * holding all names for that drive packed end-to-end with NUL terminators.
 *
 * This replaces the old `char name[256]` field, shrinking the struct from
 * 268 bytes to 12 bytes and cutting per-drive memory use by ~22×.
 *
 * The `id` field has been removed; it was always equal to the array index.
 *
 * Full path is reconstructed by walking the parent chain.
 * Root-level directories (direct children of X:\) have parent == -1.
 */
typedef struct {
    int32_t  parent;           /* -1  => root of drive                       */
    uint32_t name_off;         /* byte offset into DriveData.name_pool       */
    uint8_t  is_hidden;
    uint8_t  is_system;
    uint8_t  pad[2];           /* explicit padding for 4-byte alignment      */
} DirEntry;                    /* sizeof == 12 bytes (was 268)               */

/*
 * DriveData  --  all scanned directories for one drive
 */
typedef struct {
    char      letter;          /* 'A'..'Z'                                   */
    char      label[64];       /* volume label                               */
    UINT      type;            /* DRIVE_FIXED / DRIVE_REMOVABLE / etc.       */
    DirEntry *dirs;
    int       dir_count;
    int       dir_capacity;    /* allocated slots in dirs[]                  */
    /* String pool: all directory names packed as NUL-terminated strings.   */
    char     *name_pool;
    size_t    name_pool_len;   /* bytes used                                 */
    size_t    name_pool_cap;   /* bytes allocated                            */
} DriveData;

/*
 * NcdDatabase  --  the full in-memory representation of ncd.database
 */
/* Forward declaration for name index caching */
struct NameIndex;

typedef struct {
    int        version;
    bool       default_show_hidden;
    bool       default_show_system;
    time_t     last_scan;      /* Unix timestamp of last successful scan      */
    DriveData *drives;
    int        drive_count;
    int        drive_capacity;
    char       db_path[NCD_MAX_PATH];  /* resolved absolute path of the file */

    /*
     * Binary-blob load support.
     *
     * When the database is loaded from a binary file (db_load_binary), the
     * entire file is read into a single heap buffer (blob_buf).  Each
     * DriveData.dirs and DriveData.name_pool then point directly into that
     * buffer -- they must NOT be individually freed.
     *
     * db_free() checks is_blob: if true it frees blob_buf and db->drives[]
     * as a unit; if false it follows the normal per-drive free path.
     */
    void      *blob_buf;              /* NULL unless is_blob                */
    bool       is_blob;               /* true  => dirs/pools point in blob  */
    
    /*
     * Cached name index for fast searches.
     * Built on first call to matcher_find(), invalidated when database changes.
     * Uses void* to avoid including matcher.h here (circular dependency).
     * Cast to NameIndex* when used in matcher.c.
     */
    void *name_index;                 /* NULL until built (actually NameIndex*) */
    int   name_index_generation;      /* Incremented on modification */
} NcdDatabase;

/*
 * NcdConfig  --  user configuration defaults (stored in ncd.metadata)
 * 
 * NOTE: Legacy ncd.config file format is deprecated. Use ncd.metadata instead.
 */
typedef struct {
    uint32_t magic;               /* NCD_CFG_MAGIC or NCD_META_MAGIC         */
    uint16_t version;             /* NCD_CFG_VERSION or NCD_META_VERSION     */
    bool     default_show_hidden; /* Default for /i                          */
    bool     default_show_system; /* Default for /s                          */
    bool     default_fuzzy_match; /* Default for /z                          */
    int      default_timeout;     /* Default for /t (seconds, -1 = not set)  */
    bool     has_defaults;        /* true if any defaults have been set      */
    uint8_t  service_retry_count; /* Max retries for service busy (0=default)*/
    int16_t  rescan_interval_hours; /* Auto-rescan interval (1-168, or -1=never) */
    uint8_t  pad[4];              /* Padding for alignment                   */
} NcdConfig;

#define NCD_CFG_MAGIC    0x43434647U  /* 'G' 'F' 'C' 'C' (NCD Config) in LE    */
#define NCD_CFG_VERSION  3            /* Incremented: added rescan_interval_hours*/

/* Default value for service retry count (10 * 100ms = 1 second max wait) */
#define NCD_DEFAULT_SERVICE_RETRY_COUNT  10

/*
 * NcdHeurEntryV2  --  enhanced heuristics entry with frequency and recency
 * 
 * Scoring algorithm: score = (frequency * 10) / (1 + (days_since_last_use / 7))
 * This gives higher scores to frequently-used and recently-used entries.
 */
typedef struct {
    char     search[NCD_MAX_PATH];    /* Normalized search term              */
    char     target[NCD_MAX_PATH];    /* Selected target path                */
    uint32_t frequency;               /* Times this search->target chosen    */
    time_t   last_used;               /* Timestamp of last selection         */
    uint8_t  pad[4];                  /* Padding for 8-byte alignment        */
} NcdHeurEntryV2;                     /* 528 bytes per entry                 */

/*
 * NcdHeuristicsV2  --  enhanced heuristics database
 */

/* Hash bucket for heuristics hash map (separate chaining) */
typedef struct HeurHashBucket {
    int entry_idx;                    /* Index into entries array            */
    struct HeurHashBucket *next;      /* Next bucket in chain                */
} HeurHashBucket;

typedef struct {
    NcdHeurEntryV2 *entries;          /* Array of entries                    */
    int             count;            /* Number of valid entries             */
    int             capacity;         /* Allocated capacity                  */
    
    /* Hash map for O(1) lookup by search term */
    HeurHashBucket **hash_buckets;    /* Array of bucket pointers            */
    int              hash_bucket_count; /* Size of hash table (power of 2)   */
} NcdHeuristicsV2;

/*
 * NcdGroupEntry - single group mapping
 */
typedef struct {
    char name[NCD_MAX_GROUP_NAME];  /* Group name (e.g., "@home") */
    char path[NCD_MAX_PATH];        /* Full directory path */
    time_t created;                 /* When group was created */
} NcdGroupEntry;

/*
 * NcdGroupDb - group database
 */
typedef struct {
    NcdGroupEntry *groups;
    int count;
    int capacity;
} NcdGroupDb;

/*
 * NcdExclusionEntry - single exclusion pattern
 * 
 * Pattern syntax:
 *   - Windows: [drive:]path with * wildcards (e.g., C:Windows, *:$recycle.bin)
 *   - Linux: path with * wildcards (e.g., proc, node_modules)
 *   - Drive letter * means all drives
 *   - Leading backslash or slash means match from root only
 *   - ParentChild pattern matches only when parent contains child
 *   - * alone is invalid, but .*foo*bar* is valid
 */
typedef struct {
    char pattern[NCD_MAX_PATH];   /* Original pattern string */
    char drive;                   /* Drive letter (0 = all drives, 'A'-'Z' = specific) */
    bool match_from_root;         /* Pattern starts with path separator */
    bool has_parent_match;        /* Pattern contains parent\child syntax */
    uint8_t pad[2];
} NcdExclusionEntry;

/*
 * NcdExclusionList - exclusion database
 */
typedef struct {
    NcdExclusionEntry *entries;
    int count;
    int capacity;
} NcdExclusionList;

/* Default exclusion patterns (auto-added on first run) */
#define NCD_DEFAULT_EXCLUSIONS_WINDOWS "$recycle.bin"
#define NCD_DEFAULT_EXCLUSIONS_WIN_C   "\Windows"

/*
 * NcdDirHistoryEntry - single directory history entry
 */
typedef struct {
    char path[NCD_MAX_PATH];      /* Full directory path */
    char drive;                   /* Drive letter */
    time_t timestamp;             /* When this entry was added */
} NcdDirHistoryEntry;

#define NCD_DIR_HISTORY_MAX 9     /* Maximum entries in directory history */

/*
 * NcdDirHistory - circular list of recently visited directories
 * 
 * entries[0] is the most recent, entries[count-1] is the oldest
 * When adding to a full list, entries[NCD_DIR_HISTORY_MAX-1] is discarded
 */
typedef struct {
    NcdDirHistoryEntry entries[NCD_DIR_HISTORY_MAX];
    int count;                    /* Number of valid entries (0-9) */
} NcdDirHistory;

/*
 * NcdMetadata  --  consolidated metadata container
 * 
 * Combines config, groups, heuristics, exclusions, and dir history in one file with atomic updates.
 */
typedef struct {
    /* Configuration section */
    NcdConfig cfg;
    
    /* Groups section */
    NcdGroupDb groups;
    
    /* Heuristics section */
    NcdHeuristicsV2 heuristics;
    
    /* Exclusion list section */
    NcdExclusionList exclusions;
    
    /* Directory history section */
    NcdDirHistory dir_history;
    
    /* File path (for save operations) */
    char file_path[NCD_MAX_PATH];
    
    /* Dirty flags for selective saving */
    bool config_dirty;
    bool groups_dirty;
    bool heuristics_dirty;
    bool exclusions_dirty;
    bool dir_history_dirty;
} NcdMetadata;

/*
 * NcdOptions  --  parsed command-line options
 */
typedef struct {
    bool show_hidden;             /* /i or /a                                */
    bool show_system;             /* /s or /a                                */
    bool force_rescan;            /* /r                                      */
    bool scan_drive_mask[26];     /* /rBDE => B,D,E only                     */
    int  scan_drive_count;        /* number of true entries in mask          */
    bool skip_drive_mask[26];     /* /r-b-d => exclude B,D                   */
    int  skip_drive_count;        /* number of true entries in skip mask     */
    bool show_version;            /* /v  -- print version banner and exit    */
    bool show_help;               /* /h or /? -- print help                 */
    bool show_history;            /* /f  -- show frequent history            */
    bool clear_history;           /* /fc -- clear frequent history           */
    bool fuzzy_match;             /* /z  -- fuzzy matching with DL distance  */
    bool group_set;               /* /g <group> -- create group for cwd      */
    bool group_remove;            /* /g- <group> -- remove group             */
    bool group_list;              /* /gl        -- list all groups           */
    bool config_edit;             /* /c        -- edit configuration         */
    char group_name[NCD_MAX_GROUP_NAME]; /* Group name for /g or /g-        */
    char db_override[NCD_MAX_PATH]; /* /d <path>                             */
    char search[NCD_MAX_PATH];    /* the directory pattern argument          */
    bool has_search;
    int  timeout_seconds;         /* /t <seconds> -- scan inactivity timeout */
    bool scan_root_only;          /* Linux: /r / scans only root, not /mnt */
    char scan_subdirectory[NCD_MAX_PATH]; /* /r. -- rescan specific subdirectory */
#if DEBUG
    bool test_no_checksum;        /* /test NC -- ignore checksum validation  */
    bool test_slow_mode;          /* /test SL -- pause 1s every 100 dirs     */
#endif
    /* Agent mode options (/agent command) */
    bool agent_mode;              /* /agent -- agent API mode                */
    int  agent_subcommand;        /* 0=none, 1=query, 2=ls, 3=tree, 4=check, 5=complete, 6=mkdir, 8=mkdirs */
    bool agent_json;              /* --json output                           */
    int  agent_limit;             /* --limit N (default 20, 0=unlimited)     */
    bool agent_depth_sort;        /* --depth (sort shallowest first)         */
    int  agent_depth;             /* --depth N (for ls/tree)                 */
    bool agent_dirs_only;         /* --dirs-only (for ls)                    */
    bool agent_files_only;        /* --files-only (for ls)                   */
    char agent_pattern[64];       /* --pattern <glob> (for ls)               */
    bool agent_flat;              /* --flat (for tree)                       */
    bool agent_check_db_age;      /* --db-age (for check)                    */
    bool agent_check_stats;       /* --stats (for check)                     */
    bool agent_check_service_status; /* --service-status (for check)         */
    char agent_mkdirs_file[NCD_MAX_PATH]; /* --file <path> (for mkdirs)      */
    
    /* Exclusion list options */
    bool exclusion_add;           /* -x <pattern> -- add exclusion pattern   */
    bool exclusion_remove;        /* -x- <pattern> -- remove exclusion       */
    bool exclusion_list;          /* -xl -- list all exclusions              */
    char exclusion_pattern[NCD_MAX_PATH]; /* Pattern for -x or -x-         */
    
    /* Directory history options */
    bool history_pingpong;        /* /0 or bare ncd -- ping-pong between last two */
    int  history_index;           /* /1..9 -- jump to history entry (1=oldest, 9=newest) */
    bool history_browse;          /* /h -- interactive history browser       */
    bool history_list;            /* /hl -- print history list               */
    bool history_clear;           /* /hc -- clear all history                */
    int  history_remove;          /* /hc# -- remove entry at index (1-9), 0=not set */
    
    /* Service retry count */
    int  service_retry_count;     /* /retry <n> -- max retries for service busy (0=use config default) */
    bool service_retry_set;       /* true if /retry was specified on command line */
    
    /* Agentic debug mode */
    bool agentic_debug;           /* /agdb -- agentic debugging mode         */
} NcdOptions;

/*
 * NcdMatch  --  one candidate result returned by the matcher
 */
typedef struct {
    char  full_path[NCD_MAX_PATH]; /* e.g. "C:\Users\Scott\Downloads"        */
    char  drive_letter;            /* 'C'                                    */
    int   drive_index;             /* index in NcdDatabase.drives[]          */
    int   dir_index;               /* index in DriveData.dirs[]              */
} NcdMatch;

/* ---------------------------------------------------- utility macros      */

#define NCD_ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

/* ================================================================ memory utilities */

/* Allocate memory, exit on failure */
void *ncd_malloc(size_t size);
void *ncd_realloc(void *ptr, size_t size);
char *ncd_strdup(const char *s);
void *ncd_calloc(size_t count, size_t size);

/* Allocate with overflow checking */
void *ncd_malloc_array(size_t count, size_t size);  /* count * size with overflow check */

/* Overflow-checked arithmetic - exit on overflow */
size_t ncd_mul_overflow_check(size_t a, size_t b);  /* a * b with overflow check */
size_t ncd_add_overflow_check(size_t a, size_t b);  /* a + b with overflow check */

/* ================================================================ path utilities */

/* Get parent directory. Returns false if already at root.
 * Handles Windows "C:\" and Linux "/" correctly. */
bool path_parent(const char *path, char *out, size_t out_size);

/* Join base path with name using platform separator.
 * Returns false if result would exceed buffer. */
bool path_join(char *out, size_t out_size, const char *base, const char *name);

/* Get last component of path (filename/directory name).
 * Returns pointer into input string (not allocated). */
const char *path_leaf(const char *path);

/* Check if path is absolute */
bool path_is_absolute(const char *path);

/* Normalize path separators to platform standard */
void path_normalize_separators(char *path);

/* Get drive letter from Windows-style path (e.g., "C:\" -> 'C').
 * Returns 0 if not a drive path. */
char path_get_drive(const char *path);

/* ================================================================ pseudo filesystem list */

/* List of pseudo filesystems to skip during mount enumeration (Linux) */
/* Defined in src/platform.c as static - not exposed globally */

#ifdef __cplusplus
}
#endif

#endif /* NCD_H */
