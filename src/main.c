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

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#endif

#include "ncd.h"
#include "database.h"
#include "scanner.h"
#include "matcher.h"
#include "ui.h"
#include "platform.h"
#include "control_ipc.h"
#include "state_backend.h"

/* forward declarations used by heuristics helpers */
static void ncd_print(const char *s);
static void ncd_println(const char *s);
static void ncd_printf(const char *fmt, ...);

/* ============================================================= state backend */

/*
 * Global state view for service-backed database access.
 * Initialized on first use, closed at exit.
 */
static NcdStateView *g_state_view = NULL;
static NcdStateSourceInfo g_state_info = {0};
static bool g_state_initialized = false;

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
        g_state_view = NULL;
        return false;
    }
    
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
    return state_view_database(g_state_view);
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
    
    NcdMetadata *meta = db_metadata_load();
    if (!meta) meta = db_metadata_create();
    if (!meta) return;
    
    if (db_dir_history_add(meta, cwd, drive)) {
        meta->dir_history_dirty = true;
        db_metadata_save(meta);
    }
    db_metadata_free(meta);
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

/* Helper: Ensure metadata is loaded */
static NcdMetadata* get_metadata(void)
{
    if (!g_metadata) {
        g_metadata = db_metadata_load();
    }
    return g_metadata;
}

/* Helper: Save metadata if dirty */
static void save_metadata_if_dirty(void)
{
    if (g_metadata && (g_metadata->heuristics_dirty || g_metadata->config_dirty)) {
        db_metadata_save(g_metadata);
    }
}

static void heur_sanitize(const char *src, char *dst, size_t dst_size, bool to_lower)
{
    if (!dst || dst_size == 0) return;
    dst[0] = '\0';
    if (!src) return;

    size_t len = strlen(src);
    size_t a = 0;
    while (a < len && isspace((unsigned char)src[a])) a++;
    size_t b = len;
    while (b > a && isspace((unsigned char)src[b - 1])) b--;

    size_t j = 0;
    for (size_t i = a; i < b && j + 1 < dst_size; i++) {
        char c = src[i];
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
        if (to_lower) c = (char)tolower((unsigned char)c);
        dst[j++] = c;
    }
    dst[j] = '\0';
}

static bool heur_get_preferred(const char *search_raw, char *out_path, size_t out_size)
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

static void heur_note_choice(const char *search_raw, const char *target_path)
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

static void heur_promote_match(NcdMatch *matches, int count, const char *preferred_path)
{
    if (!matches || count <= 1 || !preferred_path || !preferred_path[0]) return;
    for (int i = 0; i < count; i++) {
        if (_stricmp(matches[i].full_path, preferred_path) == 0) {
            if (i == 0) return;
            NcdMatch tmp = matches[0];
            matches[0] = matches[i];
            matches[i] = tmp;
            return;
        }
    }
}

static void heur_print(void)
{
    NcdMetadata *meta = get_metadata();
    if (!meta) {
        ncd_println("NCD history: error loading metadata");
        return;
    }
    
    db_heur_print(meta);
}

static void heur_clear(void)
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

/* Write a string (no newline appended). */
static void ncd_print(const char *s)
{
    if (g_con) {
        platform_console_write(g_con, s);
    } else {
        fputs(s, stdout);
        fflush(stdout);
    }
}

/* Write a string followed by \r\n. */
static void ncd_println(const char *s)
{
    ncd_print(s);
    ncd_print("\r\n");
}

/* printf-style wrapper. */
static void ncd_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ncd_print(buf);
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
    ncd_printf("NCD v%s  [built %s]\r\n", NCD_BUILD_VER, NCD_BUILD_STAMP);
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
static void write_result(bool ok, const char *drive, const char *path,
                          const char *message)
{
    char tmp_dir[MAX_PATH] = {0};
    if (!platform_get_temp_path(tmp_dir, sizeof(tmp_dir))) {
        ncd_println("NCD: could not resolve temp path.");
        return;
    }

    char result_path[MAX_PATH];
    snprintf(result_path, sizeof(result_path), "%s%s", tmp_dir, NCD_RESULT_FILE);

    FILE *f = fopen(result_path, "w");
    if (!f) {
        ncd_printf("NCD: could not write result file: %s\r\n", result_path);
        return;
    }

    char safe_drive[64];
    char safe_path[NCD_MAX_PATH * 2];
    char safe_msg[1024];

    const char *src_drive = (ok && drive) ? drive : "";
    const char *src_path  = (ok && path)  ? path  : "";
    const char *src_msg   = message ? message : "";

#if NCD_PLATFORM_WINDOWS
    /*
     * Escape for: @set "VAR=value"
     * Security: Reject control characters (except tab), replace quotes,
     * and filter out percent signs to prevent batch variable expansion attacks.
     */
    size_t j = 0;
    for (size_t i = 0; src_drive[i] && j + 2 < sizeof(safe_drive); i++) {
        char c = src_drive[i];
        /* Reject control characters except common whitespace */
        if ((unsigned char)c < 32 && c != '\t') {
            c = '_';  /* Replace control chars with underscore */
        }
        /* Replace dangerous characters for batch files */
        if (c == '"') c = '\'';      /* Quotes could break out of string */
        if (c == '%') c = '_';       /* Prevent %VAR% expansion */
        if (c == '!') c = '_';       /* Prevent delayed expansion */
        if (c == '\r' || c == '\n') c = ' ';
        safe_drive[j++] = c;
    }
    safe_drive[j] = '\0';

    j = 0;
    for (size_t i = 0; src_path[i] && j + 2 < sizeof(safe_path); i++) {
        char c = src_path[i];
        /* Reject control characters except common whitespace */
        if ((unsigned char)c < 32 && c != '\t') {
            c = '_';
        }
        /* Replace dangerous characters */
        if (c == '"') c = '\'';
        if (c == '%') c = '_';
        if (c == '!') c = '_';
        if (c == '\r' || c == '\n') c = ' ';
        safe_path[j++] = c;
    }
    safe_path[j] = '\0';

    j = 0;
    for (size_t i = 0; src_msg[i] && j + 2 < sizeof(safe_msg); i++) {
        char c = src_msg[i];
        /* Reject control characters except common whitespace */
        if ((unsigned char)c < 32 && c != '\t') {
            c = '_';
        }
        /* Replace dangerous characters */
        if (c == '"') c = '\'';
        if (c == '%') c = '_';
        if (c == '!') c = '_';
        if (c == '\r' || c == '\n') c = ' ';
        safe_msg[j++] = c;
    }
    safe_msg[j] = '\0';

    fprintf(f, "@set \"NCD_STATUS=%s\"\r\n", ok ? "OK" : "ERROR");
    fprintf(f, "@set \"NCD_DRIVE=%s\"\r\n",  safe_drive);
    fprintf(f, "@set \"NCD_PATH=%s\"\r\n",   safe_path);
    fprintf(f, "@set \"NCD_MESSAGE=%s\"\r\n", safe_msg);
#else
    /*
     * Escape for: export VAR='value'
     * Security: Reject control characters and shell metacharacters
     * that could be used for command injection.
     */
    size_t j = 0;
    for (size_t i = 0; src_drive[i] && j + 2 < sizeof(safe_drive); i++) {
        char c = src_drive[i];
        /* Reject control characters */
        if ((unsigned char)c < 32) c = '_';
        /* Reject shell metacharacters */
        if (c == '\'' || c == '"' || c == '$' || c == '`' || 
            c == '\\' || c == '|' || c == '&' || c == ';' ||
            c == '<' || c == '>' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '*' || c == '?') {
            c = '_';
        }
        safe_drive[j++] = c;
    }
    safe_drive[j] = '\0';

    j = 0;
    for (size_t i = 0; src_path[i] && j + 2 < sizeof(safe_path); i++) {
        char c = src_path[i];
        /* Reject control characters */
        if ((unsigned char)c < 32) c = '_';
        /* Reject shell metacharacters */
        if (c == '\'' || c == '"' || c == '$' || c == '`' || 
            c == '\\' || c == '|' || c == '&' || c == ';' ||
            c == '<' || c == '>' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '*' || c == '?') {
            c = '_';
        }
        safe_path[j++] = c;
    }
    safe_path[j] = '\0';

    j = 0;
    for (size_t i = 0; src_msg[i] && j + 2 < sizeof(safe_msg); i++) {
        char c = src_msg[i];
        /* Reject control characters */
        if ((unsigned char)c < 32) c = '_';
        /* Reject shell metacharacters */
        if (c == '\'' || c == '"' || c == '$' || c == '`' || 
            c == '\\' || c == '|' || c == '&' || c == ';' ||
            c == '<' || c == '>' || c == '(' || c == ')' ||
            c == '{' || c == '}' || c == '*' || c == '?') {
            c = '_';
        }
        safe_msg[j++] = c;
    }
    safe_msg[j] = '\0';

    fprintf(f, "NCD_STATUS='%s'\n", ok ? "OK" : "ERROR");
    fprintf(f, "NCD_DRIVE='%s'\n",  safe_drive);
    fprintf(f, "NCD_PATH='%s'\n",   safe_path);
    fprintf(f, "NCD_MESSAGE='%s'\n", safe_msg);
#endif
    fclose(f);
}

