/*
 * main.c  --  NewChangeDirectory  (NCD)  entry point
 *
 * Usage (via ncd.bat wrapper):
 *   ncd [options] [search]
 *
 * Options:
 *   /r          Force an immediate rescan of all drives
 *   /i          Include hidden directories in search
 *   /s          Include system directories in search
 *   /a          Include all (hidden + system) directories
 *   /d <path>   Use <path> as the database file instead of the default
 *
 * Search:
 *   A partial path such as "downloads" or "scott\downloads".
 *   Matching is case-insensitive and substring-based.
 *
 * Result communication:
 *   The exe writes %TEMP%\ncd_result.bat, which the ncd.bat wrapper then
 *   calls.  That batch file sets three environment variables:
 *
 *     NCD_STATUS   -- "OK" | "ERROR"
 *     NCD_DRIVE    -- "C:"  (empty on error)
 *     NCD_PATH     -- "C:\Users\Scott\Downloads"  (empty on error)
 *     NCD_MESSAGE  -- human-readable string (always set)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <stdarg.h>

#include "ncd.h"  /* Must include first for NCD_PLATFORM_* macros */

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#endif
#include "database.h"
#include "scanner.h"
#include "matcher.h"
#include "ui.h"
#include "platform.h"
#include "control_ipc.h"
#include "state_backend.h"
#include "shm_platform.h"
#include "cli.h"
#include "result.h"

/* When TEST_BUILD is defined, expose heuristic functions for testing */
/* heur_* functions are declared in result.h - not static */

/* Global agentic debug mode flag (used by IPC layer) */
int g_agdb_mode = 0;

/* ============================================================= database dirty tracking */

/*
 * In-memory dirty tracking for standalone database modifications.
 * When the service is running, it handles persistence. In standalone mode,
 * we track dirty databases and flush them before exit.
 */

typedef struct {
    char drive;                       /* Drive letter (e.g., 'C') */
    char path[NCD_MAX_PATH];          /* Path to database file */
    NcdDatabase *db;                  /* Pointer to database (NULL if not loaded) */
    bool dirty;                       /* true if has unsaved changes */
    time_t dirty_since;               /* When it became dirty */
} DirtyDbEntry;

#define MAX_DIRTY_DBS 26              /* Max 26 drive letters */

static DirtyDbEntry g_dirty_dbs[MAX_DIRTY_DBS];
static int g_dirty_db_count = 0;
static bool g_dirty_tracking_initialized = false;

/*
 * Initialize dirty tracking system.
 */
static void init_dirty_tracking(void)
{
    if (g_dirty_tracking_initialized) return;
    memset(g_dirty_dbs, 0, sizeof(g_dirty_dbs));
    g_dirty_db_count = 0;
    g_dirty_tracking_initialized = true;
}

/*
 * Mark a database as dirty.
 * If the database is already tracked, updates the timestamp.
 * Otherwise, adds a new entry.
 */
static void db_mark_dirty_standalone(char drive, const char *path, NcdDatabase *db)
{
    init_dirty_tracking();
    
    /* Check if already tracked */
    for (int i = 0; i < g_dirty_db_count; i++) {
        if (g_dirty_dbs[i].drive == drive) {
            g_dirty_dbs[i].dirty = true;
            g_dirty_dbs[i].dirty_since = time(NULL);
            if (db && !g_dirty_dbs[i].db) {
                g_dirty_dbs[i].db = db;
            }
            return;
        }
    }
    
    /* Add new entry */
    if (g_dirty_db_count < MAX_DIRTY_DBS) {
        DirtyDbEntry *entry = &g_dirty_dbs[g_dirty_db_count++];
        entry->drive = drive;
        platform_strncpy_s(entry->path, sizeof(entry->path), path);
        entry->db = db;
        entry->dirty = true;
        entry->dirty_since = time(NULL);
    }
}

/*
 * Clear dirty flag for a database after successful save.
 */
static void db_clear_dirty_standalone(char drive)
{
    for (int i = 0; i < g_dirty_db_count; i++) {
        if (g_dirty_dbs[i].drive == drive) {
            g_dirty_dbs[i].dirty = false;
            g_dirty_dbs[i].dirty_since = 0;
            return;
        }
    }
}

/*
 * Flush all dirty databases to disk.
 * Returns number of databases flushed.
 * Called via atexit() to ensure databases are saved before program exit.
 */
static void flush_all_dirty_dbs(void)
{
    for (int i = 0; i < g_dirty_db_count; i++) {
        if (g_dirty_dbs[i].dirty) {
            NcdDatabase *db = g_dirty_dbs[i].db;
            const char *path = g_dirty_dbs[i].path;
            
            if (db && path[0]) {
                if (db_save_binary_single(db, 0, path)) {
                    db_clear_dirty_standalone(g_dirty_dbs[i].drive);
                }
            }
        }
        
        /* Free the database after saving (or if save failed) */
        if (g_dirty_dbs[i].db) {
            db_free(g_dirty_dbs[i].db);
            g_dirty_dbs[i].db = NULL;
        }
    }
    g_dirty_db_count = 0;
}

/* ============================================================= state backend */

/*
 * Global state view for service-backed database access.
 * Initialized on first use, closed at exit.
 */
static NcdStateView *g_state_view = NULL;
static NcdStateSourceInfo g_state_info = {0};
static bool g_state_initialized = false;

/* Forward declaration for cleanup function */
static void close_state_backend(void);

/*
 * Initialize the state backend (service-first, fallback to local).
 * Returns true if state is available (either service or local).
 */
static bool ensure_state_initialized(void)
{
    if (g_state_initialized) {
        return g_state_view != NULL;
    }
    
    g_state_initialized = true;
    
    int result = state_backend_open_best_effort(&g_state_view, &g_state_info);

    if (result != 0 || !g_state_view) {
        /* Check if service is busy (loading/scanning) */
        const char *err = state_backend_error_string();
        if (err && strstr(err, "not yet available")) {
            /* Service exists but is busy - report error to user */
            ncd_printf("NCD: %s\r\n", err);
            g_state_view = NULL;
            return false;
        }
        NCD_DEBUG_LOG("NCD DEBUG: State backend initialization failed, using local mode\n");
        g_state_view = NULL;
        return false;
    }

    NCD_DEBUG_LOG("NCD DEBUG: State backend initialized successfully (service=%d)\n",
            g_state_info.from_service);
    
    /* Register cleanup handler to close state backend at exit */
    atexit(close_state_backend);
    
    return true;
}

/*
 * Get database from state backend.
 * Returns database pointer (owned by state view), or NULL if not available.
 */
static const NcdDatabase *get_state_database(void)
{
    if (!ensure_state_initialized()) {
        return NULL;
    }

    const NcdDatabase *db = state_view_database(g_state_view);

    if (!db) {
        NCD_DEBUG_LOG("NCD DEBUG: state_view_database returned NULL\n");
    }
    
    return db;
}

/*
 * Get metadata from state backend.
 * Returns metadata pointer (owned by state view), or NULL if not available.
 */
static const NcdMetadata *get_state_metadata(void)
{
    if (!ensure_state_initialized()) {
        return NULL;
    }
    return state_view_metadata(g_state_view);
}

/*
 * Close the state backend and free resources.
 * Called at program exit.
 */
static void close_state_backend(void)
{
    if (g_state_view) {
        state_backend_close(g_state_view);
        g_state_view = NULL;
    }
    g_state_initialized = false;
}

/*
 * Helper: Check if we're running with service backend
 */
static bool is_service_backend(void)
{
    return (g_state_view != NULL && g_state_info.from_service);
}

/* ============================================================= directory history */

/*
 * Add current working directory to the directory history.
 * Called whenever NCD successfully navigates to a directory.
 */
static void add_current_dir_to_history(void)
{
    char cwd[MAX_PATH] = {0};
    if (!platform_get_current_dir(cwd, sizeof(cwd))) return;
    
    char drive = platform_get_drive_letter(cwd);
    if (drive == 0) {
        drive = cwd[0];
    }
    
    if (is_service_backend()) {
        /* Service mode: send update via IPC
         * Data format: [path string (null-terminated)][drive byte]
         * We send the actual string bytes, not a pointer! */
        size_t path_len = strlen(cwd) + 1;  /* Include null terminator */
        size_t data_len = path_len + 1;     /* path + drive byte */
        char *data = (char *)malloc(data_len);
        if (!data) return;
        memcpy(data, cwd, path_len);
        data[path_len] = drive;  /* Drive byte after null terminator */
        state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_DIR_HISTORY_ADD, 
                                             data, data_len);
        free(data);
    } else {
        /* Standalone mode: load, modify, save */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        if (!meta) return;
        
        if (db_dir_history_add(meta, cwd, drive)) {
            meta->dir_history_dirty = true;
            db_metadata_save(meta);
        }
        db_metadata_free(meta);
    }
}

/* ============================================================= heuristics */

/* 
 * Global metadata container - loaded once at startup, saved as needed.
 * The enhanced heuristics system uses consolidated metadata with:
 *   - Frequency tracking (how many times a search->target was chosen)
 *   - Recency tracking (timestamp of last use)
 *   - Score-based ranking (frequency * 10 / (1 + days/7))
 *   - Partial matching for similar searches
 */
static NcdMetadata *g_metadata = NULL;

/*
 * Callback for history deletion in UI (service mode)
 */
static bool history_delete_service_cb(int index, const char *path, void *user_data)
{
    (void)path;
    (void)user_data;
    if (g_state_view && g_state_info.from_service) {
        int result = state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_DIR_HISTORY_REMOVE, &index, sizeof(index));
        return (result == 0);
    }
    return false;
}

/* Helper: Ensure metadata is loaded */
static NcdMetadata* get_metadata(void)
{
    if (!g_metadata) {
        g_metadata = db_metadata_load();
    }
    return g_metadata;
}

/* Helper: Save metadata if dirty (standalone mode only) */
static void save_metadata_if_dirty(void)
{
    if (is_service_backend()) {
        /* Service handles persistence - nothing to do here */
        return;
    }
    if (g_metadata && (g_metadata->heuristics_dirty || g_metadata->config_dirty)) {
        db_metadata_save(g_metadata);
    }
}

/* Helper: Check if a path matches an exclusion pattern */
static bool match_path_exclusion(const char *path, const NcdExclusionEntry *entry)
{
    if (!path || !entry || !entry->pattern[0]) return false;
    
    char norm_path[NCD_MAX_PATH];
    char norm_pattern[NCD_MAX_PATH];
    platform_strncpy_s(norm_path, sizeof(norm_path), path);
    platform_strncpy_s(norm_pattern, sizeof(norm_pattern), entry->pattern);
#if NCD_PLATFORM_WINDOWS
    for (char *p = norm_path; *p; p++) {
        if (*p == '/') *p = NCD_PATH_SEP_CHAR;
    }
    for (char *p = norm_pattern; *p; p++) {
        if (*p == '/') *p = NCD_PATH_SEP_CHAR;
    }
#endif
    
    const char *pattern = norm_pattern;
    
    /* For root-only matches, check if pattern matches from the beginning */
    if (entry->match_from_root) {
        /* Must match from the beginning of the path */
        size_t pat_len = strlen(pattern);
        size_t path_len = strlen(norm_path);
        
        if (path_len < pat_len) return false;
        
        /* Check if path starts with pattern (case-insensitive) */
#if NCD_PLATFORM_WINDOWS
        if (_strnicmp(norm_path, pattern, pat_len) == 0) return true;
#else
        if (strncasecmp(norm_path, pattern, pat_len) == 0) return true;
#endif
        return false;
    }
    
    /* For non-root patterns, check if pattern appears anywhere in path */
    /* Simple wildcard matching for * and ? */
    const char *p = pattern;
    const char *s = norm_path;
    const char *star = NULL;
    const char *ss = s;
    
    while (*s) {
        if (*p == '*') {
            star = p++;
            ss = s;
        } else if (*p == '?') {
            p++;
            s++;
        } else if (*p && (tolower((unsigned char)*p) == tolower((unsigned char)*s))) {
            p++;
            s++;
        } else if (star) {
            p = star + 1;
            ss++;
            s = ss;
        } else {
            break;
        }
    }
    
    /* Check if we consumed the entire pattern */
    while (*p == '*') p++;
    return (*p == '\0');
}

