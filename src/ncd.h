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
 * Platform / architecture detection
 *
 * Common compiler-provided macros:
 *   Windows: _WIN32 / _WIN64 (MSVC/MinGW/Clang-cl)
 *   Linux:   __linux__
 *   x64:     _WIN64 (MSVC) or __x86_64__ / __amd64__ (GCC/Clang)
 */
#if defined(_WIN64)
#define NCD_PLATFORM_WINDOWS 1
#define NCD_PLATFORM_LINUX   0
#define NCD_ARCH_X64         1
#elif defined(_WIN32)
#define NCD_PLATFORM_WINDOWS 1
#define NCD_PLATFORM_LINUX   0
#define NCD_ARCH_X64         0
#elif defined(__linux__) && (defined(__x86_64__) || defined(__amd64__))
#define NCD_PLATFORM_WINDOWS 0
#define NCD_PLATFORM_LINUX   1
#define NCD_ARCH_X64         1
#elif defined(__linux__)
#define NCD_PLATFORM_WINDOWS 0
#define NCD_PLATFORM_LINUX   1
#define NCD_ARCH_X64         0
#else
#error Unsupported platform. Expected Windows or Linux.
#endif

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
#endif

/* --------------------------------------------------------- binary format   */

/*
 * Magic value stored in the first 4 bytes of a binary database file.
 * Spells 'NCDB' in little-endian memory order.
 */
#define NCD_BIN_MAGIC    0x4244434EU   /* 'N'=0x4E,'C'=0x43,'D'=0x44,'B'=0x42 */
#define NCD_BIN_VERSION  1

/*
 * BinFileHdr  --  fixed-size header at offset 0 of a binary database file
 *
 * All integer fields are stored in host byte order (little-endian on x86).
 * sizeof(BinFileHdr) == 24; every field is naturally aligned.
 */
typedef struct {
    uint32_t magic;        /* NCD_BIN_MAGIC                              */
    uint16_t version;      /* NCD_BIN_VERSION                            */
    uint8_t  show_hidden;  /* 1 or 0                                     */
    uint8_t  show_system;  /* 1 or 0                                     */
    int64_t  last_scan;    /* Unix timestamp (8-byte aligned @ offset 8) */
    uint32_t drive_count;
    uint32_t pad;          /* reserved, always 0                         */
} BinFileHdr;              /* 24 bytes */

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
#define NCD_RESCAN_HOURS  24   /* trigger background rescan after this many  */

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
} NcdDatabase;

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
    char db_override[NCD_MAX_PATH]; /* /d <path>                             */
    char search[NCD_MAX_PATH];    /* the directory pattern argument          */
    bool has_search;
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

#ifdef __cplusplus
}
#endif

#endif /* NCD_H */