static void result_error(const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    write_result(false, "", "", msg);
    ncd_printf("NCD: %s\r\n", msg);
}

static void result_cancel(void)
{
    write_result(false, "", "", "NCD: Cancelled.");
}

static void result_ok(const char *full_path, char drive_letter)
{
    char msg[MAX_PATH + 32];
    snprintf(msg, sizeof(msg), "Changed to %s", full_path);
#if NCD_PLATFORM_WINDOWS
    char drive[3] = { drive_letter, ':', '\0' };
    write_result(true, drive, full_path, msg);
#else
    (void)drive_letter;
    write_result(true, "", full_path, msg);
#endif
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
 * Returns true if service version matches (or no service running).
 * Returns false if there's a version mismatch (service was stopped).
 * Prints appropriate messages to the user.
 */
static bool check_service_version(bool *out_service_was_stopped)
{
    if (out_service_was_stopped) {
        *out_service_was_stopped = false;
    }
    
    if (!ipc_service_exists()) {
        return true;  /* No service running, OK to start new one */
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

/* ============================================================== CLI parse */

static void print_usage(void)
{
    /* Check service status for header line */
    const char *status_suffix;
    bool service_was_stopped = false;
    bool version_ok = check_service_version(&service_was_stopped);
    (void)version_ok;  /* We continue regardless, but log the issue */
    
    bool service_running = ipc_service_exists();
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
        "  ncd <search>                    Search and navigate to a directory\r\n"
        "  ncd [options] <search>          Search with options\r\n"
        "  ncd /r [drives]                 Rescan drives\r\n"
        "  ncd /?                          Show this help\r\n"
        "\r\n"
        "Navigation:\r\n"
        "  <search>      Partial directory name with optional wildcards (*, ?)\r\n"
        "  .             Browse from current directory (navigator mode)\r\n"
        "  X: or X:\\     Browse from drive root (navigator mode)\r\n"
        "  @<group>      Jump to a group/bookmark (see Groups below)\r\n"
        "\r\n"
        "Search Options:\r\n"
        "  /i            Include hidden directories\r\n"
        "  /s            Include system directories\r\n"
        "  /a            Include all (hidden + system)\r\n"
        "  /z            Fuzzy matching (typo-tolerant)\r\n"
        "\r\n"
        "History:\r\n"
        "  /0            Ping-pong: swap between last two directories\r\n"
        "  /1 .. /9      Jump to Nth directory in history (/1=oldest, /9=newest)\r\n"
        "  /h            Browse history (interactive, Del to remove)\r\n"
        "  /hl           List directory history\r\n"
        "  /hc           Clear all directory history\r\n"
        "  /hc#          Remove history entry # (e.g., /hc3)\r\n"
        "  /f            Show frequent searches (last 20)\r\n"
        "  /fc           Clear frequent search history\r\n"
        "\r\n"
        "Groups:\r\n"
        "  /g @<name>    Add current directory to group (e.g., /g @proj)\r\n"
        "  /g- @<name>   Remove current directory from group\r\n"
        "  /gl           List all groups and their directories\r\n"
        "\r\n"
        "Scanning:\r\n"
        "  /r            Rescan all drives\r\n"
        "  /r.           Rescan current subdirectory only\r\n"
        "  /rBDE         Rescan specific drives (B:, D:, E:)\r\n"
        "  /r-b-d        Rescan all drives except B: and D:\r\n"
        "\r\n"
        "Exclusions:\r\n"
        "  -x <pat>      Add exclusion pattern (e.g., -x C:Windows)\r\n"
        "  -x- <pat>     Remove exclusion pattern\r\n"
        "  -xl           List exclusion patterns\r\n"
        "\r\n"
        "Configuration:\r\n"
        "  /c            Edit default options interactively\r\n"
        "  /d <path>     Use alternate database file\r\n"
        "  /t <sec>      Scan timeout in seconds (default: 300)\r\n"
        "  /retry <n>    Service busy retry count (0-255, 0=use config default)\r\n"
        "  /v            Print version\r\n"
        "\r\n"
        "Agent Mode:\r\n"
        "  /agent        LLM integration mode (run 'ncd /agent' for details)\r\n",
        platform_get_app_title(), status_suffix);
    
    /* Platform-specific suffix */
    char suffix_buf[512];
    if (platform_write_help_suffix(suffix_buf, sizeof(suffix_buf)) > 0) {
        ncd_print(suffix_buf);
    }
    
    ncd_print(
        "\r\n"
        "Examples:\r\n"
        "  ncd downloads              Search for \"downloads\"\r\n"
        "  ncd scott\\downloads        Search with path context\r\n"
        "  ncd down*                  Wildcard: matches downloads, downgrade, etc.\r\n"
        "  ncd *load*                 Wildcard: matches downloads, uploads, etc.\r\n"
        "  ncd /z donloads            Fuzzy: matches \"downloads\" despite typo\r\n"
        "  ncd .                      Browse current directory tree\r\n"
        "  ncd /g @proj               Add current dir to @proj group\r\n"
        "  ncd @proj                  Jump to @proj group\r\n"
        "  ncd /0                     Swap to previous directory\r\n"
        "  ncd /r                     Rescan all drives\r\n"
        "  ncd /rBDE                  Rescan drives B:, D:, E: only\r\n"
        "\r\n"
        "Tab Completion:\r\n"
        "  Bash:    source completions/ncd.bash\r\n"
        "  Zsh:     Add completions/_ncd to your fpath\r\n"
        "  PowerShell: . completions/ncd.ps1\r\n"
    );
}

static bool parse_drive_list_token(const char *tok, bool *mask, int *count)
{
    if (!tok || !tok[0] || !mask || !count) return false;

    bool saw_sep = false;
    bool saw_letter = false;
    int i = 0;
    while (tok[i]) {
        char c = tok[i];
        if (isalpha((unsigned char)c)) {
            char u = (char)toupper((unsigned char)c);
            int idx = u - 'A';
            if (!mask[idx]) {
                mask[idx] = true;
                (*count)++;
            }
            saw_letter = true;
            i++;
            if (tok[i] == ':') i++;  /* optional colon after drive letter */
            continue;
        }
        if (c == ',' || c == '-') {
            saw_sep = true;
            i++;
            continue;
        }
        return false;
    }
    return saw_letter && saw_sep;
}

/* Forward declaration for agent mode argument parsing */
static bool parse_agent_args(int argc, char *argv[], int *consumed, NcdOptions *opts);

static bool parse_args(int argc, char *argv[], NcdOptions *opts)
{
    memset(opts, 0, sizeof(NcdOptions));
    opts->timeout_seconds = 300;  /* default 5 minute timeout */

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        /* History browse: /h - must be checked BEFORE help (/h is not help!) */
        if (_stricmp(arg, "/h") == 0 || _stricmp(arg, "-h") == 0) {
            opts->history_browse = true;
            continue;
        }

        /* Explicit help commands - note: /h is history browse, not help */
        if (_stricmp(arg, "/?") == 0 || _stricmp(arg, "-?") == 0 ||
            _stricmp(arg, "--help") == 0) {
            opts->show_help = true;
            continue;
        }

        /* Explicit frequent-history commands */
        if (_stricmp(arg, "/f") == 0 || _stricmp(arg, "-f") == 0) {
            opts->show_history = true;
            continue;
        }
        if (_stricmp(arg, "/fc") == 0 || _stricmp(arg, "-fc") == 0) {
            opts->clear_history = true;
            continue;
        }

        /* Directory history ping-pong: /0 */
        if (_stricmp(arg, "/0") == 0 || _stricmp(arg, "-0") == 0) {
            opts->history_pingpong = true;
            continue;
        }
        
        /* Directory history jump: /1 to /9 */
        if ((arg[0] == '/' || arg[0] == '-') && 
            arg[1] >= '1' && arg[1] <= '9' && arg[2] == '\0') {
            opts->history_index = arg[1] - '0';  /* 1-9 */
            continue;
        }
        
        /* History list: /hl */
        if (_stricmp(arg, "/hl") == 0 || _stricmp(arg, "-hl") == 0) {
            opts->history_list = true;
            continue;
        }
        
        /* History clear or remove by index: /hc or /hc# */
        if ((_strnicmp(arg, "/hc", 3) == 0 || _strnicmp(arg, "-hc", 3) == 0)) {
            const char *rest = arg + 3;
            if (*rest == '\0') {
                /* /hc - clear all */
                opts->history_clear = true;
            } else if (isdigit((unsigned char)*rest) && rest[1] == '\0') {
                /* /hc# - remove specific entry */
                int idx = *rest - '0';
                if (idx >= 1 && idx <= 9) {
                    opts->history_remove = idx;
                } else {
                    ncd_println("NCD: /hc# index must be 1-9");
                    return false;
                }
            } else {
                ncd_println("NCD: invalid history command");
                return false;
            }
            continue;
        }
        
        /* History remove by index (alternate form): /h-# (hidden, for testing) */
        if ((_strnicmp(arg, "/h-", 3) == 0 || _strnicmp(arg, "-h-", 3) == 0)) {
            const char *rest = arg + 3;
            if (isdigit((unsigned char)*rest) && rest[1] == '\0') {
                int idx = *rest - '0';
                if (idx >= 1 && idx <= 9) {
                    opts->history_remove = idx;
                } else {
                    ncd_println("NCD: /h-# index must be 1-9");
                    return false;
                }
            } else {
                ncd_println("NCD: invalid history command");
                return false;
            }
            continue;
        }

        /*
         * /r <drives> form:
         *   ncd /r e,p
         *   ncd /r e-p
         *   ncd /r /      (Linux: scan only root, not /mnt drives)
         * Only treat next arg as drive list if it looks like one (commas/hyphens).
         * A bare letter like "c" is treated as search, not drive "C:".
         */
        if ((_stricmp(arg, "/r") == 0 || _stricmp(arg, "-r") == 0) &&
            i + 1 < argc &&
            (argv[i + 1][0] != '-' &&
             (argv[i + 1][0] != '/' || strcmp(argv[i + 1], "/") == 0))) {
            const char *next = argv[i + 1];
#if NCD_PLATFORM_LINUX
            /* Linux special: /r / scans only root filesystem */
            if (strcmp(next, "/") == 0) {
                opts->force_rescan = true;
                opts->scan_root_only = true;
                i++;  /* consume the '/' token */
                continue;
            }
#endif
            bool looks_like_drive_list = false;
            for (int k = 0; next[k]; k++) {
                if (next[k] == ',' || next[k] == '-') {
                    looks_like_drive_list = true;
                    break;
                }
            }
            if (looks_like_drive_list) {
                bool parsed = parse_drive_list_token(next,
                                                     opts->scan_drive_mask,
                                                     &opts->scan_drive_count);
                if (parsed) {
                    opts->force_rescan = true;
                    i++;  /* consume drive-list token */
                    continue;
                }
            }
            /* Not a drive list - fall through, next arg is search term */
        }

        /*
         * /r<drives> shorthand:
         *   /r      => all drives
         *   /rBDE   => B:, D:, E:
         */
        if ((arg[0] == '/' || arg[0] == '-') &&
            (arg[1] == 'r' || arg[1] == 'R') &&
            arg[2] == '-') {
            /* /r-b-d or /r-b,d => skip listed drives */
            opts->force_rescan = true;
            int k = 2;
            while (arg[k]) {
                if (arg[k] != '-' && arg[k] != ',') {
                    ncd_printf("NCD: invalid /r exclude list: %s\r\n", arg);
                    return false;
                }
                k++;
                if (!arg[k] || !isalpha((unsigned char)arg[k])) {
                    ncd_printf("NCD: invalid /r exclude list: %s\r\n", arg);
                    return false;
                }
                char c = (char)toupper((unsigned char)arg[k]);
                int idx = c - 'A';
                if (!opts->skip_drive_mask[idx]) {
                    opts->skip_drive_mask[idx] = true;
                    opts->skip_drive_count++;
                }
                k++;
            }
            continue;
        }

        if ((arg[0] == '/' || arg[0] == '-') &&
            (arg[1] == 'r' || arg[1] == 'R') &&
            arg[2] != '\0') {
            bool drives_form = true;
            for (int k = 2; arg[k]; k++) {
                if (!isalpha((unsigned char)arg[k])) {
                    drives_form = false;
                    break;
                }
            }
            if (!drives_form) {
                /* Check for /r. (rescan current subdirectory) */
                if (arg[2] == '.' && arg[3] == '\0') {
                    opts->force_rescan = true;
                    platform_get_current_dir(opts->scan_subdirectory, NCD_MAX_PATH);
                    continue;
                }
                /* Fall through to normal option parsing (/ri, /rs, etc). */
            } else {
            opts->force_rescan = true;
            for (int k = 2; arg[k]; k++) {
                char c = (char)toupper((unsigned char)arg[k]);
                int idx = c - 'A';
                if (!opts->scan_drive_mask[idx]) {
                    opts->scan_drive_mask[idx] = true;
                    opts->scan_drive_count++;
                }
            }
            continue;
            }
        }

        /* /r . form: rescan current subdirectory */
        if ((arg[0] == '/' || arg[0] == '-') &&
            (arg[1] == 'r' || arg[1] == 'R') &&
            arg[2] == '\0' && i + 1 < argc) {
            const char *next = argv[i + 1];
            if (strcmp(next, ".") == 0) {
                opts->force_rescan = true;
                platform_get_current_dir(opts->scan_subdirectory, NCD_MAX_PATH);
                i++;  /* consume the '.' */
                continue;
            }
        }

        /* Tag list: /gl or /gL */
        if ((_stricmp(arg, "/gl") == 0 || _stricmp(arg, "-gl") == 0 ||
             _stricmp(arg, "/gL") == 0 || _stricmp(arg, "-gL") == 0)) {
            opts->group_list = true;
            continue;
        }
        
        /* Tag remove: /g- <tag> */
        if ((_stricmp(arg, "/g-") == 0 || _stricmp(arg, "-g-") == 0)) {
            if (i + 1 < argc) {
                i++;
                platform_strncpy_s(opts->group_name, sizeof(opts->group_name), argv[i]);
                opts->group_remove = true;
                continue;
            } else {
                ncd_println("NCD: /g- requires a tag name");
                return false;
            }
        }
        
        /* Tag set: /g <tag> */
        if ((_stricmp(arg, "/g") == 0 || _stricmp(arg, "-g") == 0)) {
            if (i + 1 < argc) {
                i++;
                platform_strncpy_s(opts->group_name, sizeof(opts->group_name), argv[i]);
                opts->group_set = true;
                continue;
            } else {
                ncd_println("NCD: /g requires a tag name");
                return false;
            }
        }
        
        /* Configuration editor: /c */
        if ((_stricmp(arg, "/c") == 0 || _stricmp(arg, "-c") == 0)) {
            opts->config_edit = true;
            continue;
        }
        
        /* Exclusion list: -xl */
        if ((_stricmp(arg, "-xl") == 0 || _stricmp(arg, "/xl") == 0)) {
            opts->exclusion_list = true;
            continue;
        }
        
        /* Exclusion remove: -x- <pattern> */
        if ((_stricmp(arg, "-x-") == 0 || _stricmp(arg, "/x-") == 0)) {
            if (i + 1 < argc) {
                i++;
                platform_strncpy_s(opts->exclusion_pattern, sizeof(opts->exclusion_pattern), argv[i]);
                opts->exclusion_remove = true;
                continue;
            } else {
                ncd_println("NCD: -x- requires a pattern");
                return false;
            }
        }
        
        /* Exclusion add: -x <pattern> */
        if ((_stricmp(arg, "-x") == 0 || _stricmp(arg, "/x") == 0)) {
            if (i + 1 < argc) {
                i++;
                platform_strncpy_s(opts->exclusion_pattern, sizeof(opts->exclusion_pattern), argv[i]);
                opts->exclusion_add = true;
                continue;
            } else {
                ncd_println("NCD: -x requires a pattern");
                return false;
            }
        }

#if DEBUG
        /* Hidden test options (debug builds only, not documented) */
        if ((_stricmp(arg, "/test") == 0 || _stricmp(arg, "-test") == 0)) {
            if (i + 1 < argc) {
                i++;
                const char *test_opt = argv[i];
                if (_stricmp(test_opt, "NC") == 0) {
                    opts->test_no_checksum = true;
                } else if (_stricmp(test_opt, "SL") == 0) {
                    opts->test_slow_mode = true;
                } else {
                    ncd_printf("NCD: unknown /test option: %s\r\n", test_opt);
                    return false;
                }
                continue;
            } else {
                ncd_println("NCD: /test requires an option (NC, SL)");
                return false;
            }
        }
#endif
        
        /* Agent mode: /agent <subcommand> */
        if (_stricmp(arg, "/agent") == 0 || _stricmp(arg, "-agent") == 0 ||
            _stricmp(arg, "--agent") == 0) {
            opts->agent_mode = true;
            int consumed = 0;
            if (!parse_agent_args(argc - i, &argv[i], &consumed, opts))
                return false;
            i += consumed;
            continue;
        }
        
        /* Options start with / or - */
        if ((arg[0] == '/' || arg[0] == '-') && arg[1]) {
            /* Multi-char flags after / are processed char by char */
            for (int j = 1; arg[j]; j++) {
                char flag = (char)tolower((unsigned char)arg[j]);
                switch (flag) {
                    case 'r': opts->force_rescan  = true; break;
                    case 'v': opts->show_version  = true; break;
                    case 'i': opts->show_hidden   = true; break;
                    case 's': opts->show_system   = true; break;
                    case 'a': opts->show_hidden   = true;
                              opts->show_system   = true; break;
                    case 'z': opts->fuzzy_match   = true; break;
                    case 'c': opts->config_edit   = true; break;
                    case 'd':
                        /* /d requires a path as the next argument */
                        if (arg[j + 1] == '\0' && i + 1 < argc) {
                            i++;
                            platform_strncpy_s(opts->db_override, NCD_MAX_PATH, argv[i]);
                            goto next_arg;
                        } else if (arg[j + 1]) {
                            /* /d<path> (no space) */
                            platform_strncpy_s(opts->db_override, NCD_MAX_PATH, arg + j + 1);
                            goto next_arg;
                        } else {
                            ncd_println("NCD: /d requires a path argument");
                            return false;
                        }
                    case 't':
                        /* /t requires seconds as the next argument */
                        if (arg[j + 1] == '\0' && i + 1 < argc) {
                            i++;
                            opts->timeout_seconds = atoi(argv[i]);
                            if (opts->timeout_seconds <= 0) opts->timeout_seconds = 300;
                            goto next_arg;
                        } else if (arg[j + 1]) {
                            /* /t<seconds> (no space) */
                            opts->timeout_seconds = atoi(arg + j + 1);
                            if (opts->timeout_seconds <= 0) opts->timeout_seconds = 300;
                            goto next_arg;
                        } else {
                            ncd_println("NCD: /t requires a timeout in seconds");
                            return false;
                        }
                    default:
                        /* Check for /retry option */
                        if (_strnicmp(arg + j, "retry", 5) == 0) {
                            const char *val = arg + j + 5;
                            if (*val == '\0' && i + 1 < argc) {
                                i++;
                                opts->service_retry_count = atoi(argv[i]);
                                opts->service_retry_set = true;
                                goto next_arg;
                            } else if (*val) {
                                opts->service_retry_count = atoi(val);
                                opts->service_retry_set = true;
                                goto next_arg;
                            } else {
                                ncd_println("NCD: /retry requires a count (0-255, 0=use config default)");
                                return false;
                            }
                        }
                        ncd_printf("NCD: unknown option /%c\r\n", flag);
                        return false;
                }
            }
        } else {
            /* Not an option -- must be the search string */
            if (opts->has_search) {
                /* Append with backslash (allows: ncd scott downloads) */
                platform_strncat_s(opts->search, NCD_MAX_PATH, "\\");
                platform_strncat_s(opts->search, NCD_MAX_PATH, arg);
                } else {
                platform_strncpy_s(opts->search, NCD_MAX_PATH, arg);
                opts->has_search = true;
            }
        }
next_arg:;
    }
    return true;
}

/* Agent subcommand identifiers */
#define AGENT_SUB_NONE     0
#define AGENT_SUB_QUERY    1
#define AGENT_SUB_LS       2
#define AGENT_SUB_TREE     3
#define AGENT_SUB_CHECK    4
#define AGENT_SUB_COMPLETE 5
#define AGENT_SUB_MKDIR    6
#define AGENT_SUB_QUIT     7

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
        "  ncd /agent query <search> [options]\r\n"
        "  ncd /agent ls <path> [options]\r\n"
        "  ncd /agent tree <path> [options]\r\n"
        "  ncd /agent check <path> | --db-age | --stats | --service-status\r\n"
        "  ncd /agent complete <partial> [--limit N]\r\n"
        "\r\n"
        "Commands:\r\n"
        "  query <search>    Search the NCD index for directories\r\n"
        "  ls <path>         List directory contents (live filesystem)\r\n"
        "  tree <path>       Show directory structure from DB\r\n"
        "  check <path>      Check if path exists in DB (exit code)\r\n"
        "  complete <partial>  Shell tab-completion candidates\r\n"
        "  mkdir <path>        Create a directory and add to database\r\n"
        "  quit                Request graceful service shutdown\r\n"
        "\r\n"
        "Query Options:\r\n"
        "  --json            JSON output instead of compact\r\n"
        "  --limit N         Cap results (default: 20, 0=unlimited)\r\n"
        "  --all             Include hidden + system directories\r\n"
        "  --depth           Sort shallowest first (default: by score)\r\n"
        "\r\n"
        "ls Options:\r\n"
        "  --depth N         Recurse N levels (default: 1, max: 5)\r\n"
        "  --dirs-only       Only directories\r\n"
        "  --files-only      Only files\r\n"
        "  --pattern <glob>  Filter by name (e.g., *.py)\r\n"
        "  --json            JSON output\r\n"
        "\r\n"
        "tree Options:\r\n"
        "  --depth N         Max depth (default: 3, max: 10)\r\n"
        "  --json            Nested JSON structure\r\n"
        "  --flat            Flat list instead of indented\r\n"
        "\r\n"
        "check Options:\r\n"
        "  --db-age          Output DB age in seconds\r\n"
        "  --stats           Output dir count per drive\r\n"
        "  --service-status  Check if NCD service is running and ready\r\n"
        "\r\n"
        "mkdir Options:\r\n"
        "  --json            JSON output with result code\r\n"
        "\r\n"
        "Exit Codes:\r\n"
        "  0   Success / Found\r\n"
        "  1   Not found / Error\r\n"
    );
}