/* Helper: Check if a directory should be excluded */
static bool is_path_excluded(char drive_letter, const char *path, const NcdExclusionList *exclusions)
{
    if (!exclusions || exclusions->count == 0 || !path) return false;
    
    for (int i = 0; i < exclusions->count; i++) {
        NcdExclusionEntry *entry = &exclusions->entries[i];
        
        /* Check drive match (0 means all drives) */
#if NCD_PLATFORM_WINDOWS
        if (entry->drive != 0) {
            char d = (char)toupper((unsigned char)drive_letter);
            if (d != entry->drive) continue;
        }
#else
        (void)drive_letter;
#endif
        
        /* Check if path matches the exclusion pattern */
        if (match_path_exclusion(path, entry)) {
            return true;
        }
        
        /* For patterns with parent\child syntax, check each path component */
        if (entry->has_parent_match) {
            const char *sep = path;
            while (*sep) {
                /* Find next path separator */
                const char *bs = strchr(sep, '\\');
                const char *fs = strchr(sep, '/');
                sep = bs ? (fs && fs < bs ? fs : bs) : fs;
                if (!sep) break;
                sep++;
                if (match_path_exclusion(sep, entry)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Helper: Filter matches based on exclusion list */
static int filter_matches_by_exclusions(NcdMatch *matches, int match_count, const NcdExclusionList *exclusions)
{
    if (!matches || match_count <= 0 || !exclusions || exclusions->count == 0) {
        return match_count;
    }
    
    int write_idx = 0;
    for (int read_idx = 0; read_idx < match_count; read_idx++) {
        NcdMatch *m = &matches[read_idx];
        if (!is_path_excluded(m->drive_letter, m->full_path, exclusions)) {
            /* Keep this match - move it to write position if needed */
            if (write_idx != read_idx) {
                matches[write_idx] = matches[read_idx];
            }
            write_idx++;
        }
        /* If excluded, simply don't copy it (effectively removing it) */
    }
    
    return write_idx;  /* New count */
}

/* heur_sanitize is now in result.c */

bool heur_get_preferred(const char *search_raw, char *out_path, size_t out_size)
{
    if (!out_path || out_size == 0) return false;
    out_path[0] = '\0';

    char key[NCD_MAX_PATH];
    heur_sanitize(search_raw, key, sizeof(key), true);
    if (key[0] == '\0') return false;

    NcdMetadata *meta = get_metadata();
    if (!meta) return false;

    return db_heur_get_preferred(meta, key, out_path, out_size);
}

void heur_note_choice(const char *search_raw, const char *target_path)
{
    char key[NCD_MAX_PATH];
    char target[NCD_MAX_PATH];
    heur_sanitize(search_raw, key, sizeof(key), true);
    heur_sanitize(target_path, target, sizeof(target), false);
    if (key[0] == '\0' || target[0] == '\0') return;

    NcdMetadata *meta = get_metadata();
    if (!meta) return;

    db_heur_note_choice(meta, key, target);
    save_metadata_if_dirty();
}

/* heur_promote_match is now in result.c */

void heur_print(void)
{
    NcdMetadata *meta = get_metadata();
    if (!meta) {
        ncd_println("NCD history: error loading metadata");
        return;
    }
    
    db_heur_print(meta);
}

void heur_clear(void)
{
    NcdMetadata *meta = get_metadata();
    if (!meta) {
        ncd_println("NCD history: error loading metadata");
        return;
    }
    
    db_heur_clear(meta);
    save_metadata_if_dirty();
    ncd_println("NCD history cleared.");
}

/* ================================================================ output   */

/*
 * ncd_print / ncd_println -- write directly to the console via WriteConsoleA.
 *
 * WHY: fprintf(stderr,...) is unreliable in some MinGW CRT configurations --
 * the stderr handle may not be properly attached to the console window.
 * printf(stdout) fares better but is still subject to CRT line-buffering.
 * WriteConsoleA bypasses the CRT entirely, works from any thread, and
 * never requires flushing.
 *
 * We open CONOUT$ once at startup and keep the handle in g_con.
 * If that fails (e.g. output is redirected) we fall back to puts/printf.
 */
static PlatformHandle g_con = NULL;

static void con_init(void)
{
    g_con = platform_console_open_out(false);
}

static void con_close(void)
{
    if (g_con) {
        platform_handle_close(g_con);
        g_con = NULL;
    }
}

/* ============================================================== version   */
/*
 * Bump NCD_BUILD_VER with every change so it is always possible to tell
 * exactly which binary is installed.  __DATE__ / __TIME__ are injected by
 * the compiler, giving a precise build timestamp at zero maintenance cost.
 *
 * The banner is printed ONLY when the user explicitly passes /v on the
 * command line.  It is silent in all other paths.
 * 
 * IMPORTANT: This version MUST match NCD_APP_VERSION in control_ipc.h
 * and SERVICE_VERSION in service_main.c
 */
#define NCD_BUILD_VER   "1.3"
#define NCD_BUILD_STAMP __DATE__ " " __TIME__

static void print_version(void)
{
    printf("NCD v%s  [built %s]\n", NCD_BUILD_VER, NCD_BUILD_STAMP);
    fflush(stdout);
}

/* ================================================================ result  */

/*
 * Write the ncd_result.bat file to %TEMP%.
 *
 * On success:
 *   NCD_STATUS=OK
 *   NCD_DRIVE=C:
 *   NCD_PATH=C:\Users\Scott\Downloads
 *   NCD_MESSAGE=Changed to C:\Users\Scott\Downloads
 *
 * On error:
 *   NCD_STATUS=ERROR
 *   NCD_DRIVE=
 *   NCD_PATH=
 *   NCD_MESSAGE=<reason>
 */
/* Result functions are defined in result.c */
extern void write_result(bool ok, const char *drive, const char *path,
                         const char *message);
extern void result_error(const char *fmt, ...);
extern void result_cancel(void);
extern void result_ok(const char *full_path, char drive_letter);

/* Return true when test mode safety guards are enabled. */
static bool ncd_test_mode_active(void)
{
    const char *test_mode = getenv("NCD_TEST_MODE");
    return test_mode && test_mode[0] && strcmp(test_mode, "0") != 0;
}

/* ============================================= background rescan spawner  */

/*
 * Spawn a detached copy of ourself with /r to refresh the database
 * in the background.  We pass the database path explicitly so the
 * spawned process uses the same one.
 */
static void spawn_background_rescan(const char *db_path)
{
    (void)db_path;   /* per-drive files always live in %LOCALAPPDATA% */
    
    /* Skip background rescan in test mode to avoid scanning user drives */
    if (ncd_test_mode_active()) {
        return;
    }
    
    char exe_path[MAX_PATH];
    if (!platform_get_module_path(exe_path, sizeof(exe_path)))
        return;

    char cmd[MAX_PATH + 8];
    snprintf(cmd, sizeof(cmd), "\"%s\" /r", exe_path);

    platform_spawn_detached(cmd);
}

/* ============================================================= version check */

/*
 * check_service_version  --  Check service version and handle mismatch
 *
 * Returns true if versions match (or no service running).
 * Returns false if there's a version mismatch (service was stopped).
 * Sets *out_service_was_stopped if service was stopped due to mismatch.
 * Sets *out_service_running to indicate if service is currently running.
 */
static bool check_service_version(bool *out_service_was_stopped, bool *out_service_running)
{
    if (out_service_was_stopped) {
        *out_service_was_stopped = false;
    }
    if (out_service_running) {
        *out_service_running = false;
    }
    
    if (!ipc_service_exists()) {
        /* Retry once — on Windows the named pipe may briefly not exist
         * between server accept cycles. */
        platform_sleep_ms(50);
        if (!ipc_service_exists()) {
            return true;  /* No service running, OK to start new one */
        }
    }

    /* Service exists - mark as running */
    if (out_service_running) {
        *out_service_running = true;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        return true;  /* Can't connect, service might be starting */
    }
    
    NcdIpcVersionCheckResult result;
    NcdIpcResult ipc_result = ipc_client_check_version(client, 
                                                        NCD_BUILD_VER, 
                                                        NCD_BUILD_STAMP,
                                                        &result);
    ipc_client_disconnect(client);
    
    if (ipc_result == NCD_IPC_OK && result.versions_match) {
        return true;  /* Versions match */
    }
    
    /* Version mismatch - report to user */
    ncd_println("");
    ncd_println("==================================================");
    ncd_println("VERSION MISMATCH DETECTED");
    ncd_println("==================================================");
    ncd_printf("Client version:  %s (built %s)\r\n", 
               result.client_version, result.client_build);
    ncd_printf("Service version: %s (built %s)\r\n",
               result.service_version, result.service_build);
    ncd_println("");
    ncd_println(result.message);
    ncd_println("==================================================");
    ncd_println("");
    
    if (out_service_was_stopped) {
        *out_service_was_stopped = result.service_was_stopped;
    }
    
    return false;
}

/* ============================================================= text encoding */

/* Global runtime text encoding mode (set during startup) */
static uint8_t g_text_encoding = NCD_TEXT_UTF8;

/*
 * Resolve active encoding mode in precedence order:
 * 1. Explicit CLI override (/u8 or /u16)
 * 2. Service-advertised encoding (if connected)
 * 3. Metadata encoding flag
 * 4. Database header/BOM probe from existing database files
 * 5. Default: UTF8
 */
static uint8_t resolve_text_encoding(const NcdOptions *opts)
{
    /* 1. CLI override takes highest precedence */
    if (opts->encoding_switch) {
        return opts->text_encoding;
    }
    
    /* 2. Service-advertised encoding (if connected and running) */
    if (ipc_service_exists()) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcStateInfo info;
            if (ipc_client_get_state_info(client, &info) == NCD_IPC_OK) {
                ipc_client_disconnect(client);
                if (info.text_encoding == NCD_TEXT_UTF8 || 
                    info.text_encoding == NCD_TEXT_UTF16LE) {
                    return (uint8_t)info.text_encoding;
                }
            }
            ipc_client_disconnect(client);
        }
    }
    
    /* 3. Metadata encoding flag */
    NcdConfig cfg;
    if (db_config_load(&cfg)) {
        if (cfg.text_encoding == NCD_TEXT_UTF8 || cfg.text_encoding == NCD_TEXT_UTF16LE) {
            return cfg.text_encoding;
        }
    }
    
    /* 4. Database header/BOM probe would go here */
    /* For now, just check the first drive's database file */
    char db_path[MAX_PATH];
    for (char letter = 'C'; letter <= 'Z'; letter++) {
        if (db_drive_path(letter, db_path, sizeof(db_path))) {
            if (platform_file_exists(db_path)) {
                FILE *f = fopen(db_path, "rb");
                if (f) {
                    uint8_t bom[2];
                    if (fread(bom, 1, 2, f) == 2) {
                        fclose(f);
                        /* Check for UTF16LE BOM */
                        if (bom[0] == 0xFF && bom[1] == 0xFE) {
                            return NCD_TEXT_UTF16LE;
                        }
                    } else {
                        fclose(f);
                    }
                    /* File exists but no BOM - assume UTF8 */
                    break;
                }
            }
        }
    }
    
    /* 5. Default to UTF8 */
    return NCD_TEXT_UTF8;
}

/*
 * Handle encoding mode switch.
 * Returns true if switch was confirmed and applied, false if cancelled or error.
 */
static bool handle_encoding_switch(uint8_t new_encoding, uint8_t current_encoding)
{
    if (new_encoding == current_encoding) {
        /* Already in the requested mode */
        const char *mode_name = (new_encoding == NCD_TEXT_UTF16LE) ? "UTF16" : "UTF8";
        ncd_printf("Text encoding is already set to %s.\r\n", mode_name);
        return true;
    }
    
    const char *mode_name = (new_encoding == NCD_TEXT_UTF16LE) ? "UTF16" : "UTF8";
    
    ncd_println("");
    ncd_printf("WARNING: Changing encoding to %s will invalidate existing database scans.\r\n", mode_name);
    ncd_println("A full rescan (/r) is required after this change.");
    ncd_println("Continue? (y/n)");
    
    /* Read response */
    char response[16];
    if (!fgets(response, sizeof(response), stdin)) {
        ncd_println("Cancelled.");
        return false;
    }
    if (response[0] != 'y' && response[0] != 'Y') {
        ncd_println("Cancelled.");
        return false;
    }
    
    /* Cannot change encoding while service is running - would require service restart */
    if (is_service_backend()) {
        ncd_println("Error: Cannot change encoding while service is running.");
        ncd_println("Please stop the service first: ncd_service stop");
        return false;
    }
    
    /* Update metadata config */
    NcdMetadata *meta = db_metadata_load();
    if (!meta) {
        meta = db_metadata_create();
    }
    if (!meta) {
        ncd_println("Error: Could not load or create metadata.");
        return false;
    }
    
    meta->cfg.text_encoding = new_encoding;
    meta->config_dirty = true;
    
    /* Save metadata */
    if (!db_metadata_save(meta)) {
        db_metadata_free(meta);
        ncd_println("Error: Could not save configuration.");
        return false;
    }
    db_metadata_free(meta);
    
    /* Set the global encoding for this session */
    g_text_encoding = new_encoding;
    db_set_text_encoding(new_encoding);
    
    /* Invalidate all database files by deleting them */
    char db_path[MAX_PATH];
    int deleted_count = 0;
    for (char letter = 'A'; letter <= 'Z'; letter++) {
        if (db_drive_path(letter, db_path, sizeof(db_path))) {
            if (platform_file_exists(db_path)) {
                if (platform_delete_file(db_path)) {
                    deleted_count++;
                }
            }
        }
    }
    
    ncd_printf("Encoding changed to %s. %d database file(s) invalidated.\r\n", 
               mode_name, deleted_count);
    ncd_println("Run 'ncd -r' to perform a full rescan.");
    
    return true;
}

/* ============================================================== CLI parse */

static void print_usage(void)
{
    /* Check service status for header line */
    const char *status_suffix;
    bool service_was_stopped = false;
    bool service_running = false;
    check_service_version(&service_was_stopped, &service_running);
    if (!service_running) {
        status_suffix = "  [Standalone client.]";
    } else {
        /* Service exists - check if ready */
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            status_suffix = "  [Service: Starting...]";
        } else {
            NcdIpcStateInfo info;
            NcdIpcResult result = ipc_client_get_state_info(client, &info);
            ipc_client_disconnect(client);
            if (result != NCD_IPC_OK || info.db_generation == 0) {
                status_suffix = "  [Service: Starting...]";
            } else {
                status_suffix = "  [Service: Running.]";
            }
        }
    }
    
    ncd_printf("%s%s\r\n"
        "\r\n"
        "Usage:\r\n"
        "  ncd [options] <search>           Search and navigate to a directory\r\n"
        "  ncd [command]:[value]            Execute specific maintenance or action\r\n"
        "  ncd -h | --help                  Show this help\r\n"
        "\r\n"
        "Navigation:\r\n"
        "  <search>                         Partial directory name (wildcards *, ? supported)\r\n"
        "  .                                Browse from current directory (navigator mode)\r\n"
#if NCD_PLATFORM_WINDOWS
        "  X: or X:\\                        Browse from drive root (navigator mode)\r\n"
#else
        "  /                                Browse from root filesystem (navigator mode)\r\n"
        "  /mnt/X                           Browse from Windows drive X: via /mnt (WSL only)\r\n"
#endif
        "  @<group>                         Jump to a group/bookmark (see Groups below)\r\n"
        "\r\n"
        "Search Options (Bundling supported, e.g., -asz):\r\n"
        "  -i, --hidden                     Include hidden directories\r\n"
        "  -s, --system                     Include system directories\r\n"
        "  -a, --all                        Include all (hidden + system)\r\n"
        "  -z, --fuzzy                      Fuzzy matching (typo-tolerant)\r\n"
        "\r\n"
        "History & Frequency:\r\n"
        "  -0                               Ping-pong: swap between last two directories\r\n"
        "  -1 .. -9                         Jump to Nth directory in history (-1=oldest, -9=newest)\r\n"
        "  -h                               Browse history (interactive, Del to remove)\r\n"
        "  -h:l                             List directory history\r\n"
        "  -h:c                             Clear all directory history\r\n"
        "  -h:c<#>                          Remove history entry # (e.g., -h:c3)\r\n"
        "  -f                               Show frequent searches (last 20)\r\n"
        "  -f:c                             Clear frequent search history\r\n"
        "\r\n"
        "Groups:\r\n"
        "  -g:@<name>                       Add current directory to group (e.g., -g:@proj)\r\n"
        "  -G:@<name>                       Remove current directory from group\r\n"
        "  -g:l                             List all groups and their directories\r\n"
        "\r\n"
        "Scanning:\r\n"
#if NCD_PLATFORM_WINDOWS
        "  -r                               Rescan all drives\r\n"
#else
        "  -r                               Rescan all mounted filesystems\r\n"
        "  -r:/                             Rescan root filesystem only (excludes /mnt/*)\r\n"
#endif
        "  -r:.                             Rescan current subdirectory only\r\n"
        "  -r:<drives>                      Rescan specific drives (-r:d, -r=d, or -r d)\r\n"
        "  -r:-<drives>                     Rescan all except specified (-r:-c, -r=-c, or -r -c)\r\n"
        "\r\n"
        "Exclusions:\r\n"
#if NCD_PLATFORM_WINDOWS
        "  -x:<pat>                         Add exclusion pattern (-x:pat, -x=pat, or -x pat)\r\n"
#else
        "  -x:<pat>                         Add exclusion pattern (-x:pat, -x=pat, or -x pat)\r\n"
#endif
        "  -X:<pat>                         Remove exclusion pattern (-X:pat, -X=pat, or -X pat)\r\n"
        "  -x:l                             List exclusion patterns\r\n"
        "\r\n"
        "Configuration & System:\r\n"
        "  -c, --config                     Edit default options interactively\r\n"
        "  -d:<path>                        Use alternate database file (-d:path, -d=path, or -d path)\r\n"
        "  -t:<sec>                         Scan timeout in seconds (-t:30, -t=30, or -t 30)\r\n"
        "  --retry:<n>                      Service busy retry count (--retry:5, --retry=5, or --retry 5)\r\n"
        "  -u:8 | -u:16                     Set text encoding (-u:8, -u=8, -u 8, -u:16, -u=16, or -u 16)\r\n"
        "  -v, --version                    Print version\r\n"
        "  --agent:<cmd>                    LLM integration mode (--agent:query, --agent=query, or --agent query)\r\n"
#if NCD_PLATFORM_WINDOWS
        "\r\n"
        "Windows Note:\r\n"
        "  / prefix acts as a silent alias for - (single flag only).\r\n"
        "  Example: /i works like -i, but /as is not a bundle.\r\n"
#endif
        ,
        platform_get_app_title(), status_suffix);
    
    /* Platform-specific suffix */
    char suffix_buf[512];
    if (platform_write_help_suffix(suffix_buf, sizeof(suffix_buf)) > 0) {
        ncd_print(suffix_buf);
    }
    
#if NCD_PLATFORM_WINDOWS
    ncd_print(
        "\r\n"
        "Examples:\r\n"
        "  ncd downloads              Search for \"downloads\"\r\n"
        "  ncd scott\\downloads        Search with path context\r\n"
        "  ncd down*                  Wildcard: matches downloads, downgrade, etc.\r\n"
        "  ncd *load*                 Wildcard: matches downloads, uploads, etc.\r\n"
        "  ncd -z donloads            Fuzzy: matches \"downloads\" despite typo\r\n"
        "  ncd .                      Browse current directory tree\r\n"
        "  ncd -g:@proj               Add current dir to @proj group\r\n"
        "  ncd @proj                  Jump to @proj group\r\n"
        "  ncd -0                     Swap to previous directory\r\n"
        "  ncd -r                     Rescan all drives\r\n"
        "  ncd -r:bde                 Rescan drives B:, D:, E: only\r\n"
        "  ncd -r bde                 Same as above (space-separated)\r\n"
        "  ncd -r=bde                 Same as above (= separator)\r\n"
#else
    ncd_print(
        "\r\n"
        "Examples:\r\n"
        "  ncd downloads              Search for \"downloads\"\r\n"
        "  ncd scott/downloads        Search with path context\r\n"
        "  ncd down*                  Wildcard: matches downloads, downgrade, etc.\r\n"
        "  ncd *load*                 Wildcard: matches downloads, uploads, etc.\r\n"
        "  ncd -z donloads            Fuzzy: matches \"downloads\" despite typo\r\n"
        "  ncd .                      Browse current directory tree\r\n"
        "  ncd -g:@proj               Add current dir to @proj group\r\n"
        "  ncd @proj                  Jump to @proj group\r\n"
        "  ncd -0                     Swap to previous directory\r\n"
        "  ncd -r                     Rescan all mounted filesystems\r\n"
        "  ncd -r:/                   Rescan root filesystem only\r\n"
        "  ncd -r /                   Same as above (space-separated)\r\n"
        "  ncd -r=/                   Same as above (= separator)\r\n"
#endif
        "\r\n"
        "Tab Completion:\r\n"
        "  Bash:    source completions/ncd.bash\r\n"
        "  Zsh:     Add completions/_ncd to your fpath\r\n"
        "  PowerShell: . completions/ncd.ps1\r\n"
    );
}

/* parse_drive_list_token, parse_args, parse_agent_args, glob_match are defined in cli.c */
extern bool parse_args(int argc, char *argv[], NcdOptions *opts);

/* ================================================================ agent mode */

/* Agent output uses stdout (not WriteConsoleA) for redirectability */
static void agent_print(const char *s)
{
    fputs(s, stdout);
}

static void agent_print_char(char c)
{
    fputc(c, stdout);
}

static void agent_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void agent_json_escape(const char *s)
{
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '\\': agent_print("\\\\"); break;
            case '"': agent_print("\\\""); break;
            case '\b': agent_print("\\b"); break;
            case '\f': agent_print("\\f"); break;
            case '\n': agent_print("\\n"); break;
            case '\r': agent_print("\\r"); break;
            case '\t': agent_print("\\t"); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    agent_printf("\\u%04x", (unsigned char)*p);
                } else {
                    agent_print_char(*p);
                }
        }
    }
}

static void agent_print_usage(void)
{
    agent_print(
        "NCD Agent Mode -- Filesystem oracle for LLM agents\r\n"
        "\r\n"
        "Usage:\r\n"
        "  ncd --agent:query <search> [options]\r\n"
        "  ncd --agent:ls <path> [options]\r\n"
        "  ncd --agent:tree <path> [options]\r\n"
        "  ncd --agent:check <path> | --db-age | --stats | --service-status\r\n"
        "  ncd --agent:complete <partial> [--limit N] [--json]\r\n"
        "  ncd --agent:mkdir <path> [--json]\r\n"
        "  ncd --agent:mkdirs [--file <path>] [--json] <content>\r\n"
        "  ncd --agent:quit [--json]\r\n"
        "\r\n"
        "Commands:\r\n"
        "  query <search>    Search the NCD index for directories\r\n"
        "  ls <path>         List directory contents (live filesystem)\r\n"
        "  tree <path>       Show directory structure from DB\r\n"
        "  check <path>      Check if path exists in DB (exit code)\r\n"
        "  complete <partial>  Shell tab-completion candidates\r\n"
        "  mkdir <path>        Create a directory and add to database\r\n"
        "  mkdirs <content>    Create directory tree from JSON or flat format\r\n"
        "  quit                Request graceful service shutdown\r\n"
        "\r\n"
        "Query Options:\r\n"
        "  --json            JSON output instead of compact\r\n"
        "  --limit N         Cap results (default: unlimited, 0=unlimited)\r\n"
        "  --all             Include hidden + system directories\r\n"
        "  --depth, --depth-sort  Sort shallowest first (default: by score)\r\n"
        "\r\n"
        "ls Options:\r\n"
        "  --depth N         Recurse N levels (default: 1)\r\n"
        "  --dirs-only       Only directories\r\n"
        "  --files-only      Only files\r\n"
        "  --pattern <glob>  Filter by name (e.g., *.py)\r\n"
        "  --json            JSON output\r\n"
        "\r\n"
        "tree Options:\r\n"
        "  --depth N         Max depth (default: 3)\r\n"
        "  --json            JSON array of entries with name/depth fields\r\n"
        "  --flat            Flat list instead of indented\r\n"
        "                    (with --json, emits flat relative paths)\r\n"
        "\r\n"
        "check Options:\r\n"
        "  --db-age          Output DB age in seconds\r\n"
        "  --stats           Output dir count per drive\r\n"
        "  --service-status  Check if NCD service is running and ready\r\n"
        "  --json            JSON output\r\n"
        "\r\n"
        "complete Options:\r\n"
        "  --limit N         Max candidates (default: 20)\r\n"
        "  --json            JSON output\r\n"
        "\r\n"
        "mkdir Options:\r\n"
        "  --json            JSON output with result code\r\n"
        "\r\n"
        "mkdirs Options:\r\n"
        "  --file <path>     Read tree specification from file\r\n"
        "  --json            JSON output with per-directory results\r\n"
        "\r\n"
        "mkdirs Input Formats:\r\n"
        "  JSON: [{\"name\":\"dir\",\"children\":[{\"name\":\"subdir\"}]}]\r\n"
        "  Flat: 2-space indentation indicates child directories\r\n"
        "        parent\r\n"
        "          child1\r\n"
        "          child2\r\n"
        "            grandchild\r\n"
        "\r\n"
        "Exit Codes:\r\n"
        "  0   Success / Found\r\n"
        "  1   Not found / Error\r\n"
    );
}

/* parse_agent_args and glob_match are defined in cli.c */
extern bool parse_agent_args(int argc, char *argv[], int *consumed, NcdOptions *opts);
extern bool glob_match(const char *pattern, const char *text);

/* ============================================================ agent mode   */

/* Agent mode: query - Search the NCD index for directories */
static int agent_mode_query(NcdDatabase *db, const NcdOptions *opts)
{
    if (!db) {
        agent_print("{\"error\":\"no database\"}\r\n");
        return 0;
    }
    
    int match_count = 0;
    NcdMatch *matches = matcher_find(db, opts->search, opts->show_hidden, opts->show_system, &match_count);
    
    if (!matches || match_count == 0) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"query\":\"");
            agent_json_escape(opts->search);
            agent_print("\",\"results\":[]}\r\n");
        }
        return 0;
    }
    
    /* Apply limit */
    int limit = opts->agent_limit;
    if (limit <= 0) limit = match_count;
    if (match_count < limit) limit = match_count;
    
    /* Sort by depth if requested - using insertion sort (O(n) for sorted/small data) */
    if (opts->agent_depth_sort && limit > 1) {
        /* Insertion sort - efficient for small or nearly-sorted data (O(n) best case)
         * vs bubble sort O(n^2) always. For agent queries, result sets are typically small. */
        for (int i = 1; i < limit; i++) {
            NcdMatch key = matches[i];
            /* Count depth once for key */
            int key_depth = 0;
            for (const char *p = key.full_path; *p; p++)
                if (*p == '\\' || *p == '/') key_depth++;
            
            int j = i - 1;
            while (j >= 0) {
                int j_depth = 0;
                for (const char *p = matches[j].full_path; *p; p++)
                    if (*p == '\\' || *p == '/') j_depth++;
                if (j_depth <= key_depth) break;
                matches[j + 1] = matches[j];
                j--;
            }
            matches[j + 1] = key;
        }
    }
    
    if (opts->agent_json) {
        agent_print("{\"v\":1,\"query\":\"");
        agent_json_escape(opts->search);
        agent_print("\",\"results\":[");
        for (int i = 0; i < limit; i++) {
            if (i > 0) agent_print(",");
            agent_print("{\"path\":\"");
            agent_json_escape(matches[i].full_path);
            agent_print("\",\"drive\":\"");
            agent_print_char(matches[i].drive_letter);
            agent_print("\"}");
        }
        agent_print("]}\r\n");
    } else {
        /* Compact format: one path per line */
        for (int i = 0; i < limit; i++) {
            agent_print(matches[i].full_path);
            agent_print("\r\n");
        }
    }
    
    free(matches);
    return limit;
}

/* Agent mode: ls - List directory contents from live filesystem */
static int agent_mode_ls(const NcdOptions *opts)
{
    if (!opts->has_search || !opts->search[0]) {
        agent_print("{\"error\":\"no path specified\"}\r\n");
        return 0;
    }
    
    char path[NCD_MAX_PATH];
    platform_strncpy_s(path, sizeof(path), opts->search);
    size_t len = strlen(path);
#if NCD_PLATFORM_WINDOWS
    /* Ensure drive letters have a trailing backslash (R: -> R:\) */
    if (len == 2 && path[1] == ':') {
        path[2] = NCD_PATH_SEP_CHAR;
        path[3] = '\0';
        len = 3;
    }
    /* Strip trailing dot after backslash (e.g., "T:\\." -> "T:\\") to work around batch quoting */
    if (len >= 2 && path[len - 1] == '.' && path[len - 2] == NCD_PATH_SEP_CHAR) {
        path[len - 1] = '\0';
        len--;
    }
#endif
    
    /* Ensure trailing separator for consistent depth calculation */
    len = strlen(path);
    if (len > 0 && path[len-1] != '\\' && path[len-1] != '/') {
        platform_strncat_s(path, sizeof(path), "/");
    }
    
    int base_depth = 0;
    for (const char *p = path; *p; p++)
        if (*p == '\\' || *p == '/') base_depth++;
    
    typedef struct {
        char path[NCD_MAX_PATH];
        char name[256];
        bool is_dir;
        int depth;
    } LsEntry;
    
    LsEntry *entries = NULL;
    int entry_count = 0;
    int entry_capacity = 0;
    
    /* Platform-specific directory enumeration */
#if NCD_PLATFORM_WINDOWS
    char search_path[NCD_MAX_PATH];
    snprintf(search_path, sizeof(search_path), "%s*", path);
    
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle = FindFirstFileExA(search_path, FindExInfoBasic, &find_data,
                                          FindExSearchNameMatch, NULL, 0);
    if (find_handle == INVALID_HANDLE_VALUE) {
        if (opts->agent_json) {
            agent_print("{\"error\":\"cannot open directory\"}\r\n");
        }
        return 0;
    }
    
    do {
        const char *name = find_data.cFileName;
        if (name[0] == '.') continue;  /* Skip hidden */
        
        bool is_dir = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        
        /* Pattern filtering */
        if (opts->agent_pattern[0] && !glob_match(opts->agent_pattern, name))
            continue;
        
        /* Type filtering */
        if (opts->agent_dirs_only && !is_dir) continue;
        if (opts->agent_files_only && is_dir) continue;
        
        if (entry_count >= entry_capacity) {
            entry_capacity = entry_capacity ? entry_capacity * 2 : 64;
            LsEntry *new_entries = (LsEntry *)realloc(entries, entry_capacity * sizeof(LsEntry));
            if (!new_entries) break;
            entries = new_entries;
        }
        
        LsEntry *e = &entries[entry_count++];
        platform_strncpy_s(e->name, sizeof(e->name), name);
        e->is_dir = is_dir;
        e->depth = 0;
        snprintf(e->path, sizeof(e->path), "%s%s", path, name);
    } while (FindNextFileA(find_handle, &find_data));
    
    FindClose(find_handle);
#else
    DIR *dir = opendir(path);
    if (!dir) {
        if (opts->agent_json) {
            agent_print("{\"error\":\"cannot open directory\"}\r\n");
        }
        return 0;
    }
    
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        if (name[0] == '.') continue;  /* Skip hidden */
        
        /* Determine if directory - use d_type when available (Linux/BSD) to avoid stat() */
        bool is_dir;
#if defined(_DIRENT_HAVE_D_TYPE) || defined(__linux__)
        if (ent->d_type != DT_UNKNOWN) {
            is_dir = (ent->d_type == DT_DIR);
        } else
#endif
        {
            /* Fallback to stat() for filesystems that don't support d_type */
            char full_path[NCD_MAX_PATH];
            snprintf(full_path, sizeof(full_path), "%s%s", path, name);
            struct stat st;
            is_dir = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
        }
        
        /* Pattern filtering */
        if (opts->agent_pattern[0] && !glob_match(opts->agent_pattern, name))
            continue;
        
        /* Type filtering */
        if (opts->agent_dirs_only && !is_dir) continue;
        if (opts->agent_files_only && is_dir) continue;
        
        if (entry_count >= entry_capacity) {
            entry_capacity = entry_capacity ? entry_capacity * 2 : 64;
            LsEntry *new_entries = (LsEntry *)realloc(entries, entry_capacity * sizeof(LsEntry));
            if (!new_entries) break;
            entries = new_entries;
        }
        
        LsEntry *e = &entries[entry_count++];
        platform_strncpy_s(e->name, sizeof(e->name), name);
        e->is_dir = is_dir;
        e->depth = 0;
        snprintf(e->path, sizeof(e->path), "%s%s", path, name);
    }
    closedir(dir);
#endif
    
    /* Recurse if depth > 1 - BFS queue for subdirectories */
    if (opts->agent_depth > 1) {
        typedef struct {
            char path[NCD_MAX_PATH];
            int depth;
        } DirQueueItem;
        
        DirQueueItem *queue = NULL;
        int qhead = 0, qtail = 0, qcapacity = 0;
        
        /* Enqueue all subdirectories from initial scan */
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].is_dir) {
                if (qtail >= qcapacity) {
                    qcapacity = qcapacity ? qcapacity * 2 : 16;
                    DirQueueItem *newq = (DirQueueItem *)realloc(queue, qcapacity * sizeof(DirQueueItem));
                    if (!newq) break;
                    queue = newq;
                }
                DirQueueItem *q = &queue[qtail++];
                platform_strncpy_s(q->path, sizeof(q->path), entries[i].path);
                q->depth = 1;
            }
        }
        
        /* BFS through subdirectories */
        while (qhead < qtail) {
            DirQueueItem cur = queue[qhead++];
            if (cur.depth >= opts->agent_depth) continue;
            
            char sub_path[NCD_MAX_PATH];
            platform_strncpy_s(sub_path, sizeof(sub_path), cur.path);
            size_t sub_len = strlen(sub_path);
            if (sub_len > 0 && sub_path[sub_len-1] != '\\' && sub_path[sub_len-1] != '/') {
                platform_strncat_s(sub_path, sizeof(sub_path), "/");
                sub_len++;
            }
            
#if NCD_PLATFORM_WINDOWS
            char sub_search[NCD_MAX_PATH];
            snprintf(sub_search, sizeof(sub_search), "%s*", sub_path);
            
            WIN32_FIND_DATAA sub_find;
            HANDLE sub_handle = FindFirstFileExA(sub_search, FindExInfoBasic, &sub_find,
                                                  FindExSearchNameMatch, NULL, 0);
            if (sub_handle != INVALID_HANDLE_VALUE) {
                do {
                    const char *name = sub_find.cFileName;
                    if (name[0] == '.') continue;
                    
                    bool is_dir = (sub_find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    
                    if (opts->agent_pattern[0] && !glob_match(opts->agent_pattern, name))
                        continue;
                    if (opts->agent_dirs_only && !is_dir) continue;
                    if (opts->agent_files_only && is_dir) continue;
                    
                    if (entry_count >= entry_capacity) {
                        entry_capacity = entry_capacity ? entry_capacity * 2 : 64;
                        LsEntry *new_entries = (LsEntry *)realloc(entries, entry_capacity * sizeof(LsEntry));
                        if (!new_entries) break;
                        entries = new_entries;
                    }
                    
                    LsEntry *e = &entries[entry_count++];
                    platform_strncpy_s(e->name, sizeof(e->name), name);
                    e->is_dir = is_dir;
                    e->depth = cur.depth;
                    snprintf(e->path, sizeof(e->path), "%s%s", sub_path, name);
                    
                    if (is_dir && cur.depth + 1 < opts->agent_depth) {
                        if (qtail >= qcapacity) {
                            qcapacity = qcapacity ? qcapacity * 2 : 16;
                            DirQueueItem *newq = (DirQueueItem *)realloc(queue, qcapacity * sizeof(DirQueueItem));
                            if (!newq) break;
                            queue = newq;
                        }
                        DirQueueItem *q = &queue[qtail++];
                        platform_strncpy_s(q->path, sizeof(q->path), e->path);
                        q->depth = cur.depth + 1;
                    }
                } while (FindNextFileA(sub_handle, &sub_find));
                FindClose(sub_handle);
            }
#else
            DIR *sub_dir = opendir(sub_path);
            if (sub_dir) {
                struct dirent *sub_ent;
                while ((sub_ent = readdir(sub_dir)) != NULL) {
                    const char *name = sub_ent->d_name;
                    if (name[0] == '.') continue;
                    
                    /* Use d_type when available to avoid stat() */
                    bool is_dir;
#if defined(_DIRENT_HAVE_D_TYPE) || defined(__linux__)
                    if (sub_ent->d_type != DT_UNKNOWN) {
                        is_dir = (sub_ent->d_type == DT_DIR);
                    } else
#endif
                    {
                        char full_path[NCD_MAX_PATH];
                        snprintf(full_path, sizeof(full_path), "%s%s", sub_path, name);
                        struct stat st;
                        is_dir = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
                    }
                    
                    if (opts->agent_pattern[0] && !glob_match(opts->agent_pattern, name))
                        continue;
                    if (opts->agent_dirs_only && !is_dir) continue;
                    if (opts->agent_files_only && is_dir) continue;
                    
                    if (entry_count >= entry_capacity) {
                        entry_capacity = entry_capacity ? entry_capacity * 2 : 64;
                        LsEntry *new_entries = (LsEntry *)realloc(entries, entry_capacity * sizeof(LsEntry));
                        if (!new_entries) break;
                        entries = new_entries;
                    }
                    
                    LsEntry *e = &entries[entry_count++];
                    platform_strncpy_s(e->name, sizeof(e->name), name);
                    e->is_dir = is_dir;
                    e->depth = cur.depth;
                    snprintf(e->path, sizeof(e->path), "%s%s", sub_path, name);
                    
                    if (is_dir && cur.depth + 1 < opts->agent_depth) {
                        if (qtail >= qcapacity) {
                            qcapacity = qcapacity ? qcapacity * 2 : 16;
                            DirQueueItem *newq = (DirQueueItem *)realloc(queue, qcapacity * sizeof(DirQueueItem));
                            if (!newq) break;
                            queue = newq;
                        }
                        DirQueueItem *q = &queue[qtail++];
                        platform_strncpy_s(q->path, sizeof(q->path), e->path);
                        q->depth = cur.depth + 1;
                    }
                }
                closedir(sub_dir);
            }
#endif
        }
        
        free(queue);
    }
    
    if (opts->agent_json) {
        agent_print("{\"v\":1,\"path\":\"");
        agent_json_escape(path);
        agent_print("\",\"entries\":[");
        for (int i = 0; i < entry_count; i++) {
            if (i > 0) agent_print(",");
            agent_print("{\"name\":\"");
            agent_json_escape(entries[i].name);
            agent_print("\",\"is_dir\":");
            agent_print(entries[i].is_dir ? "true" : "false");
            agent_print("}");
        }
        agent_print("]}\r\n");
    } else {
        for (int i = 0; i < entry_count; i++) {
            if (entries[i].is_dir) agent_print("[DIR] ");
            agent_print(entries[i].name);
            agent_print("\r\n");
        }
    }
    
    free(entries);
    return entry_count;
}

/* Simple hash map for path -> dir_index lookup */
typedef struct {
    uint64_t hash;
    int dir_index;
    char *path;  /* owned by map */
} PathMapEntry;

typedef struct {
    PathMapEntry *entries;
    int count;
    int capacity;
} PathMap;

static uint64_t hash_path(const char *path)
{
    /* FNV-1a hash */
    uint64_t h = 14695981039346656037ULL;
    while (*path) {
        h ^= (unsigned char)(*path++);
        h *= 1099511628211ULL;
    }
    return h;
}

static void path_map_init(PathMap *map, int expected_size)
{
    map->capacity = expected_size * 2;  /* 0.5 load factor */
    if (map->capacity < 16) map->capacity = 16;
    map->entries = ncd_calloc((size_t)map->capacity, sizeof(PathMapEntry));
    map->count = 0;
}

static void path_map_free(PathMap *map)
{
    for (int i = 0; i < map->capacity; i++) {
        if (map->entries[i].path)
            free(map->entries[i].path);
    }
    free(map->entries);
    map->entries = NULL;
    map->count = 0;
    map->capacity = 0;
}

static void path_map_put(PathMap *map, const char *path, int dir_index)
{
    uint64_t h = hash_path(path);
    int idx = (int)(h % (uint64_t)map->capacity);
    
    while (map->entries[idx].path != NULL) {
        idx = (idx + 1) % map->capacity;
    }
    
    map->entries[idx].hash = h;
    map->entries[idx].dir_index = dir_index;
    map->entries[idx].path = ncd_strdup(path);
    map->count++;
}

static int path_map_get(PathMap *map, const char *path)
{
    uint64_t h = hash_path(path);
    int idx = (int)(h % (uint64_t)map->capacity);
    
    while (map->entries[idx].path != NULL) {
        if (map->entries[idx].hash == h && _stricmp(map->entries[idx].path, path) == 0)
            return map->entries[idx].dir_index;
        idx = (idx + 1) % map->capacity;
    }
    return -1;
}

/* Children index: for each dir, store list of child indices */
typedef struct {
    int *children;      /* Array of child dir indices */
    int child_count;
    int child_capacity;
} DirChildren;

/* Build children index for a drive - O(n) time, O(n) space */
static DirChildren *build_children_index(const DriveData *drv)
{
    DirChildren *children = ncd_calloc((size_t)drv->dir_count, sizeof(DirChildren));
    
    /* First pass: count children per parent */
    for (int i = 0; i < drv->dir_count; i++) {
        int parent = drv->dirs[i].parent;
        if (parent >= 0 && parent < drv->dir_count)
            children[parent].child_count++;
    }
    
    /* Allocate arrays */
    for (int i = 0; i < drv->dir_count; i++) {
        if (children[i].child_count > 0) {
            children[i].children = ncd_malloc_array((size_t)children[i].child_count, sizeof(int));
            children[i].child_capacity = children[i].child_count;
            children[i].child_count = 0;  /* Reset for second pass */
        }
    }
    
    /* Second pass: fill arrays */
    for (int i = 0; i < drv->dir_count; i++) {
        int parent = drv->dirs[i].parent;
        if (parent >= 0 && parent < drv->dir_count) {
            DirChildren *pc = &children[parent];
            pc->children[pc->child_count++] = i;
        }
    }
    
    return children;
}

static void free_children_index(DirChildren *children, int dir_count)
{
    for (int i = 0; i < dir_count; i++) {
        if (children[i].children)
            free(children[i].children);
    }
    free(children);
}

/* Build path map for a drive - optimized iterative path building.
 * Uses explicit stack to avoid recursion depth issues on deeply nested trees.
 */
typedef struct { int dir; int path_len_before_name; } PathMapStackItem;

static void build_path_map(PathMap *map, const DriveData *drv, char drive_letter)
{
#if !NCD_PLATFORM_WINDOWS
    (void)drive_letter; /* Unused on Linux - mount point comes from drv->label */
#else
    (void)drive_letter; /* Mark as used to avoid warning */
#endif
    /* Stack needs to handle worst case: all dirs are root-level and have children.
     * Allocate 2x dir_count for safety (root dirs + their children on stack).
     */
    int stack_capacity = drv->dir_count * 2;
    if (stack_capacity < 256) stack_capacity = 256;
    
    PathMapStackItem *stack = ncd_malloc_array((size_t)stack_capacity, sizeof(PathMapStackItem));
    int stack_top = 0;
    
    char path[NCD_MAX_PATH];
    int root_len;
    
    /* Initialize with drive root - ensure it ends with separator */
#if NCD_PLATFORM_WINDOWS
    if (drv->label[0] && strstr(drv->label, ":\\")) {
        root_len = snprintf(path, sizeof(path), "%s", drv->label);
        if (root_len > 0 && path[root_len-1] != NCD_PATH_SEP[0]) {
            path[root_len] = NCD_PATH_SEP[0];
            path[++root_len] = '\0';
        }
    } else {
        root_len = snprintf(path, sizeof(path), "%c:%c", drive_letter, NCD_PATH_SEP[0]);
    }
#else
    root_len = snprintf(path, sizeof(path), "%s", drv->label);
    if (root_len > 0 && path[root_len-1] != NCD_PATH_SEP[0]) {
        path[root_len] = NCD_PATH_SEP[0];
        path[++root_len] = '\0';
    }
#endif
    /* root_len now includes the separator */
    
    /* Pre-build children index */
    DirChildren *children = build_children_index(drv);
    
    /* Start with root-level directories (parent == -1) */
    for (int i = 0; i < drv->dir_count; i++) {
        if (drv->dirs[i].parent == -1) {
            const char *name = drv->name_pool + drv->dirs[i].name_off;
            int name_len = (int)strlen(name);
            
            /* Build path: root already ends with separator, just add name */
            if (root_len + name_len >= (int)sizeof(path))
                continue;
            
            memcpy(path + root_len, name, name_len + 1);
            int path_len = root_len + name_len;
            
            /* Add to map */
            path_map_put(map, path, i);
            
            /* Push children */
            DirChildren *dc = &children[i];
            for (int j = dc->child_count - 1; j >= 0; j--) {
                int child_idx = dc->children[j];
                if (stack_top < stack_capacity) {
                    /* Child will restore to this path_len before adding its name */
                    stack[stack_top++] = (PathMapStackItem){child_idx, path_len};
                }
            }
        }
    }
    
    /* Process remaining stack items */
    while (stack_top > 0) {
        PathMapStackItem cur = stack[--stack_top];
        
        /* Restore path to parent length */
        path[cur.path_len_before_name] = '\0';
        
        const char *name = drv->name_pool + drv->dirs[cur.dir].name_off;
        int name_len = (int)strlen(name);
        
        /* Build path: parent + separator + name */
        int path_len = cur.path_len_before_name;
        if (path_len + 1 + name_len >= (int)sizeof(path))
            continue;
        
        path[path_len] = NCD_PATH_SEP[0];
        memcpy(path + path_len + 1, name, name_len + 1);
        path_len += 1 + name_len;
        
        /* Add to map */
        path_map_put(map, path, cur.dir);
        
        /* Push children */
        DirChildren *dc = &children[cur.dir];
        for (int j = dc->child_count - 1; j >= 0; j--) {
            int child_idx = dc->children[j];
            if (stack_top < stack_capacity) {
                stack[stack_top++] = (PathMapStackItem){child_idx, path_len};
            }
        }
    }
    
    free_children_index(children, drv->dir_count);
    free(stack);
}