static bool parse_agent_args(int argc, char *argv[], int *consumed, NcdOptions *opts)
{
    /* Initialize defaults */
    opts->agent_limit = 20;
    opts->agent_depth = -1;  /* -1 means use subcommand default */
    
    if (argc < 2) {
        ncd_println("NCD: /agent requires a subcommand");
        return false;
    }
    
    const char *sub = argv[1];
    *consumed = 1;
    
    if (_stricmp(sub, "query") == 0) {
        if (argc < 3) {
            ncd_println("NCD: /agent query requires a search term");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_QUERY;
        platform_strncpy_s(opts->search, sizeof(opts->search), argv[2]);
        opts->has_search = true;
        *consumed = 2;
        
        /* Parse query options */
        for (int i = 3; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--limit") == 0 && i + 1 < argc) {
                opts->agent_limit = atoi(argv[++i]);
                *consumed += 2;
            } else if (strcmp(opt, "--all") == 0) {
                opts->show_hidden = true;
                opts->show_system = true;
                (*consumed)++;
            } else if (strcmp(opt, "--depth") == 0) {
                opts->agent_depth_sort = true;
                (*consumed)++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "ls") == 0) {
        if (argc < 3) {
            ncd_println("NCD: /agent ls requires a path");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_LS;
        platform_strncpy_s(opts->search, sizeof(opts->search), argv[2]);
        opts->has_search = true;
        *consumed = 2;
        opts->agent_depth = 1;  /* default for ls */
        
        /* Parse ls options */
        for (int i = 3; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--depth") == 0 && i + 1 < argc) {
                opts->agent_depth = atoi(argv[++i]);
                if (opts->agent_depth < 1) opts->agent_depth = 1;
                if (opts->agent_depth > 5) opts->agent_depth = 5;
                *consumed += 2;
            } else if (strcmp(opt, "--dirs-only") == 0) {
                opts->agent_dirs_only = true;
                (*consumed)++;
            } else if (strcmp(opt, "--files-only") == 0) {
                opts->agent_files_only = true;
                (*consumed)++;
            } else if (strcmp(opt, "--pattern") == 0 && i + 1 < argc) {
                platform_strncpy_s(opts->agent_pattern, sizeof(opts->agent_pattern), argv[++i]);
                *consumed += 2;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "tree") == 0) {
        if (argc < 3) {
            ncd_println("NCD: /agent tree requires a path");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_TREE;
        platform_strncpy_s(opts->search, sizeof(opts->search), argv[2]);
        opts->has_search = true;
        *consumed = 2;
        opts->agent_depth = 3;  /* default for tree */
        
        /* Parse tree options */
        for (int i = 3; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--depth") == 0 && i + 1 < argc) {
                opts->agent_depth = atoi(argv[++i]);
                if (opts->agent_depth < 1) opts->agent_depth = 1;
                if (opts->agent_depth > 10) opts->agent_depth = 10;
                *consumed += 2;
            } else if (strcmp(opt, "--flat") == 0) {
                opts->agent_flat = true;
                (*consumed)++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "check") == 0) {
        opts->agent_subcommand = AGENT_SUB_CHECK;
        
        /* Parse check options */
        for (int i = 2; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--db-age") == 0) {
                opts->agent_check_db_age = true;
                (*consumed)++;
            } else if (strcmp(opt, "--stats") == 0) {
                opts->agent_check_stats = true;
                (*consumed)++;
            } else if (strcmp(opt, "--service-status") == 0) {
                opts->agent_check_service_status = true;
                (*consumed)++;
            } else if (opt[0] != '-') {
                /* First non-option is the path */
                platform_strncpy_s(opts->search, sizeof(opts->search), opt);
                opts->has_search = true;
                (*consumed)++;
            } else {
                break;
            }
        }
        
        /* check requires at least one of: path, --db-age, --stats, --service-status */
        if (!opts->has_search && !opts->agent_check_db_age && !opts->agent_check_stats && !opts->agent_check_service_status) {
            ncd_println("NCD: /agent check requires a path or --db-age or --stats or --service-status");
            return false;
        }
        return true;
        
    } else if (_stricmp(sub, "complete") == 0) {
        opts->agent_subcommand = AGENT_SUB_COMPLETE;
        
        /* Parse complete options */
        for (int i = 2; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--limit") == 0 && i + 1 < argc) {
                opts->agent_limit = atoi(argv[++i]);
                *consumed += 2;
            } else if (opt[0] != '-') {
                /* First non-option is the partial text to complete */
                platform_strncpy_s(opts->search, sizeof(opts->search), opt);
                opts->has_search = true;
                (*consumed)++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "mkdir") == 0) {
        if (argc < 3) {
            ncd_println("NCD: /agent mkdir requires a path");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_MKDIR;
        platform_strncpy_s(opts->search, sizeof(opts->search), argv[2]);
        opts->has_search = true;
        *consumed = 2;
        
        /* Parse mkdir options */
        for (int i = 3; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "quit") == 0) {
        opts->agent_subcommand = AGENT_SUB_QUIT;
        *consumed = 1;
        return true;
        
    } else {
        ncd_printf("NCD: unknown /agent subcommand: %s\r\n", sub);
        return false;
    }
}

/* Simple glob matching for agent pattern filtering */
static bool glob_match(const char *pattern, const char *text)
{
    const char *p = pattern;
    const char *t = text;
    const char *star = NULL;
    const char *ss = NULL;
    
    while (*t) {
        if (*p == '*') {
            star = p++;
            ss = t;
        } else if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*t)) {
            p++;
            t++;
        } else if (star) {
            p = star + 1;
            t = ++ss;
        } else {
            return false;
        }
    }
    
    while (*p == '*') p++;
    return *p == '\0';
}

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
    
    /* Sort by depth if requested */
    if (opts->agent_depth_sort) {
        /* Simple bubble sort by path depth (slash count) */
        for (int i = 0; i < limit - 1; i++) {
            for (int j = i + 1; j < limit; j++) {
                int depth_i = 0, depth_j = 0;
                for (const char *p = matches[i].full_path; *p; p++)
                    if (*p == '\\' || *p == '/') depth_i++;
                for (const char *p = matches[j].full_path; *p; p++)
                    if (*p == '\\' || *p == '/') depth_j++;
                if (depth_i > depth_j) {
                    NcdMatch tmp = matches[i];
                    matches[i] = matches[j];
                    matches[j] = tmp;
                }
            }
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
    
    /* Ensure trailing separator for consistent depth calculation */
    size_t len = strlen(path);
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
        
        /* Get file stats to determine if directory */
        char full_path[NCD_MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s%s", path, name);
        struct stat st;
        bool is_dir = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
        
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
        platform_strncpy_s(e->name, sizeof(e->name), name, sizeof(e->name));
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
                    
                    char full_path[NCD_MAX_PATH];
                    snprintf(full_path, sizeof(full_path), "%s%s", sub_path, name);
                    struct stat st;
                    bool is_dir = (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode));
                    
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
                    platform_strncpy_s(e->name, sizeof(e->name), name, sizeof(e->name));
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
    root_len = snprintf(path, sizeof(path), "%c:%c", drive_letter, NCD_PATH_SEP[0]);
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
            if (root_len + name_len >= sizeof(path))
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
        if (path_len + 1 + name_len >= sizeof(path))
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
    for (char *p = search_path; *p; p++) {
        if (*p == '/') *p = '\\';
    }
    
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
        snprintf(drive_root, sizeof(drive_root), "%c:%c", drive->letter, NCD_PATH_SEP[0]);
#else
        snprintf(drive_root, sizeof(drive_root), "%s", drive->label);
#endif
        
        if (_strnicmp(search_path, drive_root, strlen(drive_root)) == 0) {
            found_drive_idx = d;
            found_dir_idx = path_map_get(&path_map, search_path);
            if (found_dir_idx >= 0)
                break;
        }
    }
    
    path_map_free(&path_map);
    
    if (found_dir_idx < 0) {
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
    
    DriveData *drive = &db->drives[found_drive_idx];
    
    while (qhead < qtail) {
        QueueItem cur = queue[qhead++];
        DirChildren *dc = &children_idx[cur.dir];
        
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
                if (len + name_len < sizeof(e->full_path) - 1) {
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
                if (len + name_len < sizeof(e->full_path) - 1) {
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
    
    if (opts->agent_json) {
        agent_print("{\"v\":1,\"tree\":");
        /* Build nested structure - simplified flat array for now */
        agent_print("[");
        for (int i = 0; i < entry_count; i++) {
            if (i > 0) agent_print(",");
            agent_print("{\"n\":\"");
            /* For JSON, extract just the name from the full path */
            const char *name_only = strrchr(entries[i].full_path, NCD_PATH_SEP[0]);
            if (name_only) name_only++;
            else name_only = entries[i].full_path;
            agent_json_escape(name_only);
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
            for (char *p = search_path; *p; p++)
                if (*p == '/') *p = '\\';
            
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
        /* Check service status */
        bool service_running = ipc_service_exists();
        
        if (!service_running) {
            /* Service not running */
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"status\":\"not_running\",\"message\":\"Service not running\"}\r\n");
            } else {
                agent_print("NOT_RUNNING\r\n");
            }
            return 0;
        }
        
        /* Service is running - check if it has loaded databases */
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
        
        NcdIpcStateInfo info;
        NcdIpcResult result = ipc_client_get_state_info(client, &info);
        ipc_client_disconnect(client);
        
        if (result != NCD_IPC_OK || info.db_generation == 0) {
            /* Service running but database not loaded yet */
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"status\":\"starting\",\"message\":\"Service running but not loaded\"}\r\n");
            } else {
                agent_print("STARTING\r\n");
            }
            return 0;
        }
        
        /* Service ready - running and has loaded databases */
        if (opts->agent_json) {
            agent_printf("{\"v\":1,\"status\":\"ready\",\"message\":\"Service ready\",\"meta_generation\":%llu,\"db_generation\":%llu}\r\n",
                (unsigned long long)info.meta_generation,
                (unsigned long long)info.db_generation);
        } else {
            agent_print("READY\r\n");
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
            result = AGENT_MKDIR_ERROR_PARENT;
            platform_strncpy_s(result_msg, sizeof(result_msg), "Parent directory does not exist");
        } else {
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
    }
    
    /* If successful, add to database */
    if (result == AGENT_MKDIR_OK || result == AGENT_MKDIR_EXISTS) {
        char target_drive = path_get_drive(path);
        if (target_drive == 0 && path[0] != '\0') {
            /* Try to get drive from first character */
            target_drive = (char)toupper((unsigned char)path[0]);
        }
        
        if (target_drive != 0) {
            /* Load existing database for this drive */
            char target_db[NCD_MAX_PATH] = {0};
            NcdDatabase *db = NULL;
            
            if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                db = db_load_auto(target_db);
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
            
            /* Add the directory to the database */
            if (drv) {
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
                    
                    /* Save the database */
                    char drv_path[NCD_MAX_PATH];
                    if (db_drive_path(target_drive, drv_path, sizeof(drv_path))) {
                        db_save_binary_single(db, 0, drv_path);
                    }
                }
            }
            
            if (db) db_free(db);
        }
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
    /* Handle subdirectory rescan: /r. */
    if (opts->scan_subdirectory[0]) {
        char drive = platform_get_drive_letter(opts->scan_subdirectory);
        ncd_printf("NCD: Rescanning subdirectory %s...\r\n", opts->scan_subdirectory);
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
        result_error("Invalid command line.  Run 'ncd /?' for help.");
        con_close();
        return 1;
    }

#if DEBUG
    /* Set debug test flags in database module */
    extern bool g_test_no_checksum;
    extern bool g_test_slow_mode;
    g_test_no_checksum = opts.test_no_checksum;
    g_test_slow_mode = opts.test_slow_mode;
#endif

    /* ------------------------------------------------ first-run configuration */
    /* If no config file exists and user didn't specify /c, prompt for configuration */
    if (!db_metadata_exists() && !opts.config_edit && !opts.show_help && 
        !opts.group_list && !opts.group_set && !opts.group_remove &&
        !opts.show_history && !opts.clear_history && !opts.show_version &&
        !opts.force_rescan && !opts.has_search &&
        !opts.history_pingpong && opts.history_index == 0 && !opts.history_browse && 
        !opts.history_list && !opts.history_clear && opts.history_remove == 0) {
        ncd_println("Welcome to NCD! Let's set up your default options.");
        ncd_println("(Use 'ncd /c' anytime to change these settings)\r\n");
        
        NcdMetadata *meta = db_metadata_create();
        db_config_init_defaults(&meta->cfg);
        db_exclusion_init_defaults(meta);
        
        if (ui_edit_config(meta)) {
            meta->config_dirty = true;
            if (db_metadata_save(meta)) {
                ncd_println("Configuration saved.\r\n");
            } else {
                ncd_println("Warning: Could not save configuration.\r\n");
            }
        } else {
            ncd_println("Using default settings. (Run 'ncd /c' to configure later)\r\n");
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
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        /* Get current directory for targeted removal */
        char cwd[MAX_PATH] = {0};
        bool have_cwd = platform_get_current_dir(cwd, sizeof(cwd));
        
        /* Check if current directory is in the group */
        bool in_group = false;
        if (have_cwd) {
            const NcdGroupEntry *entry = NULL;
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
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        if (db_exclusion_remove(meta, opts.exclusion_pattern)) {
            db_metadata_save(meta);
            ncd_printf("Removed exclusion: %s\r\n", opts.exclusion_pattern);
        } else {
            ncd_printf("Exclusion not found: %s\r\n", opts.exclusion_pattern);
        }
        db_metadata_free(meta);
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ exclusion add command */
    if (opts.exclusion_add) {
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
        NcdMetadata *meta = db_metadata_load();
        if (meta) {
            db_dir_history_clear(meta);
            db_metadata_save(meta);
            ncd_println("Directory history cleared.");
            db_metadata_free(meta);
        }
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history remove by index */
    if (opts.history_remove > 0) {
        NcdMetadata *meta = db_metadata_load();
        if (!meta) {
            ncd_println("No history.");
            con_close();
            return 1;
        }
        
        int idx = opts.history_remove - 1;  /* convert 1-based to 0-based */
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
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history browse */
    if (opts.history_browse) {
        NcdMetadata *meta = db_metadata_load();
        if (!meta || db_dir_history_count(meta) == 0) {
            ncd_println("No history entries.");
            if (meta) db_metadata_free(meta);
            con_close();
            return 0;
        }
        
        /* Build match array from history entries */
        int count = db_dir_history_count(meta);
        NcdMatch *matches = ncd_malloc(sizeof(NcdMatch) * count);
        int valid = 0;
        
        for (int i = 0; i < count; i++) {
            const NcdDirHistoryEntry *e = db_dir_history_get(meta, i);
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
            db_metadata_free(meta);
            con_close();
            return 0;
        }
        
        int chosen;
        if (valid == 1) {
            chosen = 0;
        } else {
            chosen = ui_select_history(matches, &valid, meta);
            /* Note: valid may have decreased if user deleted entries */
        }
        
        if (chosen >= 0 && chosen < valid) {
            add_current_dir_to_history();
            result_ok(matches[chosen].full_path, matches[chosen].drive_letter);
        } else {
            result_cancel();
        }
        
        free(matches);
        db_metadata_free(meta);
        con_close();
        return 0;
    }
    
    /* ------------------------------------------------ directory history ping-pong */
    if (opts.history_pingpong || (!opts.has_search && argc == 1)) {
        /* /0 or no args: ping-pong between [0] and [1], swapping them */
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        if (db_dir_history_count(meta) < 2) {
            ncd_println("NCD: Not enough history entries to ping-pong.\r\n");
            ncd_println("Navigate to a directory first with 'ncd <search>'.\r\n");
            db_metadata_free(meta);
            con_close();
            return 1;
        }
        
        /* Get the second entry (will become first after swap) */
        const NcdDirHistoryEntry *entry = db_dir_history_get(meta, 1);
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
        
        /* Swap first two entries */
        db_dir_history_swap_first_two(meta);
        meta->dir_history_dirty = true;
        db_metadata_save(meta);
        
        /* Return the (now) first entry */
        result_ok(entry->path, entry->drive);
        db_metadata_free(meta);
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
                char cwd[MAX_PATH] = {0};
                platform_get_current_dir(cwd, sizeof(cwd));
                char target_drive = (char)toupper((unsigned char)cwd[0]);
                
                if (isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
                    target_drive = (char)toupper((unsigned char)opts.search[0]);
                }
                
                if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                    db = db_load_auto(target_db);
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
                /* Load database for tree */
                NcdDatabase *db = NULL;
                char target_db[NCD_MAX_PATH] = {0};
                char cwd[MAX_PATH] = {0};
                platform_get_current_dir(cwd, sizeof(cwd));
                char target_drive = (char)toupper((unsigned char)cwd[0]);
                
                if (isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
                    target_drive = (char)toupper((unsigned char)opts.search[0]);
                }
                
                if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                    db = db_load_auto(target_db);
                }
                
                int count = agent_mode_tree(db, &opts);
                result = (count > 0) ? 0 : 1;
                if (db) db_free(db);
                break;
            }
            case AGENT_SUB_CHECK: {
                /* Load database for check */
                NcdDatabase *db = NULL;
                if (opts.agent_check_db_age || opts.agent_check_stats || opts.has_search) {
                    char target_db[NCD_MAX_PATH] = {0};
                    char cwd[MAX_PATH] = {0};
                    platform_get_current_dir(cwd, sizeof(cwd));
                    char target_drive = (char)toupper((unsigned char)cwd[0]);
                    
                    if (opts.has_search && isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
                        target_drive = (char)toupper((unsigned char)opts.search[0]);
                    }
                    
                    if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                        db = db_load_auto(target_db);
                    }
                }
                
                result = agent_mode_check(db, &opts);
                if (db) db_free(db);
                break;
            }
            case AGENT_SUB_COMPLETE: {
                /* Complete doesn't need database, but can use it if available */
                NcdDatabase *db = NULL;
                char cwd[MAX_PATH] = {0};
                platform_get_current_dir(cwd, sizeof(cwd));
                char target_drive = (char)toupper((unsigned char)cwd[0]);
                char target_db[NCD_MAX_PATH] = {0};
                
                if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
                    db = db_load_auto(target_db);
                }
                
                result = agent_mode_complete(db, &opts);
                if (db) db_free(db);
                break;
            }
            case AGENT_SUB_MKDIR: {
                result = agent_mode_mkdir(&opts);
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
            if (db_metadata_save(meta)) {
                ncd_println("Configuration saved.");
            } else {
                ncd_println("Failed to save configuration.");
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
        if (!meta) meta = db_metadata_create();
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
            if (db_drive_path(drive, drv_path, sizeof(drv_path))) {
                int vstatus = db_check_file_version(drv_path);
                if (vstatus == DB_VERSION_MISMATCH || vstatus == DB_VERSION_SKIPPED) {
                    ncd_printf("NCD: Database for %c: needs a full rescan "
                               "(format changed).\r\n"
                               "     Run 'ncd /r' to rebuild, then retry "
                               "the subdirectory rescan.\r\n", drive);
                    db_metadata_free(meta);
                    scan_set_exclusion_list(NULL);
                    db_free(db);
                    con_close();
                    return 1;
                }
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
            if (!db_drive_path(db->drives[i].letter, drv_path, sizeof(drv_path))) {
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
        const char *group_name = opts.search;  /* Includes the @ prefix */
        
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
    if (!db_drive_path(target_drive, target_db, sizeof(target_db))) {
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
                           "     Run 'ncd /r' to rebuild, then retry "
                           "the subdirectory rescan.\r\n", target_drive);
                con_close();
                return 1;
            }
        }

        if (opts.force_rescan && opts.scan_drive_count > 0)
            ncd_printf("NCD: Forced rescan -- scanning requested drives...\r\n");
        else
            ncd_printf("NCD: %s -- scanning all drives...\r\n",
                       db_exists ? "Forced rescan" : "No database found");

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

        int n = (opts.force_rescan
                 ? run_requested_rescan(scan_db, &opts, NULL)
                 : scan_mounts(scan_db, NULL, 0, opts.show_hidden, opts.show_system, opts.timeout_seconds, NULL));
        ncd_printf("  Scan complete: %d directories found.\r\n", n);

        for (int i = 0; i < scan_db->drive_count; i++) {
            char drv_path[NCD_MAX_PATH];
            if (!db_drive_path(scan_db->drives[i].letter, drv_path,
                               sizeof(drv_path)))
                continue;
            db_save_binary_single(scan_db, i, drv_path);
        }
        db_free(scan_db);
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
                ncd_println("Skipped database updates. Use 'ncd /r' to force rescan.");
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
                        snprintf(mount_bufs[i], MAX_PATH, "%c:\\", rescan_drives[i]);
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
                    ncd_println("Skipped database updates. Use 'ncd /r' to force rescan.");
                }
            }
        }
        /* Re-check target after potential update */
        target_version_status = db_check_file_version(target_db);
    }
    
    /* Try to get database from state backend (service) first */
    const NcdDatabase *primary_db = get_state_database();
    bool using_service_db = (primary_db != NULL);
    
    /* Fall back to loading local database if service not available */
    if (!primary_db && target_version_status != DB_VERSION_SKIPPED) {
        /* Corrupt target DB (not just wrong version) -- rebuild that file */
        ncd_printf("NCD: Database for %c: is corrupt -- rebuilding...\r\n",
                   target_drive);
        NcdDatabase *scan_db = db_create();
        scan_db->default_show_hidden = opts.show_hidden;
        scan_db->default_show_system = opts.show_system;
        scan_db->last_scan = time(NULL);
        int n = scan_mounts(scan_db, NULL, 0, opts.show_hidden, opts.show_system, opts.timeout_seconds, NULL);
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
    if (!matches || match_count == 0) {
        if (primary_db && !using_service_db) { db_free((NcdDatabase *)primary_db); primary_db = NULL; } else { primary_db = NULL; }

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
        for (int i = 1; i <= 26 && drive_count < 26; i++) {
            char letter = (char)i;
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
    }

    if (!matches || match_count == 0) {
        result_error("NCD: No directory found matching \"%s\".", opts.search);
        if (primary_db && !using_service_db) db_free((NcdDatabase *)primary_db);
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
        if (primary_db && !using_service_db) db_free((NcdDatabase *)primary_db);
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
        if (primary_db && !using_service_db) db_free((NcdDatabase *)primary_db);
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
        char root[4] = { resolved_drive, ':', '\\', '\0' };
        UINT dtype = platform_get_drive_type(root);
        if (dtype == PLATFORM_DRIVE_NO_ROOT_DIR || dtype == PLATFORM_DRIVE_UNKNOWN) {
            result_error("Drive %c: is not mounted.", resolved_drive);
            spawn_background_rescan(NULL);
            free(matches);
            if (primary_db && !using_service_db) db_free((NcdDatabase *)primary_db);
            con_close();
            return 2;
        }
    }
#endif

    /* Verify the actual directory still exists */
    if (!platform_dir_exists(resolved_path)) {
        result_error("Directory no longer exists: %s\r\n"
                     "Tip: run 'ncd /r' to refresh the database.",
                     resolved_path);
        spawn_background_rescan(NULL);
        free(matches);
        if (primary_db && !using_service_db) db_free((NcdDatabase *)primary_db);
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
    if (primary_db && !using_service_db) db_free((NcdDatabase *)primary_db);
    return 0;
}