/* Agent mode: tree - Show directory structure from DB */
static int agent_mode_tree(NcdDatabase *db, const NcdOptions *opts)
{
    if (!db) {
        agent_print("{\"error\":\"no database\"}\r\n");
        return 0;
    }
    
    if (!opts->has_search || !opts->search[0]) {
        agent_print("{\"error\":\"no path specified\"}\r\n");
        return 0;
    }
    
    /* Find the directory in the database */
    char search_path[NCD_MAX_PATH];
    platform_strncpy_s(search_path, sizeof(search_path), opts->search);
    
    /* Normalize path */
#if NCD_PLATFORM_WINDOWS
    for (char *p = search_path; *p; p++) {
        if (*p == '/') *p = NCD_PATH_SEP_CHAR;
    }
#endif
#if NCD_PLATFORM_WINDOWS
    /* Ensure drive letters have a trailing backslash (R: -> R:\) */
    size_t sp_len = strlen(search_path);
    if (sp_len == 2 && search_path[1] == ':') {
        search_path[2] = NCD_PATH_SEP_CHAR;
        search_path[3] = '\0';
        sp_len = 3;
    }
    /* Strip trailing dot after backslash (e.g., "T:\\." -> "T:\\") to work around batch quoting */
    if (sp_len >= 2 && search_path[sp_len - 1] == '.' && search_path[sp_len - 2] == NCD_PATH_SEP_CHAR) {
        search_path[sp_len - 1] = '\0';
    }
#endif
    
    /* Build path-to-index map for O(1) lookup - much faster than scanning */
    PathMap path_map;
    int total_dirs = 0;
    for (int d = 0; d < db->drive_count; d++)
        total_dirs += db->drives[d].dir_count;
    path_map_init(&path_map, total_dirs);
    
    for (int d = 0; d < db->drive_count; d++) {
        build_path_map(&path_map, &db->drives[d], db->drives[d].letter);
    }
    
    /* O(1) lookup instead of O(n) scan */
    int found_drive_idx = -1;
    int found_dir_idx = -1;
    
    for (int d = 0; d < db->drive_count; d++) {
        DriveData *drive = &db->drives[d];
        char drive_root[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
        if (drive->label[0] && strstr(drive->label, ":\\")) {
            snprintf(drive_root, sizeof(drive_root), "%s", drive->label);
        } else {
            snprintf(drive_root, sizeof(drive_root), "%c:%c", drive->letter, NCD_PATH_SEP[0]);
        }
#else
        snprintf(drive_root, sizeof(drive_root), "%s", drive->label);
#endif
        
        if (_strnicmp(search_path, drive_root, strlen(drive_root)) == 0) {
            found_drive_idx = d;
            found_dir_idx = path_map_get(&path_map, search_path);
            /* If search path is exactly the drive root, use sentinel */
            if (found_dir_idx < 0 && _stricmp(search_path, drive_root) == 0) {
                found_dir_idx = -2;
            }
            if (found_dir_idx >= 0 || found_dir_idx == -2)
                break;
        }
    }
    
    path_map_free(&path_map);
    
    if (found_dir_idx < 0 && found_dir_idx != -2) {
        if (opts->agent_json) {
            agent_print("{\"error\":\"path not found in database\"}\r\n");
        } else {
            agent_print("Path not found in database: ");
            agent_print(search_path);
            agent_print("\r\n");
        }
        return 0;
    }
    
    /* Build children index once for O(1) child access */
    DirChildren *children_idx = build_children_index(&db->drives[found_drive_idx]);
    
    /* For drive root searches, build a virtual root children list */
    DriveData *drive = &db->drives[found_drive_idx];
    int *root_children = NULL;
    int root_child_count = 0;
    if (found_dir_idx == -2) {
        for (int i = 0; i < drive->dir_count; i++) {
            if (drive->dirs[i].parent == -1)
                root_child_count++;
        }
        if (root_child_count > 0) {
            root_children = (int *)malloc(root_child_count * sizeof(int));
            int j = 0;
            for (int i = 0; i < drive->dir_count; i++) {
                if (drive->dirs[i].parent == -1)
                    root_children[j++] = i;
            }
        }
    }
    
    /* Collect entries */
    typedef struct {
        char full_path[NCD_MAX_PATH];
        int depth;
    } TreeEntry;
    
    TreeEntry *entries = NULL;
    int entry_count = 0;
    int entry_capacity = 0;
    
    /* BFS to find all descendants using children index */
    typedef struct { int dir; int depth; int parent_entry_idx; } QueueItem;
    int max_queue_size = db->drives[found_drive_idx].dir_count + 1;
    QueueItem *queue = (QueueItem *)malloc(max_queue_size * sizeof(QueueItem));
    if (!queue) {
        free_children_index(children_idx, db->drives[found_drive_idx].dir_count);
        return 0;
    }
    
    int qhead = 0, qtail = 0;
    queue[qtail++] = (QueueItem){found_dir_idx, 0, -1};
    
    while (qhead < qtail) {
        QueueItem cur = queue[qhead++];
        DirChildren *dc;
        if (cur.dir == -2) {
            DirChildren virtual_root = { root_children, root_child_count };
            dc = &virtual_root;
        } else {
            dc = &children_idx[cur.dir];
        }
        
        /* Iterate children directly using index - O(child_count) instead of O(n) */
        for (int i = 0; i < dc->child_count; i++) {
            int child_idx = dc->children[i];
            const char *name = drive->name_pool + drive->dirs[child_idx].name_off;
            
            if (entry_count >= entry_capacity) {
                entry_capacity = entry_capacity ? entry_capacity * 2 : 64;
                TreeEntry *new_entries = (TreeEntry *)realloc(entries, entry_capacity * sizeof(TreeEntry));
                if (!new_entries) break;
                entries = new_entries;
            }
            
            TreeEntry *e = &entries[entry_count];
            e->depth = cur.depth;
            
            /* Build full path efficiently */
            if (cur.parent_entry_idx < 0) {
                /* Direct child of root */
                int len = (int)strlen(search_path);
                memcpy(e->full_path, search_path, len + 1);
                
                if (len > 0 && e->full_path[len - 1] != NCD_PATH_SEP[0]) {
                    e->full_path[len] = NCD_PATH_SEP[0];
                    e->full_path[len + 1] = '\0';
                    len++;
                }
                
                int name_len = (int)strlen(name);
                if (len + name_len < (int)sizeof(e->full_path) - 1) {
                    memcpy(e->full_path + len, name, name_len + 1);
                }
            } else {
                /* Child of another entry - copy parent's path */
                TreeEntry *parent = &entries[cur.parent_entry_idx];
                int len = (int)strlen(parent->full_path);
                memcpy(e->full_path, parent->full_path, len + 1);
                
                if (len > 0 && e->full_path[len - 1] != NCD_PATH_SEP[0]) {
                    e->full_path[len] = NCD_PATH_SEP[0];
                    e->full_path[len + 1] = '\0';
                    len++;
                }
                
                int name_len = (int)strlen(name);
                if (len + name_len < (int)sizeof(e->full_path) - 1) {
                    memcpy(e->full_path + len, name, name_len + 1);
                }
            }
            
            int current_idx = entry_count++;
            
            if (cur.depth + 1 < opts->agent_depth) {
                queue[qtail++] = (QueueItem){child_idx, cur.depth + 1, current_idx};
            }
        }
    }
    
    free(queue);
    free_children_index(children_idx, drive->dir_count);
    free(root_children);
    
    if (opts->agent_json) {
        agent_print("{\"v\":1,\"tree\":");
        /* JSON array output. With --flat, emit full relative paths in "n". */
        agent_print("[");
        for (int i = 0; i < entry_count; i++) {
            if (i > 0) agent_print(",");
            agent_print("{\"n\":\"");
            if (opts->agent_flat) {
                /* Flat JSON format: full relative path from search root */
                size_t search_len = strlen(search_path);
                const char *rel_path = entries[i].full_path;
                if (strncmp(entries[i].full_path, search_path, search_len) == 0) {
                    rel_path = entries[i].full_path + search_len;
                    if (*rel_path == NCD_PATH_SEP[0]) rel_path++;
                    if (*rel_path == '\0') {
                        const char *name_only = strrchr(entries[i].full_path, NCD_PATH_SEP[0]);
                        rel_path = name_only ? (name_only + 1) : entries[i].full_path;
                    }
                }
                agent_json_escape(rel_path);
            } else {
                /* Default JSON format: leaf name only */
                const char *name_only = strrchr(entries[i].full_path, NCD_PATH_SEP[0]);
                if (name_only) name_only++;
                else name_only = entries[i].full_path;
                agent_json_escape(name_only);
            }
            agent_print("\",\"d\":");
            char dstr[16];
            snprintf(dstr, sizeof(dstr), "%d", entries[i].depth);
            agent_print(dstr);
            agent_print("}");
        }
        agent_print("]}\r\n");
    } else if (opts->agent_flat) {
        /* Flat format: show full relative path from search root */
        for (int i = 0; i < entry_count; i++) {
            /* Calculate relative path from search root */
            size_t search_len = strlen(search_path);
            const char *rel_path = entries[i].full_path;
            /* Skip past the search path prefix if present */
            if (strncmp(entries[i].full_path, search_path, search_len) == 0) {
                rel_path = entries[i].full_path + search_len;
                /* Skip leading separator if present */
                if (*rel_path == NCD_PATH_SEP[0]) rel_path++;
                /* If nothing left, use the entry name */
                if (*rel_path == '\0') {
                    const char *name_only = strrchr(entries[i].full_path, NCD_PATH_SEP[0]);
                    if (name_only) rel_path = name_only + 1;
                    else rel_path = entries[i].full_path;
                }
            }
            agent_print(rel_path);
            agent_print("\r\n");
        }
    } else {
        /* Indented tree format: show just the name with indentation */
        for (int i = 0; i < entry_count; i++) {
            for (int j = 0; j < entries[i].depth; j++)
                agent_print("  ");
            const char *name_only = strrchr(entries[i].full_path, NCD_PATH_SEP[0]);
            if (name_only) name_only++;
            else name_only = entries[i].full_path;
            agent_print(name_only);
            agent_print("\r\n");
        }
    }
    
    free(entries);
    return entry_count;
}

/* Agent mode: check - Check existence and status */
static int agent_mode_check(NcdDatabase *db, const NcdOptions *opts)
{
    bool found = false;
    
    if (opts->has_search && opts->search[0]) {
        /* Check if path exists in filesystem */
        found = platform_dir_exists(opts->search);
        
        /* Check if path is in database - use path map for O(1) lookup */
        bool in_db = false;
        if (db) {
            char search_path[NCD_MAX_PATH];
            platform_strncpy_s(search_path, sizeof(search_path), opts->search);
#if NCD_PLATFORM_WINDOWS
            for (char *p = search_path; *p; p++)
                if (*p == '/') *p = NCD_PATH_SEP_CHAR;
#endif
#if NCD_PLATFORM_WINDOWS
            /* Ensure drive letters have a trailing backslash (R: -> R:\) */
            size_t sp_len = strlen(search_path);
            if (sp_len == 2 && search_path[1] == ':') {
                search_path[2] = NCD_PATH_SEP_CHAR;
                search_path[3] = '\0';
                sp_len = 3;
            }
            /* Strip trailing dot after backslash (e.g., "T:\\." -> "T:\\") to work around batch quoting */
            if (sp_len >= 2 && search_path[sp_len - 1] == '.' && search_path[sp_len - 2] == NCD_PATH_SEP_CHAR) {
                search_path[sp_len - 1] = '\0';
            }
#endif
            
            /* Build path map once for O(1) lookup instead of O(n²) scan */
            PathMap path_map;
            int total_dirs = 0;
            for (int d = 0; d < db->drive_count; d++)
                total_dirs += db->drives[d].dir_count;
            path_map_init(&path_map, total_dirs);
            
            for (int d = 0; d < db->drive_count; d++) {
                build_path_map(&path_map, &db->drives[d], db->drives[d].letter);
            }
            
            in_db = path_map_get(&path_map, search_path) >= 0;
            path_map_free(&path_map);
        }
        
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"path\":\"");
            agent_json_escape(opts->search);
            agent_print("\",\"exists\":");
            agent_print(found ? "true" : "false");
            agent_print(",\"in_db\":");
            agent_print(in_db ? "true" : "false");
            agent_print("}\r\n");
        } else {
            agent_print(found ? "EXISTS" : "NOT_FOUND");
            agent_print("\r\n");
        }
        return found ? 0 : 1;
    }
    
    if (opts->agent_check_db_age) {
        if (!db) {
            agent_print("{\"error\":\"no database\"}\r\n");
            return 1;
        }
        time_t age = time(NULL) - db->last_scan;
        if (opts->agent_json) {
            agent_printf("{\"v\":1,\"db_age\":%lld}\r\n", (long long)age);
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%lld\r\n", (long long)age);
            agent_print(buf);
        }
        return 0;
    }
    
    if (opts->agent_check_stats) {
        if (!db) {
            agent_print("{\"error\":\"no database\"}\r\n");
            return 1;
        }
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"drives\":[");
            for (int i = 0; i < db->drive_count; i++) {
                if (i > 0) agent_print(",");
                agent_printf("{\"letter\":\"%c\",\"count\":%d}",
                    db->drives[i].letter, db->drives[i].dir_count);
            }
            agent_print("]}\r\n");
        } else {
            for (int i = 0; i < db->drive_count; i++) {
                if (i > 0) agent_print(" ");
                agent_printf("%c:%d", db->drives[i].letter, db->drives[i].dir_count);
            }
            agent_print("\r\n");
        }
        return 0;
    }
    
    
    if (opts->agent_check_service_status) {
        /* Check service status with retry — on Windows the named pipe may
         * briefly not exist between server accept cycles. */
        bool service_running = ipc_service_exists();
        if (!service_running) {
            /* Retry once after a short delay to ride out the accept gap */
            platform_sleep_ms(50);
            service_running = ipc_service_exists();
        }

        if (!service_running) {
            /* Service not running */
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"status\":\"not_running\",\"message\":\"Service not running\"}\r\n");
            } else {
                agent_print("NOT_RUNNING\r\n");
            }
            return 0;
        }
        
        /* Service is running - get detailed status */
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            /* Could not connect despite service existing */
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"status\":\"starting\",\"message\":\"Service running but not loaded\"}\r\n");
            } else {
                agent_print("STARTING\r\n");
            }
            return 0;
        }
        
        NcdIpcDetailedStatus info;
        NcdIpcResult result = ipc_client_get_detailed_status(client, &info);
        ipc_client_disconnect(client);
        
        if (result != NCD_IPC_OK) {
            /* Could not get detailed status */
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"status\":\"starting\",\"message\":\"Service running but not loaded\"}\r\n");
            } else {
                agent_print("STARTING\r\n");
            }
            return 0;
        }
        
        const char *state_str = "UNKNOWN";
        switch (info.runtime_state) {
            case 0: state_str = "STOPPED"; break;
            case 1: state_str = "STARTING"; break;
            case 2: state_str = "LOADING"; break;
            case 3: state_str = "READY"; break;
            case 4: state_str = "SCANNING"; break;
        }
        
        if (opts->agent_json) {
            agent_printf("{\"v\":1,\"status\":\"%s\",\"state\":%u,\"log_level\":%d,\"pending_count\":%u,\"dirty_flags\":%u,",
                         state_str, info.runtime_state, info.log_level,
                         info.pending_count, info.dirty_flags);
            agent_printf("\"version\":\"%s\",\"build\":\"%s\",",
                         info.app_version, info.build_stamp);
            agent_printf("\"meta_generation\":%llu,\"db_generation\":%llu,",
                         (unsigned long long)info.meta_generation,
                         (unsigned long long)info.db_generation);
            agent_printf("\"meta_path\":\"%s\",\"log_path\":\"%s\",",
                         info.meta_path, info.log_path);
            agent_printf("\"drives\":[");
            for (uint32_t i = 0; i < info.drive_count; i++) {
                if (i > 0) agent_print(",");
                agent_printf("{\"letter\":\"%c\",\"dir_count\":%u,\"db_path\":\"%s\"}",
                             info.drives[i].letter,
                             info.drives[i].dir_count,
                             info.drives[i].db_path);
            }
            agent_print("]}\r\n");
        } else {
            agent_printf("State: %s\r\n", state_str);
            if (info.status_message[0]) {
                agent_printf("Message: %s\r\n", info.status_message);
            }
            if (info.app_version[0]) {
                agent_printf("Version: %s\r\n", info.app_version);
            }
            if (info.build_stamp[0]) {
                agent_printf("Build: %s\r\n", info.build_stamp);
            }
            agent_printf("Log level: %d\r\n", info.log_level);
            if (info.pending_count > 0) {
                agent_printf("Pending requests: %u\r\n", info.pending_count);
            }
            if (info.dirty_flags != 0) {
                agent_printf("Dirty flags: 0x%08X\r\n", info.dirty_flags);
            }
            agent_printf("Meta generation: %llu\r\n", (unsigned long long)info.meta_generation);
            agent_printf("DB generation: %llu\r\n", (unsigned long long)info.db_generation);
            if (info.meta_path[0]) {
                agent_printf("Metadata file: %s\r\n", info.meta_path);
            }
            if (info.log_path[0]) {
                agent_printf("Log file: %s\r\n", info.log_path);
            }
            if (info.drive_count > 0) {
                agent_printf("Cached drives (%u):\r\n", info.drive_count);
                for (uint32_t i = 0; i < info.drive_count; i++) {
                    if (info.drives[i].db_path[0]) {
                        agent_printf("  %c: (%u dirs) -> %s\r\n",
                                     info.drives[i].letter,
                                     info.drives[i].dir_count,
                                     info.drives[i].db_path);
                    } else {
                        agent_printf("  %c: (%u dirs)\r\n",
                                     info.drives[i].letter,
                                     info.drives[i].dir_count);
                    }
                }
            } else {
                agent_print("Cached drives: none\r\n");
            }
        }
        return 0;
    }
    
    return 1;
}

/* Agent mkdir result codes */
typedef enum {
    AGENT_MKDIR_OK,
    AGENT_MKDIR_EXISTS,
    AGENT_MKDIR_ERROR_PATH,
    AGENT_MKDIR_ERROR_PARENT,
    AGENT_MKDIR_ERROR_PERMS,
    AGENT_MKDIR_ERROR_OTHER
} AgentMkdirResult;

/*
 * Helper: Add a single directory path to the database.
 * Loads/creates database for the drive if needed, finds parent, adds directory.
 * Returns true if directory was added (or already exists), false on error.
 * Marks database dirty if modified.
 */
static bool add_path_to_database(const char *path)
{
    if (!path || !path[0]) return false;
    
    char target_drive = path_get_drive(path);
    if (target_drive == 0 && path[0] != '\0') {
        target_drive = (char)toupper((unsigned char)path[0]);
    }
    
    if (target_drive == 0) return false;
    
    /* Load existing database for this drive */
    char target_db[NCD_MAX_PATH] = {0};
    NcdDatabase *db = NULL;
    
    if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
        /* Check if we already have this database in dirty tracking */
        for (int i = 0; i < g_dirty_db_count; i++) {
            if (g_dirty_dbs[i].drive == target_drive && g_dirty_dbs[i].db) {
                db = g_dirty_dbs[i].db;
                break;
            }
        }
        if (!db) {
            db = db_load_auto(target_db);
        }
    }
    
    if (!db) {
        db = db_create();
        db->last_scan = time(NULL);
    }
    
    /* Find or add the drive */
    DriveData *drv = db_find_drive(db, target_drive);
    if (!drv) {
        drv = db_add_drive(db, target_drive);
    }
    
    if (!drv) {
        db_free(db);
        return false;
    }
    
    /* Normalize path separator for the platform */
    char norm_path[NCD_MAX_PATH];
    platform_strncpy_s(norm_path, sizeof(norm_path), path);
    path_normalize_separators(norm_path);
    
    /* Find parent directory in database using path map */
    char parent_path[NCD_MAX_PATH];
    const char *leaf_name = path_leaf(norm_path);
    int32_t parent_idx = -1;
    
    /* Build path map for O(1) lookups */
    PathMap path_map;
    path_map_init(&path_map, drv->dir_count);
    build_path_map(&path_map, drv, drv->letter);
    
    if (path_parent(norm_path, parent_path, sizeof(parent_path))) {
        parent_idx = path_map_get(&path_map, parent_path);
    }
    
    /* Check if directory already exists in database - O(1) lookup */
    bool in_db = path_map_get(&path_map, norm_path) >= 0;
    
    path_map_free(&path_map);
    
    if (!in_db && leaf_name) {
        /* Add to database */
        db_add_dir(drv, leaf_name, parent_idx, false, false);
        
        /* Mark database as dirty for deferred save */
        char drv_path[NCD_MAX_PATH];
        if (db_drive_path(target_drive, drv_path, sizeof(drv_path))) {
            db_mark_dirty_standalone(target_drive, drv_path, db);
        }
    } else {
        /* No changes made, free the database if not tracked */
        bool tracked = false;
        for (int i = 0; i < g_dirty_db_count; i++) {
            if (g_dirty_dbs[i].drive == target_drive && g_dirty_dbs[i].db == db) {
                tracked = true;
                break;
            }
        }
        if (!tracked) {
            db_free(db);
        }
    }
    
    return true;
}

/* Helper: create all parent directories for a path */
static bool mkdir_create_parents(const char *path)
{
    char parent[NCD_MAX_PATH];
    if (!path_parent(path, parent, sizeof(parent)))
        return false;
    if (platform_dir_exists(parent))
        return true;
    if (!mkdir_create_parents(parent))
        return false;
    return platform_create_dir(parent);
}

/* Agent mode: mkdir - Create a directory and add to database */
static int agent_mode_mkdir(const NcdOptions *opts)
{
    if (!opts->has_search || !opts->search[0]) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"error\":\"no path specified\",\"result\":\"error\"}\r\n");
        } else {
            agent_print("ERROR: No path specified\r\n");
        }
        return 1;
    }
    
    const char *path = opts->search;
    AgentMkdirResult result = AGENT_MKDIR_ERROR_OTHER;
    char result_msg[256] = {0};
    
    /* Check if path already exists */
    if (platform_dir_exists(path)) {
        result = AGENT_MKDIR_EXISTS;
        platform_strncpy_s(result_msg, sizeof(result_msg), "Directory already exists");
    } else {
        /* Check if parent directory exists */
        char parent_path[NCD_MAX_PATH];
        if (!path_parent(path, parent_path, sizeof(parent_path))) {
            result = AGENT_MKDIR_ERROR_PATH;
            platform_strncpy_s(result_msg, sizeof(result_msg), "Invalid path");
        } else if (!platform_dir_exists(parent_path)) {
            if (mkdir_create_parents(parent_path)) {
                /* Parents created, proceed to create target */
            } else {
                result = AGENT_MKDIR_ERROR_PARENT;
                platform_strncpy_s(result_msg, sizeof(result_msg), "Parent directory does not exist");
                goto mkdir_done;
            }
        }
        /* Try to create the directory */
        if (platform_create_dir(path)) {
            result = AGENT_MKDIR_OK;
            platform_strncpy_s(result_msg, sizeof(result_msg), "Directory created successfully");
        } else {
            /* Determine specific error based on errno or GetLastError */
#if NCD_PLATFORM_WINDOWS
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED) {
                result = AGENT_MKDIR_ERROR_PERMS;
                platform_strncpy_s(result_msg, sizeof(result_msg), "Permission denied");
            } else {
                result = AGENT_MKDIR_ERROR_OTHER;
                platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to create directory");
            }
#else
            if (errno == EACCES || errno == EPERM) {
                result = AGENT_MKDIR_ERROR_PERMS;
                platform_strncpy_s(result_msg, sizeof(result_msg), "Permission denied");
            } else {
                result = AGENT_MKDIR_ERROR_OTHER;
                platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to create directory");
            }
#endif
        }
    }
    
mkdir_done:
    /* If successful, add to database */
    if (result == AGENT_MKDIR_OK || result == AGENT_MKDIR_EXISTS) {
        add_path_to_database(path);
    }
    
    /* Output result */
    if (opts->agent_json) {
        const char *result_str;
        switch (result) {
            case AGENT_MKDIR_OK:      result_str = "created"; break;
            case AGENT_MKDIR_EXISTS:  result_str = "exists"; break;
            case AGENT_MKDIR_ERROR_PERMS:  result_str = "error_perms"; break;
            case AGENT_MKDIR_ERROR_PATH:   result_str = "error_path"; break;
            case AGENT_MKDIR_ERROR_PARENT: result_str = "error_parent"; break;
            default:                  result_str = "error"; break;
        }
        agent_print("{\"v\":1,\"path\":\"");
        agent_json_escape(path);
        agent_printf("\",\"result\":\"%s\",\"message\":\"", result_str);
        agent_json_escape(result_msg);
        agent_print("\"}\r\n");
    } else {
        agent_print(result_msg);
        agent_print("\r\n");
    }
    
    return (result == AGENT_MKDIR_OK || result == AGENT_MKDIR_EXISTS) ? 0 : 1;
}

/* Agent mkdirs result codes */
typedef enum {
    AGENT_MKDIRS_OK,
    AGENT_MKDIRS_ERROR_PATH,
    AGENT_MKDIRS_ERROR_PARENT,
    AGENT_MKDIRS_ERROR_PERMS,
    AGENT_MKDIRS_ERROR_PARSE,
    AGENT_MKDIRS_ERROR_FILE,
    AGENT_MKDIRS_ERROR_OTHER
} AgentMkdirsResult;

/* Structure to hold a directory tree node */
typedef struct MkdirsNode {
    char name[NCD_MAX_NAME];
    struct MkdirsNode *children;
    int child_count;
    int child_capacity;
} MkdirsNode;

/* Free a directory tree */
static void mkdirs_free_tree(MkdirsNode *node)
{
    if (!node) return;
    for (int i = 0; i < node->child_count; i++) {
        mkdirs_free_tree(&node->children[i]);
    }
    free(node->children);
}

/* Add a child to a node */
static MkdirsNode* mkdirs_add_child(MkdirsNode *parent, const char *name)
{
    if (!parent) return NULL;
    
    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity ? parent->child_capacity * 2 : 4;
        MkdirsNode *new_children = (MkdirsNode*)realloc(parent->children, new_cap * sizeof(MkdirsNode));
        if (!new_children) return NULL;
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }
    
    MkdirsNode *child = &parent->children[parent->child_count++];
    memset(child, 0, sizeof(MkdirsNode));
    platform_strncpy_s(child->name, sizeof(child->name), name);
    return child;
}

/* Skip whitespace in JSON */
static const char* json_skip_ws(const char *p)
{
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r'))
        p++;
    return p;
}

/* Parse a JSON string value (assumes starting quote already consumed) */
static const char* json_parse_string(const char *p, char *out, size_t out_size)
{
    size_t i = 0;
    while (*p && *p != '"' && i < out_size - 1) {
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                case '\\': out[i++] = '\\'; break;
                case '"': out[i++] = '"'; break;
                default: out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/* Parse JSON array of directory names or objects with 'name' and 'children' */
static const char* json_parse_dirs(const char *p, MkdirsNode *parent)
{
    p = json_skip_ws(p);
    if (*p != '[') return NULL;
    p++;
    
    while (1) {
        p = json_skip_ws(p);
        if (*p == ']') {
            p++;
            break;
        }
        
        if (*p == '"') {
            /* Simple string: "dirname" */
            p++;
            char name[NCD_MAX_NAME];
            p = json_parse_string(p, name, sizeof(name));
            if (!p) return NULL;
            mkdirs_add_child(parent, name);
        } else if (*p == '{') {
            /* Object: {"name":"dirname", "children":[...]} */
            p++;
            char name[NCD_MAX_NAME] = {0};
            MkdirsNode *new_node = NULL;
            
            while (1) {
                p = json_skip_ws(p);
                if (*p == '}') {
                    p++;
                    break;
                }
                
                if (*p == '"') {
                    p++;
                    char key[NCD_MAX_NAME];
                    p = json_parse_string(p, key, sizeof(key));
                    if (!p) return NULL;
                    p = json_skip_ws(p);
                    if (*p == ':') {
                        p++;
                        p = json_skip_ws(p);
                        
                        if (strcmp(key, "name") == 0 && *p == '"') {
                            p++;
                            p = json_parse_string(p, name, sizeof(name));
                            if (!p) return NULL;
                            new_node = mkdirs_add_child(parent, name);
                        } else if (strcmp(key, "children") == 0 && *p == '[') {
                            if (!new_node) new_node = mkdirs_add_child(parent, name);
                            p = json_parse_dirs(p, new_node);
                            if (!p) return NULL;
                        } else {
                            /* Skip unknown value */
                            if (*p == '"') {
                                p++;
                                char skip[NCD_MAX_NAME];
                                p = json_parse_string(p, skip, sizeof(skip));
                            } else {
                                while (*p && *p != ',' && *p != '}') p++;
                            }
                        }
                    }
                }
                
                p = json_skip_ws(p);
                if (*p == ',') {
                    p++;
                    continue;
                }
            }
        }
        
        p = json_skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
    }
    
    return p;
}

/* Parse flat file format with 2-space indentation */
static bool parse_flat_format(const char *content, MkdirsNode *root)
{
    const char *p = content;
    MkdirsNode *stack[64];
    int stack_depth = 0;
    stack[0] = root;
    
    char line[NCD_MAX_PATH];
    int line_num = 0;
    
    while (*p) {
        line_num++;
        
        /* Read one line */
        int line_len = 0;
        while (*p && *p != '\n' && line_len < (int)sizeof(line) - 1) {
            line[line_len++] = *p++;
        }
        line[line_len] = '\0';
        if (*p == '\n') p++;
        
        /* Skip empty lines */
        char *line_p = line;
        while (*line_p == ' ' || *line_p == '\t') line_p++;
        if (*line_p == '\0' || *line_p == '\r') continue;
        
        /* Remove trailing whitespace */
        char *end = line_p + strlen(line_p) - 1;
        while (end > line_p && (*end == ' ' || *end == '\t' || *end == '\r'))
            *end-- = '\0';
        
        /* Count leading spaces for depth */
        int spaces = 0;
        const char *orig_line = line;
        while (*orig_line == ' ') {
            spaces++;
            orig_line++;
        }
        int depth = spaces / 2;
        
        /* Validate depth */
        if (depth > stack_depth + 1) {
            /* Indentation too deep - invalid */
            return false;
        }
        
        /* Adjust stack */
        stack_depth = depth;
        
        /* Add directory to parent */
        MkdirsNode *parent = stack[depth];
        MkdirsNode *new_node = mkdirs_add_child(parent, orig_line);
        if (!new_node) return false;
        
        /* This node could be a parent for next level */
        if (stack_depth + 1 < 64) {
            stack[stack_depth + 1] = new_node;
        }
    }
    
    return true;
}

/* Create directories recursively from tree */
static int mkdirs_create_recursive(const char *base_path, MkdirsNode *node, 
                                    AgentMkdirsResult *results, int *result_count,
                                    int max_results, bool json_output, bool update_db)
{
    int created = 0;
    char path[NCD_MAX_PATH];
    
    for (int i = 0; i < node->child_count && *result_count < max_results; i++) {
        MkdirsNode *child = &node->children[i];
        
        /* Build full path */
        if (base_path[0]) {
            snprintf(path, sizeof(path), "%s%s%s", base_path, NCD_PATH_SEP, child->name);
        } else {
            platform_strncpy_s(path, sizeof(path), child->name);
        }
        
        /* Try to create directory */
        AgentMkdirsResult result = AGENT_MKDIRS_OK;
        char result_msg[256] = {0};
        
        if (platform_dir_exists(path)) {
            result = AGENT_MKDIRS_OK;
            platform_strncpy_s(result_msg, sizeof(result_msg), "Directory already exists");
        } else {
            /* Create parent if needed */
            char parent_path[NCD_MAX_PATH];
            if (path_parent(path, parent_path, sizeof(parent_path))) {
                if (!platform_dir_exists(parent_path)) {
                    if (!platform_create_dir(parent_path)) {
                        result = AGENT_MKDIRS_ERROR_PARENT;
                        platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to create parent directory");
                    }
                }
            }
            
            if (result == AGENT_MKDIRS_OK) {
                if (platform_create_dir(path)) {
                    result = AGENT_MKDIRS_OK;
                    platform_strncpy_s(result_msg, sizeof(result_msg), "Directory created");
                    created++;
                } else {
#if NCD_PLATFORM_WINDOWS
                    DWORD err = GetLastError();
                    if (err == ERROR_ACCESS_DENIED) {
                        result = AGENT_MKDIRS_ERROR_PERMS;
                        platform_strncpy_s(result_msg, sizeof(result_msg), "Permission denied");
                    } else {
                        result = AGENT_MKDIRS_ERROR_OTHER;
                        platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to create directory");
                    }
#else
                    if (errno == EACCES || errno == EPERM) {
                        result = AGENT_MKDIRS_ERROR_PERMS;
                        platform_strncpy_s(result_msg, sizeof(result_msg), "Permission denied");
                    } else {
                        result = AGENT_MKDIRS_ERROR_OTHER;
                        platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to create directory");
                    }
#endif
                }
            }
        }
        
        /* Record result */
        results[*result_count] = result;
        (*result_count)++;
        
        /* Add to database if requested and operation was successful or dir exists */
        if (update_db && (result == AGENT_MKDIRS_OK)) {
            add_path_to_database(path);
        }
        
        /* Output result */
        if (json_output) {
            const char *result_str;
            switch (result) {
                case AGENT_MKDIRS_OK: result_str = "created"; break;
                case AGENT_MKDIRS_ERROR_PERMS: result_str = "error_perms"; break;
                case AGENT_MKDIRS_ERROR_PARENT: result_str = "error_parent"; break;
                case AGENT_MKDIRS_ERROR_PATH: result_str = "error_path"; break;
                default: result_str = "error"; break;
            }
            if (*result_count == 1) {
                agent_print("{\"v\":1,\"dirs\":[");
            } else {
                agent_print(",");
            }
            agent_print("{\"path\":\"");
            agent_json_escape(path);
            agent_printf("\",\"result\":\"%s\",\"message\":\"", result_str);
            agent_json_escape(result_msg);
            agent_print("\"}");
        } else {
            agent_print(path);
            agent_print(": ");
            agent_print(result_msg);
            agent_print("\r\n");
        }
        
        /* Recurse into children */
        if (child->child_count > 0) {
            created += mkdirs_create_recursive(path, child, results, result_count, max_results, json_output, update_db);
        }
    }
    
    return created;
}

/* Read entire file into memory */
static char* read_file_contents(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size < 0 || size > 1024 * 1024) { /* Max 1MB */
        fclose(f);
        return NULL;
    }
    
    char *buf = (char*)malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    
    size_t read = fread(buf, 1, size, f);
    fclose(f);
    
    buf[read] = '\0';
    return buf;
}

/* Agent mode: mkdirs - Create directory tree from JSON or flat file */
static int agent_mode_mkdirs(const NcdOptions *opts)
{
    const char *content = NULL;
    char *file_content = NULL;
    bool is_json = false;
    
    /* Determine input source */
    if (opts->agent_mkdirs_file[0]) {
        /* Read from file */
        file_content = read_file_contents(opts->agent_mkdirs_file);
        if (!file_content) {
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"error\":\"failed to read file\",\"result\":\"error\"}\r\n");
            } else {
                agent_print("ERROR: Failed to read file\r\n");
            }
            return 1;
        }
        content = file_content;
        /* Detect JSON by checking first non-whitespace char */
        const char *p = content;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        is_json = (*p == '[' || *p == '{');
    } else if (opts->has_search) {
        /* Content from command line */
        content = opts->search;
        /* Detect JSON */
        const char *p = content;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        is_json = (*p == '[' || *p == '{');
    } else {
        /* Try to read from stdin */
        /* For now, error out */
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"error\":\"no input provided (use --file or provide content)\",\"result\":\"error\"}\r\n");
        } else {
            agent_print("ERROR: No input provided. Use --file or provide content as argument.\r\n");
            agent_print("Usage: ncd --agent:mkdirs [--file <path>] [--json] <content>\r\n");
            agent_print("\r\n");
            agent_print("Flat file format (2 spaces = child):\r\n");
            agent_print("  parent\r\n");
            agent_print("    child1\r\n");
            agent_print("    child2\r\n");
            agent_print("      grandchild\r\n");
            agent_print("\r\n");
            agent_print("JSON format:\r\n");
            agent_print("  [{\"name\":\"parent\",\"children\":[{\"name\":\"child\"}]}]\r\n");
        }
        return 1;
    }
    
    /* Parse the directory tree */
    MkdirsNode root;
    memset(&root, 0, sizeof(root));
    platform_strncpy_s(root.name, sizeof(root.name), "");
    
    bool parse_ok;
    if (is_json) {
        const char *p = json_skip_ws(content);
        parse_ok = (json_parse_dirs(p, &root) != NULL);
    } else {
        parse_ok = parse_flat_format(content, &root);
    }
    
    if (file_content) {
        free(file_content);
    }
    
    if (!parse_ok) {
        mkdirs_free_tree(&root);
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"error\":\"failed to parse input\",\"result\":\"error\"}\r\n");
        } else {
            agent_print("ERROR: Failed to parse input\r\n");
        }
        return 1;
    }
    
    /* Create directories */
    AgentMkdirsResult results[256];
    int result_count = 0;
    
    if (opts->agent_json) {
        /* JSON output - mkdirs_create_recursive outputs as it goes */
        (void)mkdirs_create_recursive("", &root, results, &result_count, 256, true, true);
        if (result_count > 0) {
            agent_print("]}\r\n");
        } else {
            agent_print("{\"v\":1,\"dirs\":[]}\r\n");
        }
        mkdirs_free_tree(&root);
        return 0;
    } else {
        /* Plain text output */
        agent_print("Creating directory tree...\r\n\r\n");
        (void)mkdirs_create_recursive("", &root, results, &result_count, 256, false, true);
        mkdirs_free_tree(&root);
        
        agent_printf("\r\nCreated %d directories\r\n", result_count);
        return 0;
    }
}

/* Agent mode: complete - Shell tab-completion candidates */
static int agent_mode_complete(NcdDatabase *db, const NcdOptions *opts)
{
    const char *partial = opts->search;
    int limit = opts->agent_limit > 0 ? opts->agent_limit : 20;
    int printed = 0;

    /* Track printed names for deduplication - using fixed-size array for simplicity */
    /* Max 256 entries for dedup tracking */
    #define MAX_PRINTED 256
    char printed_names[MAX_PRINTED][NCD_MAX_NAME];
    int printed_count = 0;

    NcdMetadata *meta = db_metadata_load();

    /* Helper macro: add name to printed list */
    #define RECORD_PRINTED(name) \
        do { \
            if (printed_count < MAX_PRINTED) { \
                platform_strncpy_s(printed_names[printed_count], NCD_MAX_NAME, (name)); \
                printed_count++; \
            } \
        } while(0)

    /* Group name completion (@prefix) */
    if (partial[0] == '@') {
        if (meta) {
            for (int i = 0; i < meta->groups.count && printed < limit; i++) {
                if (_strnicmp(meta->groups.groups[i].name, partial, strlen(partial)) == 0) {
                    agent_printf("%s\r\n", meta->groups.groups[i].name);
                    printed++;
                }
            }
        }
        if (meta) db_metadata_free(meta);
        return 0;
    }

    /* History matches (sorted first - higher signal) */
    if (meta) {
        int hist_count = db_dir_history_count(meta);
        for (int i = 0; i < hist_count && printed < limit; i++) {
            const NcdDirHistoryEntry *e = db_dir_history_get(meta, i);
            if (!e) continue;
            const char *name = path_leaf(e->path);
            if (!name) name = e->path;
            if (_strnicmp(name, partial, strlen(partial)) == 0) {
                bool found = false;
                for (int j = 0; j < printed_count && !found; j++) {
                    if (_stricmp(printed_names[j], name) == 0) found = true;
                }
                if (!found) {
                    agent_printf("%s\r\n", name);
                    RECORD_PRINTED(name);
                    printed++;
                }
            }
        }
    }

    /* Database matches */
    if (printed < limit && db) {
        /* Load all directory names and filter by prefix */
        for (int d = 0; d < db->drive_count && printed < limit; d++) {
            DriveData *drive = &db->drives[d];
            for (int i = 0; i < drive->dir_count && printed < limit; i++) {
                const char *name = drive->name_pool + drive->dirs[i].name_off;
                if (_strnicmp(name, partial, strlen(partial)) == 0) {
                    bool found = false;
                    for (int j = 0; j < printed_count && !found; j++) {
                        if (_stricmp(printed_names[j], name) == 0) found = true;
                    }
                    if (!found) {
                        agent_printf("%s\r\n", name);
                        RECORD_PRINTED(name);
                        printed++;
                    }
                }
            }
        }
    }

    #undef MAX_PRINTED
    #undef IS_ALREADY_PRINTED
    #undef RECORD_PRINTED

    if (meta) db_metadata_free(meta);
    return 0;
}

#if NCD_PLATFORM_LINUX
#endif

#if NCD_PLATFORM_WINDOWS
typedef struct {
    char drive;
} SingleScanProgress;

static void single_scan_progress_cb(char drive_letter, const char *current_path, void *user_data)
{
    (void)drive_letter;
    SingleScanProgress *p = (SingleScanProgress *)user_data;
    if (!p || !current_path) return;
    ncd_printf("\r  [%c:] scanning %s", p->drive, current_path);
}
#endif

static int run_requested_rescan(NcdDatabase *db, const NcdOptions *opts,
                                const NcdExclusionList *exclusions)
{
    /* In test mode, only subdirectory rescans are permitted. */
    if (ncd_test_mode_active() && !opts->scan_subdirectory[0]) {
        ncd_printf("NCD: Test mode active, skipping non-subdirectory rescan.\r\n");
        return 0;
    }

    /* Handle subdirectory rescan: /r. */
    if (opts->scan_subdirectory[0]) {
        char drive = platform_get_drive_letter(opts->scan_subdirectory);
        int n = scan_subdirectory(db, drive, opts->scan_subdirectory, opts->show_hidden, opts->show_system, exclusions);
        ncd_printf("  Scanned %d directories.\r\n", n);
        return n;
    }

    /* Linux special: /r / scans only root filesystem */
    if (opts->scan_root_only) {
        const char *root_mount = "/";
        return scan_mounts(db, &root_mount, 1, opts->show_hidden, opts->show_system, opts->timeout_seconds, exclusions);
    }

    /* Build a drive list from the options. If opts->scan_drive_count>0 use
     * those drives. If skip mask is specified, enumerate available drives
     * and exclude the skipped ones. If neither is set, pass count==0 to
     * scan_mounts to scan all drives. */
    char drives[26];
    int dcount = 0;

    if (opts->scan_drive_count > 0) {
        for (int i = 0; i < 26; i++) 
            if (opts->scan_drive_mask[i] && !opts->skip_drive_mask[i]) 
                drives[dcount++] = (char)('A' + i);
    } else if (opts->skip_drive_count > 0) {
        /* Get available drives and filter by skip mask */
        char all_drives[26];
        int all_count = platform_get_available_drives(all_drives, 26);
        dcount = platform_filter_available_drives(all_drives, all_count, 
                                                   opts->skip_drive_mask,
                                                   drives, 26);
    } else {
        dcount = 0; /* scan_mounts will enumerate all mounts */
    }

    /* If no specific mounts requested, let scan_mounts enumerate them */
    if (dcount == 0) {
        return scan_mounts(db, NULL, 0, opts->show_hidden, opts->show_system, opts->timeout_seconds, exclusions);
    }

    /* Build platform-specific mount strings from the selected drives. */
    const char *mounts[26];
    char mount_bufs[26][MAX_PATH];
    int mcount = 0;

    for (int i = 0; i < dcount; i++) {
        if (platform_build_mount_path(drives[i], mount_bufs[mcount], MAX_PATH)) {
            mounts[mcount] = mount_bufs[mcount];
            mcount++;
        }
    }

    if (mcount == 0) return 0;
    return scan_mounts(db, mounts, mcount, opts->show_hidden, opts->show_system, opts->timeout_seconds, exclusions);
}

/* ================================================================= main   */

int main(int argc, char *argv[])
{
    /* Open a direct console handle as early as possible so all subsequent
     * ncd_print / ncd_printf calls bypass CRT buffering.                  */
    con_init();

    /* ------------------------------------------------------ parse options */
    NcdOptions opts;
    if (!parse_args(argc, argv, &opts)) {
        result_error("Invalid command line.  Run 'ncd -?' for help.");
        con_close();
        return 1;
    }
    
    /* Apply config override if specified */
    if (opts.conf_override[0]) {
        db_metadata_set_override(opts.conf_override);
    }

#if DEBUG
    /* Set debug test flags in database module */
    extern bool g_test_no_checksum;
    extern bool g_test_slow_mode;
    g_test_no_checksum = opts.test_no_checksum;
    g_test_slow_mode = opts.test_slow_mode;
#endif

    /* ------------------------------------------------ agentic debug mode */
    if (opts.agentic_debug) {
        /* Enable IPC debugging */
        extern void ipc_set_debug_mode(int);
        ipc_set_debug_mode(1);
        
        /* Use printf for agentic debug - must work even without console handle */
        printf("=== AGENTIC DEBUG MODE ===\n");
        printf("This mode is for agentic debugging and testing only.\n");
        printf("\n");
        fflush(stdout);
        
        /* Service diagnostics */
        printf("--- Service Diagnostics ---\n");
        fflush(stdout);
        
        /* Check if service exists and is connectable */
        bool service_exists = ipc_service_exists();
        printf("ipc_service_exists(): %s\n", service_exists ? "YES" : "NO");
        
        if (service_exists) {
            NcdIpcClient *client = ipc_client_connect();
            if (client) {
                printf("ipc_client_connect(): SUCCESS\n");
                
                /* Try ping */
                NcdIpcResult ping_result = ipc_client_ping(client);
                printf("ipc_client_ping(): %s\n", ipc_error_string(ping_result));
                
                /* Try version check */
                NcdIpcVersionCheckResult ver;
                NcdIpcResult ver_result = ipc_client_check_version(client, NCD_BUILD_VER, NCD_BUILD_STAMP, &ver);
                printf("ipc_client_check_version(): %s (code %d)\n", ipc_error_string(ver_result), (int)ver_result);
                if (ver_result == NCD_IPC_OK) {
                    printf("  versions_match: %s\n", ver.versions_match ? "YES" : "NO");
                    printf("  service_version: %s\n", ver.service_version);
                    printf("  service_build: %s\n", ver.service_build);
                    printf("  message: %s\n", ver.message);
                }
                
                /* Try state info */
                NcdIpcStateInfo info;
                NcdIpcResult info_result = ipc_client_get_state_info(client, &info);
                printf("ipc_client_get_state_info(): %s (code %d)\n", ipc_error_string(info_result), (int)info_result);
                if (info_result == NCD_IPC_OK) {
                    printf("  protocol_version: %u\n", info.protocol_version);
                    printf("  db_generation: %llu\n", (unsigned long long)info.db_generation);
                    printf("  meta_generation: %llu\n", (unsigned long long)info.meta_generation);
                    printf("  meta_size: %lu\n", (unsigned long)info.meta_size);
                    printf("  db_size: %lu\n", (unsigned long)info.db_size);
                    printf("  meta_name: '%s'\n", info.meta_name);
                    printf("  db_name: '%s'\n", info.db_name);
                    
                    if (info.meta_generation == 0 && info.db_generation == 0) {
                        printf("  WARNING: Generations are 0 - service may still be loading\n");
                    }
                    if (info.meta_name[0] == '\0') {
                        printf("  WARNING: meta_name is empty - metadata snapshot not published\n");
                    }
                    if (info.db_name[0] == '\0') {
                        printf("  WARNING: db_name is empty - database snapshot not published\n");
                    }
                } else {
                    printf("  State info failed - service may be in invalid state\n");
                }
                
                /* Additional diagnostics: check shared memory directly */
                printf("\n--- Shared Memory Diagnostics ---\n");
                ShmHandle *meta_shm = NULL;
                ShmHandle *db_shm = NULL;
                ShmResult meta_shm_result = shm_open_existing(info.meta_name, SHM_ACCESS_READ, &meta_shm);
                ShmResult db_shm_result = shm_open_existing(info.db_name, SHM_ACCESS_READ, &db_shm);
                printf("shm_open_existing(meta_name='%s'): %d (0=OK)\n", info.meta_name, (int)meta_shm_result);
                printf("shm_open_existing(db_name='%s'): %d (0=OK)\n", info.db_name, (int)db_shm_result);
                if (meta_shm) shm_close(meta_shm);
                if (db_shm) shm_close(db_shm);
                
                ipc_client_disconnect(client);
            } else {
                printf("ipc_client_connect(): FAILED\n");
                printf("  (Service exists but we cannot connect - may be starting or dead)\n");
            }
        }
        
        printf("\n");
        printf("--- State Backend Diagnostics ---\n");
        
        /* Test state backend initialization */
        NcdStateView *state_view = NULL;
        NcdStateSourceInfo state_info = {0};
        int state_result = state_backend_open_best_effort(&state_view, &state_info);
        printf("state_backend_open_best_effort(): %d (0=OK)\n", state_result);
        if (state_result == 0 && state_view) {
            printf("  from_service: %s\n", state_info.from_service ? "YES" : "NO");
            printf("  generation: %llu\n", (unsigned long long)state_info.generation);
            printf("  db_generation: %llu\n", (unsigned long long)state_info.db_generation);
            
            const NcdMetadata *meta = state_view_metadata(state_view);
            printf("  metadata: %s\n", meta ? "AVAILABLE" : "NULL");
            
            const NcdDatabase *db = state_view_database(state_view);
            printf("  database: %s\n", db ? "AVAILABLE" : "NULL");
            
            state_backend_close(state_view);
        } else {
            printf("  State backend initialization FAILED\n");
        }
        
        printf("\n");
        printf("=== END AGENTIC DEBUG ===\n");
        fflush(stdout);
        con_close();
        return 0;
    }

    /* ------------------------------------------------ first-run configuration */
    /* If no config file exists and user didn't specify /c, prompt for configuration */
    bool meta_exists = db_metadata_exists();
    if (!meta_exists && !opts.config_edit && !opts.show_help && 
        !opts.group_list && !opts.group_set && !opts.group_remove &&
        !opts.show_history && !opts.clear_history && !opts.show_version &&
        !opts.force_rescan && !opts.has_search &&
        !opts.history_pingpong && opts.history_index == 0 && !opts.history_browse && 
        !opts.history_list && !opts.history_clear && opts.history_remove == 0) {
        ncd_println("Welcome to NCD! Let's set up your default options.");
        ncd_println("(Use 'ncd -c' anytime to change these settings)\r\n");
        
        NcdMetadata *meta = db_metadata_create();
        db_config_init_defaults(&meta->cfg);
        db_exclusion_init_defaults(meta);
        
        if (ui_edit_config(meta)) {
            meta->config_dirty = true;
            if (is_service_backend()) {
                /* Service mode: send update via IPC */
                if (state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_CONFIG,
                                                         &meta->cfg, sizeof(meta->cfg)) == 0) {
                    ncd_println("Configuration saved.\r\n");
                } else {
                    ncd_println("Warning: Could not save configuration via service.\r\n");
                }
            } else {
                /* Standalone mode: save directly */
                if (db_metadata_save(meta)) {
                    ncd_println("Configuration saved.\r\n");
                } else {
                    ncd_println("Warning: Could not save configuration.\r\n");
                }
            }
        } else {
            ncd_println("Using default settings. (Run 'ncd -c' to configure later)\r\n");
        }
        db_metadata_free(meta);
    }
    /* ------------------------------------------------ apply config defaults */
    /* Load configuration and apply defaults for options not explicitly set */
    NcdConfig cfg;
    if (db_config_load(&cfg) && cfg.has_defaults) {
        /* Only apply defaults if user didn't explicitly set these options */
        if (!opts.show_hidden && !opts.show_system) {
            /* /a sets both, so only apply defaults if neither /i nor /s was used */
            opts.show_hidden = cfg.default_show_hidden;
            opts.show_system = cfg.default_show_system;
        }
        if (!opts.fuzzy_match) {
            opts.fuzzy_match = cfg.default_fuzzy_match;
        }
        if (opts.timeout_seconds == 300 && cfg.default_timeout > 0) {
            /* Only override if using the built-in default (300) */
            opts.timeout_seconds = cfg.default_timeout;
        }
    } else {
    }

    /* ------------------------------------------------ register exit handler */
    /* Register cleanup handler to flush dirty databases before exit */
    atexit(flush_all_dirty_dbs);
    
    /* ------------------------------------------------ resolve text encoding */
    /* Resolve and set the active text encoding mode */
    g_text_encoding = resolve_text_encoding(&opts);
    db_set_text_encoding(g_text_encoding);
    
    /* Handle encoding mode switch if requested */
    if (opts.encoding_switch) {
        bool switch_ok = handle_encoding_switch(opts.text_encoding, g_text_encoding);
        con_close();
        return switch_ok ? 0 : 1;
    }
    if (argc == 1) {
        print_usage();
        con_close();
        return 0;
    }
    if (opts.show_help) {
        print_usage();
        con_close();
        return 0;
    }
    if (opts.clear_history) {
        heur_clear();
        con_close();
        return 0;
    }
    
    if (opts.show_history) {
        heur_print();
        con_close();
        return 0;
    }
    
    /* -------------------------------------------------- tag list command  */
    if (opts.group_list) {
        NcdMetadata *meta = db_metadata_load();
        if (meta) {
            db_group_list(meta);
            db_metadata_free(meta);
        } else {
            ncd_println("No groups defined.");
        }
        con_close();
        return 0;
    }
    
    /* ----------------------------------------------- group remove command */
    if (opts.group_remove) {
        char cwd[MAX_PATH] = {0};
        bool have_cwd = platform_get_current_dir(cwd, sizeof(cwd));
        
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            /* Note: Service handles both single-path removal and full group removal */
            int result = state_backend_submit_metadata_update(g_state_view, 
                NCD_META_UPDATE_GROUP_REMOVE, opts.group_name, 
                strlen(opts.group_name) + 1);
            if (result == 0) {
                ncd_printf("Group '%s' removed.\r\n", opts.group_name);
            } else {
                ncd_printf("Group '%s' not found or could not be removed.\r\n", opts.group_name);
            }
            con_close();
            return 0;
        }
        
        /* Standalone mode: load, modify, save */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        /* Check if current directory is in the group */
        bool in_group = false;
        if (have_cwd) {
            int count = 0;
            for (int i = 0; i < meta->groups.count; i++) {
                if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0) {
                    count++;
                    if (_stricmp(meta->groups.groups[i].path, cwd) == 0) {
                        in_group = true;
                    }
                }
            }
            
            if (in_group && count > 1) {
                /* Remove only current directory from group */
                if (db_group_remove_path(meta, opts.group_name, cwd)) {
                    db_metadata_save(meta);
                    ncd_printf("Removed '%s' from group '%s'.\r\n", cwd, opts.group_name);
                } else {
                    ncd_printf("Failed to remove from group '%s'.\r\n", opts.group_name);
                }
                db_metadata_free(meta);
                con_close();
                return 0;
            }
        }
        
        /* Remove entire group (all entries) */
        if (db_group_remove(meta, opts.group_name)) {
            db_metadata_save(meta);
            ncd_printf("Group '%s' removed.\r\n", opts.group_name);
        } else {
            ncd_printf("Group '%s' not found.\r\n", opts.group_name);
        }
        db_metadata_free(meta);
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ group set command   */
    if (opts.group_set) {
        char cwd[MAX_PATH] = {0};
        if (!platform_get_current_dir(cwd, sizeof(cwd))) {
            result_error("Could not determine current directory.");
            con_close();
            return 1;
        }
        
        if (is_service_backend()) {
            /* Service mode: use state view to check current status, then send update */
            const NcdMetadata *meta = get_state_metadata();
            
            /* Check if already in group before adding */
            bool already_in_group = false;
            int existing_count = 0;
            if (meta) {
                for (int i = 0; i < meta->groups.count; i++) {
                    if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0) {
                        existing_count++;
                        if (_stricmp(meta->groups.groups[i].path, cwd) == 0) {
                            already_in_group = true;
                        }
                    }
                }
            }
            
            /* Send update via IPC
             * Data format: [name_len (4 bytes)][path_len (4 bytes)][name string][path string]
             * We send the actual string bytes, not pointers! */
            size_t name_len = strlen(opts.group_name) + 1;  /* Include null terminator */
            size_t path_len = strlen(cwd) + 1;
            size_t data_len = 8 + name_len + path_len;  /* 2 x uint32_t + both strings */
            char *data = (char *)malloc(data_len);
            if (!data) {
                ncd_println("Failed to set group (out of memory?).");
                con_close();
                return 1;
            }
            *(uint32_t *)data = (uint32_t)name_len;
            *(uint32_t *)(data + 4) = (uint32_t)path_len;
            memcpy(data + 8, opts.group_name, name_len);
            memcpy(data + 8 + name_len, cwd, path_len);
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_GROUP_ADD, data, data_len);
            free(data);
            
            if (result == 0) {
                if (already_in_group) {
                    ncd_printf("'%s' is already in group '%s'.\r\n", cwd, opts.group_name);
                } else if (existing_count > 0) {
                    ncd_printf("Added to group '%s' (%d entries) -> '%s'\r\n", 
                               opts.group_name, existing_count + 1, cwd);
                } else {
                    ncd_printf("Group '%s' -> '%s'\r\n", opts.group_name, cwd);
                }
            } else {
                ncd_println("Failed to set group (too many groups?).");
            }
            con_close();
            return 0;
        }
        
        /* Standalone mode: load, modify, save */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        /* Check if already in group before adding */
        bool already_in_group = false;
        for (int i = 0; i < meta->groups.count; i++) {
            if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0 &&
                _stricmp(meta->groups.groups[i].path, cwd) == 0) {
                already_in_group = true;
                break;
            }
        }
        
        /* Count existing entries for this group (before adding) */
        int existing_count = 0;
        for (int i = 0; i < meta->groups.count; i++) {
            if (_stricmp(meta->groups.groups[i].name, opts.group_name) == 0) {
                existing_count++;
            }
        }
        
        if (db_group_set(meta, opts.group_name, cwd)) {
            db_metadata_save(meta);
            
            if (already_in_group) {
                ncd_printf("'%s' is already in group '%s'.\r\n", cwd, opts.group_name);
            } else if (existing_count > 0) {
                /* Added to existing group */
                ncd_printf("Added to group '%s' (%d entries) -> '%s'\r\n", 
                           opts.group_name, existing_count + 1, cwd);
            } else {
                /* New group created */
                ncd_printf("Group '%s' -> '%s'\r\n", opts.group_name, cwd);
            }
        } else {
            ncd_println("Failed to set group (too many groups?).");
        }
        db_metadata_free(meta);
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ exclusion list command */
    if (opts.exclusion_list) {
        NcdMetadata *meta = db_metadata_load();
        if (meta) {
            db_exclusion_print(meta);
            db_metadata_free(meta);
        } else {
            ncd_println("Exclusion list: empty");
        }
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ exclusion remove command */
    if (opts.exclusion_remove) {
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_EXCLUSION_REMOVE, opts.exclusion_pattern,
                strlen(opts.exclusion_pattern) + 1);
            if (result == 0) {
                ncd_printf("Removed exclusion: %s\r\n", opts.exclusion_pattern);
            } else {
                ncd_printf("Exclusion not found: %s\r\n", opts.exclusion_pattern);
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (!meta) meta = db_metadata_create();
            
            if (db_exclusion_remove(meta, opts.exclusion_pattern)) {
                db_metadata_save(meta);
                ncd_printf("Removed exclusion: %s\r\n", opts.exclusion_pattern);
            } else {
                ncd_printf("Exclusion not found: %s\r\n", opts.exclusion_pattern);
            }
            db_metadata_free(meta);
        }
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ exclusion add command */
    if (opts.exclusion_add) {
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_EXCLUSION_ADD, opts.exclusion_pattern,
                strlen(opts.exclusion_pattern) + 1);
            if (result == 0) {
                ncd_printf("Added exclusion: %s\r\n", opts.exclusion_pattern);
            } else {
                ncd_printf("Failed to add exclusion.\r\n");
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (!meta) meta = db_metadata_create();
            
            if (db_exclusion_add(meta, opts.exclusion_pattern)) {
                db_metadata_save(meta);
                ncd_printf("Added exclusion: %s\r\n", opts.exclusion_pattern);
            } else {
                const char *err = db_get_last_error();
                ncd_printf("Failed to add exclusion: %s\r\n", err);
            }
            db_metadata_free(meta);
        }
        con_close();
        return 0;
    }
    /* ------------------------------------------------ directory history list */
    if (opts.history_list) {
        NcdMetadata *meta = db_metadata_load();
        if (meta) {
            db_dir_history_print(meta);
            db_metadata_free(meta);
        } else {
            ncd_println("Directory history: empty");
        }
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history clear */
    if (opts.history_clear) {
        if (is_service_backend()) {
            /* Service mode: send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_CLEAR_HISTORY, NULL, 0);
            if (result == 0) {
                ncd_println("Directory history cleared.");
            } else {
                ncd_println("Failed to clear directory history.");
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (meta) {
                db_dir_history_clear(meta);
                db_metadata_save(meta);
                ncd_println("Directory history cleared.");
                db_metadata_free(meta);
            }
        }
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history remove by index */
    if (opts.history_remove > 0) {
        int idx = opts.history_remove - 1;  /* convert 1-based to 0-based */
        
        if (is_service_backend()) {
            /* Service mode: read from state view, then send update via IPC */
            const NcdMetadata *meta = get_state_metadata();
            if (!meta) {
                ncd_println("No history.");
                con_close();
                return 1;
            }
            
            const NcdDirHistoryEntry *entry = db_dir_history_get(meta, idx);
            if (!entry) {
                ncd_printf("History entry %d not found (only %d entries).\r\n",
                           opts.history_remove, db_dir_history_count(meta));
                con_close();
                return 1;
            }
            
            ncd_printf("Removed from history: %s\r\n", entry->path);
            
            /* Send update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_DIR_HISTORY_REMOVE, &idx, sizeof(idx));
            if (result != 0) {
                ncd_println("Warning: Failed to update service.");
            }
        } else {
            /* Standalone mode: load, modify, save */
            NcdMetadata *meta = db_metadata_load();
            if (!meta) {
                ncd_println("No history.");
                con_close();
                return 1;
            }
            
            const NcdDirHistoryEntry *entry = db_dir_history_get(meta, idx);
            if (!entry) {
                ncd_printf("History entry %d not found (only %d entries).\r\n",
                           opts.history_remove, db_dir_history_count(meta));
                db_metadata_free(meta);
                con_close();
                return 1;
            }
            
            ncd_printf("Removed from history: %s\r\n", entry->path);
            db_dir_history_remove(meta, idx);
            db_metadata_save(meta);
            db_metadata_free(meta);
        }
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history browse */
    if (opts.history_browse) {
        NcdMetadata *meta = NULL;
        const NcdMetadata *meta_view = NULL;
        
        if (is_service_backend()) {
            /* Service mode: use state view */
            meta_view = get_state_metadata();
        } else {
            /* Standalone mode: load metadata */
            meta = db_metadata_load();
            meta_view = meta;
        }
        
        if (!meta_view || db_dir_history_count(meta_view) == 0) {
            ncd_println("No history entries.");
            if (meta) db_metadata_free(meta);
            con_close();
            return 0;
        }
        
        /* Build match array from history entries */
        int count = db_dir_history_count(meta_view);
        NcdMatch *matches = ncd_malloc(sizeof(NcdMatch) * count);
        int valid = 0;
        
        for (int i = 0; i < count; i++) {
            const NcdDirHistoryEntry *e = db_dir_history_get(meta_view, i);
            if (e && platform_dir_exists(e->path)) {
                platform_strncpy_s(matches[valid].full_path, 
                                   sizeof(matches[valid].full_path), e->path);
                matches[valid].drive_letter = e->drive;
                matches[valid].drive_index = -1;  /* not from database */
                matches[valid].dir_index = -1;
                valid++;
            } else if (e) {
                ncd_printf("Warning: Directory no longer exists: %s\r\n", e->path);
            }
        }
        
        if (valid == 0) {
            ncd_println("No valid history entries (directories may have been removed).");
            free(matches);
            if (meta) db_metadata_free(meta);
            con_close();
            return 0;
        }
        
        int chosen;
        if (valid == 1) {
            chosen = 0;
        } else {
            /* Use callback for service mode, NULL for standalone */
            ui_history_delete_cb delete_cb = is_service_backend() ? history_delete_service_cb : NULL;
            chosen = ui_select_history(matches, &valid, (NcdMetadata *)meta_view, delete_cb, NULL);
            /* Note: valid may have decreased if user deleted entries */
        }
        
        if (chosen >= 0 && chosen < valid) {
            add_current_dir_to_history();
            result_ok(matches[chosen].full_path, matches[chosen].drive_letter);
        } else {
            result_cancel();
        }
        
        free(matches);
        if (meta) db_metadata_free(meta);
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history ping-pong */
    if (opts.history_pingpong || (!opts.has_search && argc == 1)) {
        /* /0 or no args: ping-pong between [0] and [1], swapping them */
        const NcdMetadata *meta_view = NULL;
        
        if (is_service_backend()) {
            meta_view = get_state_metadata();
        } else {
            /* Standalone: load metadata */
            NcdMetadata *meta = db_metadata_load();
            if (!meta) meta = db_metadata_create();
            meta_view = meta;
        }
        
        if (!meta_view || db_dir_history_count(meta_view) < 2) {
            ncd_println("NCD: Not enough history entries to ping-pong.\r\n");
            ncd_println("Navigate to a directory first with 'ncd <search>'.\r\n");
            if (!is_service_backend() && meta_view) {
                db_metadata_free((NcdMetadata *)meta_view);
            }
            con_close();
            return 1;
        }
        
        /* Get the second entry (will become first after swap) */
        const NcdDirHistoryEntry *entry = db_dir_history_get(meta_view, 1);
        if (!entry) {
            result_error("History entry not found.");
            if (!is_service_backend()) {
                db_metadata_free((NcdMetadata *)meta_view);
            }
            con_close();
            return 1;
        }
        
        /* Verify directory still exists */
        if (!platform_dir_exists(entry->path)) {
            ncd_printf("NCD: Directory no longer exists: %s\r\n", entry->path);
            if (!is_service_backend()) {
                db_metadata_free((NcdMetadata *)meta_view);
            }
            con_close();
            return 1;
        }
        
        if (is_service_backend()) {
            /* Service mode: send swap update via IPC */
            int result = state_backend_submit_metadata_update(g_state_view,
                NCD_META_UPDATE_DIR_HISTORY_SWAP, NULL, 0);
            if (result != 0) {
                ncd_println("Warning: Failed to update service.");
            }
        } else {
            /* Standalone mode: swap and save */
            NcdMetadata *meta = (NcdMetadata *)meta_view;
            db_dir_history_swap_first_two(meta);
            meta->dir_history_dirty = true;
            db_metadata_save(meta);
            db_metadata_free(meta);
        }
        
        /* Return the (now) first entry */
        result_ok(entry->path, entry->drive);
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history jump /1-/9 */
    if (opts.history_index > 0) {
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        int count = db_dir_history_count(meta);
        if (count == 0) {
            ncd_println("NCD: No history entries.\r\n");
            db_metadata_free(meta);
            con_close();
            return 1;
        }
        
        if (opts.history_index > count) {
            ncd_printf("NCD: History entry /%d not found (only %d entries).\r\n",
                       opts.history_index, count);
            db_metadata_free(meta);
            con_close();
            return 1;
        }
        
        /* /1 = oldest, /count = most recent */
        const NcdDirHistoryEntry *entry = db_dir_history_get_by_display_index(meta, opts.history_index);
        if (!entry) {
            result_error("History entry not found.");
            db_metadata_free(meta);
            con_close();
            return 1;
        }
        
        /* Verify directory still exists */
        if (!platform_dir_exists(entry->path)) {
            ncd_printf("NCD: Directory no longer exists: %s\r\n", entry->path);
            db_metadata_free(meta);
            con_close();
            return 1;
        }
        
        result_ok(entry->path, entry->drive);
        db_metadata_free(meta);
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------------ agent mode  */
    if (opts.agent_mode) {
        int result = 0;
        
        switch (opts.agent_subcommand) {
            case AGENT_SUB_QUERY: {
                /* Load database and search */
                NcdDatabase *db = NULL;
                char target_db[NCD_MAX_PATH] = {0};
                if (opts.db_override[0]) {
                    platform_strncpy_s(target_db, sizeof(target_db), opts.db_override);
                    db = db_load_auto(target_db);
                } else {
                    char cwd[MAX_PATH] = {0};
                    platform_get_current_dir(cwd, sizeof(cwd));
                    char target_drive = platform_get_drive_letter(cwd);
                    
                    if (isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
                        target_drive = (char)toupper((unsigned char)opts.search[0]);
                    }
                    
                    if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                        db = db_load_auto(target_db);
                    }
                }
                
                int count = agent_mode_query(db, &opts);
                result = (count > 0) ? 0 : 1;
                if (db) db_free(db);
                break;
            }
            case AGENT_SUB_LS: {
                int count = agent_mode_ls(&opts);
                result = (count > 0) ? 0 : 1;
                break;
            }
            case AGENT_SUB_TREE: {
                /* Prefer service-backed state when available for consistency */
                NcdDatabase *db = NULL;
                bool owns_db = false;
                const NcdDatabase *state_db = get_state_database();
                if (state_db && is_service_backend()) {
                    db = (NcdDatabase *)state_db;
                } else {
                    char target_db[NCD_MAX_PATH] = {0};
                    if (opts.db_override[0]) {
                        platform_strncpy_s(target_db, sizeof(target_db), opts.db_override);
                        db = db_load_auto(target_db);
                    } else {
                        char cwd[MAX_PATH] = {0};
                        platform_get_current_dir(cwd, sizeof(cwd));
                        char target_drive = platform_get_drive_letter(cwd);
                        
                        if (isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
                            target_drive = (char)toupper((unsigned char)opts.search[0]);
                        }
                        
                        if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                            db = db_load_auto(target_db);
                        }
                    }
                    owns_db = true;
                }
                
                int count = agent_mode_tree(db, &opts);
                result = (count > 0) ? 0 : 1;
                if (db && owns_db) db_free(db);
                break;
            }
            case AGENT_SUB_CHECK: {
                /* Load database for check */
                NcdDatabase *db = NULL;
                if (opts.agent_check_db_age || opts.agent_check_stats || opts.has_search) {
                    char target_db[NCD_MAX_PATH] = {0};
                    if (opts.db_override[0]) {
                        platform_strncpy_s(target_db, sizeof(target_db), opts.db_override);
                        db = db_load_auto(target_db);
                    } else {
                        char cwd[MAX_PATH] = {0};
                        platform_get_current_dir(cwd, sizeof(cwd));
                        char target_drive = platform_get_drive_letter(cwd);
                        
                        if (opts.has_search && isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
                            target_drive = (char)toupper((unsigned char)opts.search[0]);
                        }
                        
                        if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                            db = db_load_auto(target_db);
                        }
                    }
                }
                
                result = agent_mode_check(db, &opts);
                if (db) db_free(db);
                break;
            }
            case AGENT_SUB_COMPLETE: {
                /* Complete doesn't need database, but can use it if available */
                NcdDatabase *db = NULL;
                char target_db[NCD_MAX_PATH] = {0};
                if (opts.db_override[0]) {
                    platform_strncpy_s(target_db, sizeof(target_db), opts.db_override);
                    db = db_load_auto(target_db);
                } else {
                    char cwd[MAX_PATH] = {0};
                    platform_get_current_dir(cwd, sizeof(cwd));
                    char target_drive = platform_get_drive_letter(cwd);
                    
                    if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                        db = db_load_auto(target_db);
                    }
                }
                
                result = agent_mode_complete(db, &opts);
                if (db) db_free(db);
                break;
            }
            case AGENT_SUB_MKDIR: {
                result = agent_mode_mkdir(&opts);
                break;
            }
            case AGENT_SUB_MKDIRS: {
                result = agent_mode_mkdirs(&opts);
                break;
            }
            case AGENT_SUB_QUIT: {
                /* Request graceful service shutdown */
                if (!ipc_service_exists()) {
                    if (opts.agent_json) {
                        agent_print("{\"v\":1,\"status\":\"not_running\",\"message\":\"Service not running\"}\r\n");
                    } else {
                        agent_print("NOT_RUNNING\r\n");
                    }
                    result = 0;
                    break;
                }
                
                NcdIpcClient *client = ipc_client_connect();
                if (!client) {
                    if (opts.agent_json) {
                        agent_print("{\"v\":1,\"status\":\"error\",\"message\":\"Failed to connect to service\"}\r\n");
                    } else {
                        agent_print("ERROR: Failed to connect to service\r\n");
                    }
                    result = 1;
                    break;
                }
                
                NcdIpcResult ipc_result = ipc_client_request_shutdown(client);
                ipc_client_disconnect(client);
                
                if (ipc_result == NCD_IPC_OK) {
                    if (opts.agent_json) {
                        agent_print("{\"v\":1,\"status\":\"shutdown_requested\",\"message\":\"Service shutdown requested\"}\r\n");
                    } else {
                        agent_print("OK: Service shutdown requested\r\n");
                    }
                    result = 0;
                } else {
                    if (opts.agent_json) {
                        agent_printf("{\"v\":1,\"status\":\"error\",\"message\":\"%s\"}\r\n", ipc_error_string(ipc_result));
                    } else {
                        agent_printf("ERROR: %s\r\n", ipc_error_string(ipc_result));
                    }
                    result = 1;
                }
                break;
            }
            default: {
                agent_print_usage();
                result = 1;
                break;
            }
        }
        
        con_close();
        return result;
    }
    
    /* ------------------------------------------------ configuration editor */
    if (opts.config_edit) {
        NcdMetadata *meta = get_metadata();
        if (!meta) {
            ncd_println("Failed to load metadata.");
            con_close();
            return 1;
        }
        
        if (ui_edit_config(meta)) {
            meta->config_dirty = true;
            if (is_service_backend()) {
                /* Service mode: send update via IPC */
                if (state_backend_submit_metadata_update(g_state_view, NCD_META_UPDATE_CONFIG,
                                                         &meta->cfg, sizeof(meta->cfg)) == 0) {
                    ncd_println("Configuration saved.");
                } else {
                    ncd_println("Failed to save configuration via service.");
                }
            } else {
                /* Standalone mode: save directly */
                if (db_metadata_save(meta)) {
                    ncd_println("Configuration saved.");
                } else {
                    ncd_println("Failed to save configuration.");
                }
            }
        } else {
            ncd_println("Configuration cancelled.");
        }
        con_close();
        return 0;
    }
    /*
     * /v behavior:
     *   - If /v is the only actionable flag, print version and exit.
     *   - If combined with a search or /r, print version and continue.
     */
    if (opts.show_version) {
        print_version();
        if (!opts.has_search && !opts.force_rescan) {
            con_close();
            return 0;
        }
    }
    /* ------------------------------------------- forced rescan shortcut  */
    if (opts.force_rescan && !opts.has_search) {
        /* Load metadata and set exclusion list for scanning */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) {
            meta = db_metadata_create();
        }
        db_exclusion_init_defaults(meta);
        scan_set_exclusion_list(&meta->exclusions);
        
        NcdDatabase *db = db_create();
        db->default_show_hidden = opts.show_hidden;
        db->default_show_system = opts.show_system;
        db->last_scan = time(NULL);
        /* For subdirectory rescan, load existing database first.
         * A partial rescan merges into an existing DB, so we cannot proceed
         * if the on-disk format is outdated -- the merge would be lost when
         * the DB is eventually rebuilt.  Prompt for a full rescan instead. */
        if (opts.scan_subdirectory[0]) {
            char drive = platform_get_drive_letter(opts.scan_subdirectory);
            char drv_path[NCD_MAX_PATH];
            if (opts.db_override[0]) {
                platform_strncpy_s(drv_path, sizeof(drv_path), opts.db_override);
            } else if (!db_drive_path(drive, drv_path, sizeof(drv_path))) {
                drv_path[0] = '\0';
            }
            if (drv_path[0]) {
                int vstatus = db_check_file_version(drv_path);
                if (vstatus == DB_VERSION_MISMATCH || vstatus == DB_VERSION_SKIPPED) {
                    ncd_printf("NCD: Database for %c: needs a full rescan "
                               "(format changed).\r\n"
                               "     Run 'ncd -r' to rebuild, then retry "
                               "the subdirectory rescan.\r\n", drive);
                    db_metadata_free(meta);
                    scan_set_exclusion_list(NULL);
                    db_free(db);
                    con_close();
                    return 1;
                }
                /* Load existing database for subdirectory rescan to merge with */
                NcdDatabase *existing = db_load_auto(drv_path);
                if (existing) {
                    db_free(db);
                    db = existing;
                }
            }
        }
        int n = run_requested_rescan(db, &opts, &meta->exclusions);
        
        /* Cleanup metadata */
        db_metadata_free(meta);
        scan_set_exclusion_list(NULL);
        ncd_printf("  Scan complete: %d directories found across %d drive(s).\r\n",
                   n, db->drive_count);

        /* Save each drive to its own per-drive database file. */
        for (int i = 0; i < db->drive_count; i++) {
            char drv_path[NCD_MAX_PATH];
            if (opts.db_override[0]) {
                /* Use custom database path from -d flag */
                platform_strncpy_s(drv_path, sizeof(drv_path), opts.db_override);
            } else if (!db_drive_path(db->drives[i].letter, drv_path, sizeof(drv_path))) {
                ncd_printf("NCD: Warning -- could not build path for drive %c:\r\n",
                           db->drives[i].letter);
                continue;
            }
            if (!db_save_binary_single(db, i, drv_path))
                ncd_printf("NCD: Warning -- could not save %c: database.\r\n",
                           db->drives[i].letter);
            else
                ncd_printf("  Saved %c: -> %s\r\n", db->drives[i].letter, drv_path);
        }

        db_free(db);
        return 0;
    }

    /* ------------------------------------------ need a search at this pt */
    if (!opts.has_search) {
        print_usage();
        con_close();
        return 0;
    }

    /* ------------------------------------------------ version check flow  */
    /*
     * Only check all databases for version mismatches when we actually try
     * to load a database and find it has the wrong version. This keeps
     * normal operation fast - we only do the full check when necessary.
     */

    /* -------------------------------------- direct navigation mode (.)   */
    if (strcmp(opts.search, ".") == 0 ||
        strcmp(opts.search, ".\\") == 0 ||
        strcmp(opts.search, "./") == 0) {
        char cwd[MAX_PATH] = {0};
        if (!platform_get_current_dir(cwd, sizeof(cwd))) {
            result_error("Could not determine current directory.");
            con_close();
            return 1;
        }

        char chosen[MAX_PATH] = {0};
        if (!ui_navigate_directory(cwd, chosen, sizeof(chosen))) {
            result_cancel();
            con_close();
            return 0;
        }
        heur_note_choice(opts.search, chosen);
        add_current_dir_to_history();
        result_ok(chosen, (char)toupper((unsigned char)chosen[0]));
        con_close();
        return 0;
    }

    /* -------------------------- drive-root navigation mode (X: / X:\)   */
    if (platform_is_drive_specifier(opts.search)) {
        char root[MAX_PATH] = {0};
        root[0] = (char)toupper((unsigned char)opts.search[0]);
        root[1] = ':';
        root[2] = '\\';
        root[3] = '\0';

        char chosen[MAX_PATH] = {0};
        if (!ui_navigate_directory(root, chosen, sizeof(chosen))) {
            result_cancel();
            con_close();
            return 0;
        }
        heur_note_choice(opts.search, chosen);
        add_current_dir_to_history();
        result_ok(chosen, root[0]);
        con_close();
        return 0;
    }

    /* -------------------------------------- tag lookup (@tagname)        */
    if (opts.search[0] == '@') {
        const char *group_name = opts.search + 1;  /* Skip the @ prefix */
        
        NcdMetadata *meta = db_metadata_load();
        if (!meta) {
            result_error("Unknown group: %s", group_name);
            con_close();
            return 1;
        }
        
        /* Get all entries for this group */
        const NcdGroupEntry *entries[256];
        int count = db_group_get_all(meta, group_name, entries, 256);
        
        if (count == 0) {
            db_metadata_free(meta);
            result_error("Unknown group: %s", group_name);
            con_close();
            return 1;
        }
        
        /* Filter out non-existent directories */
        NcdMatch *matches = ncd_malloc(sizeof(NcdMatch) * count);
        int valid_count = 0;
        
        for (int i = 0; i < count; i++) {
            if (platform_dir_exists(entries[i]->path)) {
                platform_strncpy_s(matches[valid_count].full_path, 
                                   sizeof(matches[valid_count].full_path),
                                   entries[i]->path);
                matches[valid_count].drive_letter = entries[i]->path[0];
                matches[valid_count].drive_index = -1;  /* Not from database */
                matches[valid_count].dir_index = -1;
                valid_count++;
            } else {
                ncd_printf("Warning: Group entry points to non-existent directory: %s\r\n",
                           entries[i]->path);
            }
        }
        
        if (valid_count == 0) {
            free(matches);
            db_metadata_free(meta);
            result_error("Group '%s' has no valid directories.", group_name);
            con_close();
            return 1;
        }
        
        /* Single entry - navigate directly */
        if (valid_count == 1) {
            char drive_letter = matches[0].drive_letter;
            add_current_dir_to_history();
            result_ok(matches[0].full_path, drive_letter);
            free(matches);
            db_metadata_free(meta);
            con_close();
            return 0;
        }
        
        /* Multiple entries - show selection UI */
        int selected = ui_select_match(matches, valid_count, group_name);
        
        if (selected < 0) {
            /* User cancelled */
            free(matches);
            db_metadata_free(meta);
            result_cancel();
            con_close();
            return 0;
        }
        
        /* Navigate to selected entry */
        char drive_letter = matches[selected].drive_letter;
        add_current_dir_to_history();
        result_ok(matches[selected].full_path, drive_letter);
        free(matches);
        db_metadata_free(meta);
        con_close();
        return 0;
    }

    /* ----------------------------------------- parse drive from search   */
    char clean_search[NCD_MAX_PATH];
    char target_drive = platform_parse_drive_from_search(opts.search, clean_search, sizeof(clean_search));

    /* ----------------------- resolve target drive's per-drive db path    */
    char target_db[NCD_MAX_PATH] = {0};
    if (opts.db_override[0]) {
        /* Use custom database path from -d flag */
        platform_strncpy_s(target_db, sizeof(target_db), opts.db_override);
    } else if (!db_drive_path(target_drive, target_db, sizeof(target_db))) {
        result_error("Cannot determine database path for drive %c:",
                     target_drive);
        con_close();
        return 1;
    }

    bool db_exists = platform_file_exists(target_db);

    /* -------------------- first run / forced rescan: rebuild everything  */
    if (!db_exists || opts.force_rescan) {
        /* Subdirectory rescan with search: same version guard as the
         * no-search path above -- a partial rescan into an outdated DB
         * would be lost on the next full rebuild. */
        if (opts.scan_subdirectory[0] && db_exists) {
            int vstatus = db_check_file_version(target_db);
            if (vstatus == DB_VERSION_MISMATCH || vstatus == DB_VERSION_SKIPPED) {
                ncd_printf("NCD: Database for %c: needs a full rescan "
                           "(format changed).\r\n"
                           "     Run 'ncd -r' to rebuild, then retry "
                           "the subdirectory rescan.\r\n", target_drive);
                con_close();
                return 1;
            }
        }

        bool allow_implicit_rebuild = true;
        if (ncd_test_mode_active() && !opts.force_rescan && !opts.scan_subdirectory[0]) {
            /* Never trigger an implicit full-drive scan in test mode. */
            allow_implicit_rebuild = false;
            ncd_printf("NCD: Test mode active -- skipping implicit rebuild for drive %c:.\r\n",
                       target_drive);
            ncd_println("     Run 'ncd -r:.' from your isolated test directory first.");
        }

        if (allow_implicit_rebuild) {
            if (opts.force_rescan && opts.scan_drive_count > 0) {
                ncd_printf("NCD: Forced rescan -- scanning requested drives...\r\n");
            } else if (opts.force_rescan) {
                ncd_printf("NCD: Forced rescan -- scanning all drives...\r\n");
            } else {
                ncd_printf("NCD: %s -- scanning drive %c:...\r\n",
                           db_exists ? "Forced rescan" : "No database found",
                           target_drive);
            }

            NcdDatabase *scan_db = db_create();
            scan_db->default_show_hidden = opts.show_hidden;
            scan_db->default_show_system = opts.show_system;
            scan_db->last_scan = time(NULL);

            /* For subdirectory rescan, load existing database so we merge
             * rather than replace the drive data. */
            if (opts.scan_subdirectory[0] && db_exists) {
                NcdDatabase *existing = db_load_auto(target_db);
                if (existing) {
                    db_free(scan_db);
                    scan_db = existing;
                }
            }

            int n;
            if (opts.force_rescan) {
                n = run_requested_rescan(scan_db, &opts, NULL);
            } else {
                /* Scan ONLY the target drive, not all drives */
                char mount_path[MAX_PATH];
                if (!platform_build_mount_path(target_drive, mount_path, sizeof(mount_path))) {
                    platform_get_current_dir(mount_path, sizeof(mount_path));
                }
                /* For drive 0 (native Linux), ensure we use CWD if platform_build_mount_path
                 * returns a generic path like "/mnt/" that doesn't match the actual location */
                if (target_drive == 0 && strncmp(mount_path, "/mnt/", 5) == 0) {
                    platform_get_current_dir(mount_path, sizeof(mount_path));
                }
                const char *mounts[1] = { mount_path };
                n = scan_mounts(scan_db, mounts, 1, opts.show_hidden, opts.show_system, opts.timeout_seconds, NULL);
            }
            ncd_printf("  Scan complete: %d directories found.\r\n", n);

            for (int i = 0; i < scan_db->drive_count; i++) {
                char drv_path[NCD_MAX_PATH];
                if (opts.db_override[0]) {
                    /* Use custom database path from -d flag */
                    platform_strncpy_s(drv_path, sizeof(drv_path), opts.db_override);
                } else if (!db_drive_path(scan_db->drives[i].letter, drv_path,
                                   sizeof(drv_path)))
                    continue;
                db_save_binary_single(scan_db, i, drv_path);
            }
            db_free(scan_db);

            /* If database didn't exist, return special exit code 3 to indicate rebuild */
            if (!db_exists) {
                con_close();
                return 3;
            }
        }
    }

    /* -------------------- check version and load target drive           */
    /*
     * Fast path: Only check our specific database first. If it has the wrong
     * version, then we check ALL databases to present a unified update UI.
     * This keeps normal operation fast.
     */
    int target_version_status = db_check_file_version(target_db);
    
    if (target_version_status == DB_VERSION_MISMATCH && !opts.force_rescan) {
        /* Target DB has wrong version - check ALL databases for version issues */
        DbVersionInfo outdated[26];
        int outdated_count = db_check_all_versions(outdated, 26);
        
        if (outdated_count > 0) {
            /* Build arrays for UI */
            char drives[26];
            uint16_t versions[26];
            bool selected[26];
            
            for (int i = 0; i < outdated_count; i++) {
                drives[i] = outdated[i].letter;
                versions[i] = outdated[i].version;
            }
            
            /* Present TUI for drive selection */
            int choice = ui_select_drives_for_update(drives, versions, 
                                                      outdated_count, selected, 60);
            
            if (choice == UI_UPDATE_NONE) {
                /* User chose to skip all - set skip flags */
                for (int i = 0; i < outdated_count; i++) {
                    db_set_skipped_rescan_flag(outdated[i].path);
                }
                ncd_println("Skipped database updates. Use 'ncd -r' to force rescan.");
            } else {
                /* User chose to update some or all */
                bool any_selected = false;
                for (int i = 0; i < outdated_count; i++) {
                    if (selected[i]) {
                        any_selected = true;
                        break;
                    }
                }
                
                if (any_selected) {
                    /* Set skip flag on drives user chose NOT to update */
                    for (int i = 0; i < outdated_count; i++) {
                        if (!selected[i]) {
                            db_set_skipped_rescan_flag(outdated[i].path);
                        }
                    }
                    
                    /* Build drive list for rescan */
                    char rescan_drives[26];
                    int rescan_count = 0;
                    for (int i = 0; i < outdated_count; i++) {
                        if (selected[i]) {
                            rescan_drives[rescan_count++] = outdated[i].letter;
                        }
                    }
                    
                    /* Perform rescan of selected drives */
                    ncd_printf("NCD: Updating %d database(s)...\r\n", rescan_count);
                    NcdDatabase *scan_db = db_create();
                    scan_db->default_show_hidden = opts.show_hidden;
                    scan_db->default_show_system = opts.show_system;
                    scan_db->last_scan = time(NULL);
                    
#if NCD_PLATFORM_WINDOWS
                    const char *mounts[26];
                    char mount_bufs[26][MAX_PATH];
                    for (int i = 0; i < rescan_count; i++) {
                        snprintf(mount_bufs[i], MAX_PATH, "%c:%s", rescan_drives[i], NCD_PATH_SEP);
                        mounts[i] = mount_bufs[i];
                    }
                    int n = scan_mounts(scan_db, mounts, rescan_count, 
                                        opts.show_hidden, opts.show_system, 
                                        opts.timeout_seconds, NULL);
#else
                    /* Linux: need to map drive letters back to mount points */
                    const char *mounts[26];
                    char mount_bufs[26][MAX_PATH];
                    int mcount = 0;
                    char all_mount_bufs[26][MAX_PATH];
                    const char *all_mount_ptrs[26];
                    int all_count = ncd_platform_enumerate_mounts(all_mount_bufs, all_mount_ptrs,
                                                              sizeof(all_mount_bufs[0]), 26);
                    for (int i = 0; i < rescan_count && mcount < 26; i++) {
                        for (int j = 0; j < all_count; j++) {
                            char letter = platform_get_drive_letter(all_mount_ptrs[j]);
                            if (letter == rescan_drives[i]) {
                                snprintf(mount_bufs[mcount], MAX_PATH, "%s", all_mount_ptrs[j]);
                                mounts[mcount] = mount_bufs[mcount];
                                mcount++;
                                break;
                            }
                        }
                    }
                    int n = scan_mounts(scan_db, mounts, mcount, 
                                        opts.show_hidden, opts.show_system, 
                                        opts.timeout_seconds, NULL);
#endif
                    ncd_printf("  Scan complete: %d directories found.\r\n", n);
                    
                    /* Save updated databases */
                    for (int i = 0; i < scan_db->drive_count; i++) {
                        char drv_path[NCD_MAX_PATH];
                        if (!db_drive_path(scan_db->drives[i].letter, drv_path,
                                           sizeof(drv_path)))
                            continue;
                        db_save_binary_single(scan_db, i, drv_path);
                    }
                    db_free(scan_db);
                } else {
                    /* User deselected all - treat as skip */
                    for (int i = 0; i < outdated_count; i++) {
                        db_set_skipped_rescan_flag(outdated[i].path);
                    }
                    ncd_println("Skipped database updates. Use 'ncd -r' to force rescan.");
                }
            }
        }
        /* Re-check target after potential update */
        target_version_status = db_check_file_version(target_db);
    }
    
    /* Try to get database from state backend (service) first */
    NcdDatabase *primary_db = (NcdDatabase *)get_state_database();
    bool using_service_db = (primary_db != NULL);
    
    /* Track if database was rebuilt for exit code 3 */
    bool database_was_rebuilt = false;
    
    /* Fall back to loading local database if service not available.
     * Rebuild if:
     * 1. No database loaded from service/backend AND target has error (missing/corrupt), OR
     * 2. Target drive's database file is corrupt (even if other drives loaded) */
    bool target_db_corrupt = (target_version_status == DB_VERSION_ERROR);
    if ((!primary_db || target_db_corrupt) && target_version_status != DB_VERSION_SKIPPED) {
        if (ncd_test_mode_active() && !opts.force_rescan && !opts.scan_subdirectory[0]) {
            ncd_printf("NCD: Test mode active -- refusing automatic rebuild scan for drive %c:.\r\n",
                       target_drive);
        } else {
        /* Corrupt target DB (not just wrong version) -- rebuild that file */
        ncd_printf("NCD: Database for %c: is corrupt -- rebuilding...\r\n",
                   target_drive);
        NcdDatabase *scan_db = db_create();
        scan_db->default_show_hidden = opts.show_hidden;
        scan_db->default_show_system = opts.show_system;
        scan_db->last_scan = time(NULL);
        /* Scan ONLY the target drive, not all drives */
        char mount_path[MAX_PATH];
        if (!platform_build_mount_path(target_drive, mount_path, sizeof(mount_path))) {
            platform_get_current_dir(mount_path, sizeof(mount_path));
        }
        /* For drive 0 (native Linux), ensure we use CWD */
        if (target_drive == 0 && strncmp(mount_path, "/mnt/", 5) == 0) {
            platform_get_current_dir(mount_path, sizeof(mount_path));
        }
        const char *mounts[1] = { mount_path };
        int n = scan_mounts(scan_db, mounts, 1, opts.show_hidden, opts.show_system, opts.timeout_seconds, NULL);
        ncd_printf("  Rebuild complete: %d directories found.\r\n", n);
        for (int i = 0; i < scan_db->drive_count; i++) {
            char drv_path[NCD_MAX_PATH];
            if (!db_drive_path(scan_db->drives[i].letter, drv_path,
                               sizeof(drv_path)))
                continue;
            db_save_binary_single(scan_db, i, drv_path);
        }
        db_free(scan_db);
        primary_db = db_load_auto(target_db);
        database_was_rebuilt = true;
        if (!primary_db) {
            /* Database was rebuilt but failed to load - return special exit code 3 */
            con_close();
            return 3;
        }
        }
    }

    /* Override with custom database if /d was specified (standalone mode only).
     * Do NOT free the state-backend database here -- it is owned by the backend
     * and will be cleaned up on exit. Just shadow the local variable. */
    if (opts.db_override[0] && !is_service_backend()) {
        primary_db = db_load_auto(target_db);
    } else if (!primary_db && !database_was_rebuilt && target_version_status != DB_VERSION_SKIPPED) {
        primary_db = db_load_auto(target_db);
    }

    /* Load metadata to get exclusion list for filtering search results */
    NcdMetadata *search_meta = NULL;
    if (is_service_backend()) {
        search_meta = (NcdMetadata *)get_state_metadata();
    } else {
        search_meta = db_metadata_load();
    }
    
    /* 
     * Filter excluded directories from the loaded database.
     * Note: When using the service, filtering is already done by the service
     * during database loading, so we skip it here. Local backend still needs
     * filtering because it loads the raw database from disk.
     */
    if (primary_db && search_meta && search_meta->exclusions.count > 0 && !is_service_backend()) {
        int removed = db_filter_excluded(primary_db, search_meta);
        if (removed > 0) {
            NCD_DEBUG_LOG("Filtered %d excluded directories from database\n", removed);
        }
    }
    
    int match_count = 0;
    NcdMatch *matches = NULL;

    if (primary_db) {
        if (opts.fuzzy_match) {
            matches = matcher_find_fuzzy(primary_db,
                                         clean_search,
                                         opts.show_hidden,
                                         opts.show_system,
                                         &match_count);
        } else {
            matches = matcher_find(primary_db,
                                   clean_search,
                                   opts.show_hidden,
                                   opts.show_system,
                                   &match_count);
        }
    }

    /* -------------------- fallback: search all other drive databases     */
    if ((!matches || match_count == 0) && !is_service_backend()) {
        if (primary_db && !using_service_db) { db_free(primary_db); primary_db = NULL; } else { primary_db = NULL; }

        /* In test mode, do not fall back to scanning user drive databases.
         * Tests use isolated databases via /d override and should not leak
         * into the user's real data. */
        const char *test_mode = getenv("NCD_TEST_MODE");
        if (test_mode && test_mode[0]) {
            /* Skip fallback search in test mode */
        } else {

        /* 
         * Phase 1: Collect all available drive letters first, then load all
         * databases before searching. This avoids the load-search-free 
         * pattern that prevented the name index from being reused.
         */
        char drive_letters[26];
        int drive_count = 0;
        
#if NCD_PLATFORM_WINDOWS
        for (int i = 0; i < 26 && drive_count < 26; i++) {
            char letter = (char)('A' + i);
            if (letter == target_drive) continue;   /* already searched    */
#else
        /* On Linux, include drive 0 (native filesystem) and drives 1-26 (/mnt/X) */
        char all_drives[27];
        int all_count = platform_get_available_drives(all_drives, 27);
        for (int i = 0; i < all_count && drive_count < 26; i++) {
            char letter = all_drives[i];
            if (letter == target_drive) continue;   /* already searched    */
#endif
            char drv_path[NCD_MAX_PATH];
            if (!db_drive_path(letter, drv_path, sizeof(drv_path))) continue;
            if (!platform_file_exists(drv_path)) continue;
            
            drive_letters[drive_count++] = letter;
        }
        
        /* Phase 2: Load all databases */
        NcdDatabase *loaded_dbs[26];
        int loaded_count = 0;
        
        for (int i = 0; i < drive_count; i++) {
            char drv_path[NCD_MAX_PATH];
            if (!db_drive_path(drive_letters[i], drv_path, sizeof(drv_path))) continue;
            
            NcdDatabase *d = db_load_auto(drv_path);
            if (d) {
                /* Filter excluded directories from loaded database */
                if (search_meta && search_meta->exclusions.count > 0) {
                    db_filter_excluded(d, search_meta);
                }
                loaded_dbs[loaded_count++] = d;
            }
        }
        
        /* Phase 3: Search all loaded databases */
        for (int i = 0; i < loaded_count; i++) {
            NcdDatabase *d = loaded_dbs[i];
            int cnt = 0;
            NcdMatch *m;
            
            if (opts.fuzzy_match) {
                m = matcher_find_fuzzy(d, clean_search,
                                       opts.show_hidden, opts.show_system,
                                       &cnt);
            } else {
                m = matcher_find(d, clean_search,
                                 opts.show_hidden, opts.show_system,
                                 &cnt);
            }

            if (m && cnt > 0) {
                NcdMatch *tmp = realloc(matches,
                                        (size_t)(match_count + cnt)
                                        * sizeof(NcdMatch));
                if (tmp) {
                    memcpy(tmp + match_count, m,
                           (size_t)cnt * sizeof(NcdMatch));
                    match_count += cnt;
                    matches = tmp;
                }
                free(m);
            }
        }
        
        /* Phase 4: Free all databases */
        for (int i = 0; i < loaded_count; i++) {
            db_free(loaded_dbs[i]);
        }
        } /* end of non-test-mode fallback block */
    }

    /* Filter matches based on exclusion list */
    if (!is_service_backend() && search_meta && match_count > 0) {
        int filtered_count = filter_matches_by_exclusions(matches, match_count, &search_meta->exclusions);
        if (filtered_count != match_count) {
            match_count = filtered_count;
        }
    }
    
    /* Free locally loaded metadata (not needed anymore) */
    if (search_meta && !is_service_backend()) {
        db_metadata_free(search_meta);
        search_meta = NULL;
    }

    if (!matches || match_count == 0) {
        NCD_DEBUG_LOG("NCD DEBUG: No matches found. primary_db=%p, using_service_db=%d\n",
                (void*)primary_db, using_service_db);
        result_error("NCD: No directory found matching \"%s\".", opts.search);
        if (primary_db && !using_service_db) db_free(primary_db);
        con_close();
        return 1;
    }

    /* Heuristics: if this search term has a prior chosen target, list it first. */
    char preferred_path[NCD_MAX_PATH] = {0};
    if (heur_get_preferred(opts.search, preferred_path, sizeof(preferred_path)))
        heur_promote_match(matches, match_count, preferred_path);

    /* ------------------------------------------------- select a match    */
    int chosen;
    char chosen_path[MAX_PATH] = {0};
    bool chose_custom_path = false;
    if (match_count == 1) {
        chosen = 0;
    } else {
        chosen = ui_select_match_ex(matches, match_count, opts.search,
                                    chosen_path, sizeof(chosen_path));
    }

    if (chosen == -1) {
        /* User cancelled */
        result_cancel();
        free(matches);
        if (primary_db && !using_service_db) db_free(primary_db);
        con_close();
        return 0;   /* not an error -- don't show a message */
    }

    char resolved_path[MAX_PATH] = {0};
    char resolved_drive = '\0';
    if (chosen == -2 && chosen_path[0]) {
        snprintf(resolved_path, sizeof(resolved_path), "%s", chosen_path);
        resolved_drive = (char)toupper((unsigned char)resolved_path[0]);
        chose_custom_path = true;
    } else if (chosen == -2) {
        result_error("NCD: Navigation did not return a valid path.");
        free(matches);
        if (primary_db && !using_service_db) db_free(primary_db);
        con_close();
        return 1;
    } else {
        const NcdMatch *sel = &matches[chosen];
        snprintf(resolved_path, sizeof(resolved_path), "%s", sel->full_path);
        resolved_drive = sel->drive_letter;
    }

    /* ------------------------------------------------ verify drive alive  */
#if NCD_PLATFORM_WINDOWS
    {
        char root[4] = { resolved_drive, ':', NCD_PATH_SEP_CHAR, '\0' };
        UINT dtype = platform_get_drive_type(root);
        if (dtype == PLATFORM_DRIVE_NO_ROOT_DIR || dtype == PLATFORM_DRIVE_UNKNOWN) {
            result_error("Drive %c: is not mounted.", resolved_drive);
            spawn_background_rescan(NULL);
            free(matches);
            if (primary_db && !using_service_db) db_free(primary_db);
            con_close();
            return 2;
        }
    }
#endif

    /* Verify the actual directory still exists */
    if (!platform_dir_exists(resolved_path)) {
        result_error("Directory no longer exists: %s\r\n"
                     "Tip: run 'ncd -r' to refresh the database.",
                     resolved_path);
        spawn_background_rescan(NULL);
        free(matches);
        if (primary_db && !using_service_db) db_free(primary_db);
        con_close();
        return 2;
    }

    /* -------------------------------------------- write success result   */
    heur_note_choice(opts.search, resolved_path);
    add_current_dir_to_history();
    result_ok(resolved_path, resolved_drive);

    free(matches);

    /* --------- schedule background rescan if DB is older than configured interval ---- */
    bool needs_rescan = false;
    int rescan_interval = NCD_RESCAN_HOURS_DEFAULT;  /* default 24 hours */
    
    /* Load configured rescan interval from metadata */
    NcdMetadata *rescan_meta = db_metadata_load();
    if (rescan_meta) {
        if (rescan_meta->cfg.rescan_interval_hours == NCD_RESCAN_NEVER) {
            /* Auto-rescan disabled */
            rescan_interval = -1;
        } else if (rescan_meta->cfg.rescan_interval_hours > 0) {
            /* Use configured interval (already validated to be within bounds) */
            rescan_interval = rescan_meta->cfg.rescan_interval_hours;
        }
        db_metadata_free(rescan_meta);
    }
    
    if (primary_db && primary_db->last_scan > 0 && rescan_interval > 0) {
        time_t age_seconds = time(NULL) - primary_db->last_scan;
        needs_rescan = (age_seconds > (time_t)(rescan_interval * 3600));
    }
    if (!chose_custom_path && needs_rescan) {
        ncd_printf("NCD: Database is over %d hours old -- "
                   "spawning background rescan...\r\n", rescan_interval);
        spawn_background_rescan(NULL);
    }

    /* Cleanup global metadata before exit */
    save_metadata_if_dirty();
    if (g_metadata) {
        db_metadata_free(g_metadata);
        g_metadata = NULL;
    }

    con_close();
    if (primary_db && !using_service_db) db_free(primary_db);
    /* Return exit code 3 if database was rebuilt, otherwise 0 for success */
    return database_was_rebuilt ? 3 : 0;
}
