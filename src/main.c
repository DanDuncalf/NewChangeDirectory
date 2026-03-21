/*
 * main.c  --  NewChangeDirectory  (NCD)  entry point
 *
 * Usage (via ncd.bat wrapper):
 *   ncd [options] [search]
 *
 * Options:
 *   /r          Force an immediate rescan of all drives
 *   /h          Include hidden directories in search
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

#include "ncd.h"
#include "database.h"
#include "scanner.h"
#include "matcher.h"
#include "ui.h"
#include "platform.h"

/* forward declarations used by heuristics helpers */
static void ncd_print(const char *s);
static void ncd_println(const char *s);
static void ncd_printf(const char *fmt, ...);

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
 */
#define NCD_BUILD_VER   "1.2"
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

/* ============================================================== CLI parse */

static void print_usage(void)
{
    ncd_printf("%s\r\n"
        "\r\n"
        "Usage:\r\n"
        "  ncd <search>\r\n"
        "  ncd [/r|/r<drives>] [/i] [/s] [/a] [/d <dbpath>] [/t <sec>] <search>\r\n"
        "  ncd /r|/r<drives>\r\n"
        "  ncd /f | /fc\r\n"
        "  ncd /0 | /1 | /2 | /3 ... /9\r\n"
        "  ncd /h | /?\r\n"
        "\r\n"
        "Command Key:\r\n"
        "  /h, /?      Help\r\n"
        "  /v          Print version (continues if combined with work)\r\n"
        "  /f          Show Frequent history (last 20 unique searches)\r\n"
        "  /fc         Clear Frequent history\r\n"
        "  /0          Ping-pong: swap between last two directories (same as no args)\r\n"
        "  /1          Add current directory to history list\r\n"
        "  /2 .. /9    Jump to Nth directory in history (up to 9 entries)\r\n"
        "  /r          Force rescan of all drives now\r\n"
        "  /r. or /r . Rescan current subdirectory only\r\n"
        "  /rBDE       Force rescan of specific drives only (B:,D:,E:)\r\n"
        "  /r-b-d      Force rescan of all drives EXCEPT B: and D:\r\n"
        "  /r-b,d      Same as /r-b-d (comma separators supported)\r\n"
        "  /i          Include hidden directories in search\r\n"
        "  /s          Include system directories in search\r\n"
        "  /a          Include all directories (hidden + system)\r\n"
        "  /z          Fuzzy matching (digit-word substitution + DL distance)\r\n"
        "  /g <group>  Group current directory (e.g., /g @home)\r\n"
        "  /g- <group> Remove a group\r\n"
        "  /gl         List all groups\r\n"
        "  -x <pat>    Add exclusion pattern (e.g., -x C:Windows)\r\n"
        "  -x- <pat>   Remove exclusion pattern\r\n"
        "  -xl         List all exclusion patterns\r\n"
        "  /c          Edit configuration (set default options)\r\n"
        "  /d <path>   Use alternate database file\r\n"
        "  /t <sec>    Scan inactivity timeout in seconds (default: 300)\r\n"
        "  <search>    Partial path with optional wildcards (*, ?)\r\n"
        "              Special: '.' opens navigator from current directory\r\n"
        "                       'X:' or 'X:' opens navigator from drive root\r\n",
        platform_get_app_title());
    
    /* Platform-specific suffix */
    char suffix_buf[512];
    if (platform_write_help_suffix(suffix_buf, sizeof(suffix_buf)) > 0) {
        ncd_print(suffix_buf);
    }
    
    ncd_print(
        "\r\n"
        "Examples:\r\n"
        "  ncd downloads\r\n"
        "  ncd scott\\downloads\r\n"
        "  ncd down*       (wildcard: matches downloads, downgrade, etc.)\r\n"
        "  ncd *load*      (wildcard: matches downloads, uploads, etc.)\r\n"
        "  ncd src?x64     (single char: matches src_x64, src-x64)\r\n"
        "  ncd /i scott\\appdata\r\n"
        "  ncd /f\r\n"
        "  ncd /fc\r\n"
        "  ncd /rBDE       (force immediate rescan of B:,D:,E:)\r\n"
        "  ncd /r-b-d      (force immediate rescan excluding B:,D:)\r\n"
        "  ncd /r-b,d      (same exclude syntax using commas)\r\n"
        "  ncd /r e,p      (same as /rEP)\r\n"
        "  ncd /r          (force immediate rescan)\r\n"
#if NCD_PLATFORM_LINUX
        "  ncd /r /        (force rescan of Linux root only, no /mnt drives)\r\n"
#endif
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

static bool parse_args(int argc, char *argv[], NcdOptions *opts)
{
    memset(opts, 0, sizeof(NcdOptions));
    opts->timeout_seconds = 300;  /* default 5 minute timeout */

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        /* Explicit help commands */
        if (_stricmp(arg, "/h") == 0 || _stricmp(arg, "-h") == 0 ||
            _stricmp(arg, "/?") == 0 || _stricmp(arg, "-?") == 0 ||
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

        /* Directory history navigation: /0, /1, /2, etc. */
        if ((arg[0] == '/' || arg[0] == '-') && isdigit((unsigned char)arg[1])) {
            int idx = atoi(arg + 1);
            if (idx >= 0 && idx <= 9) {
                opts->history_nav = true;
                opts->history_index = idx;
                continue;
            }
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

static int run_requested_rescan(NcdDatabase *db, const NcdOptions *opts)
{
    /* Handle subdirectory rescan: /r. */
    if (opts->scan_subdirectory[0]) {
        char drive = platform_get_drive_letter(opts->scan_subdirectory);
        ncd_printf("NCD: Rescanning subdirectory %s...\r\n", opts->scan_subdirectory);
        int n = scan_subdirectory(db, drive, opts->scan_subdirectory, opts->show_hidden, opts->show_system);
        ncd_printf("  Scanned %d directories.\r\n", n);
        return n;
    }

    /* Linux special: /r / scans only root filesystem */
    if (opts->scan_root_only) {
        const char *root_mount = "/";
        return scan_mounts(db, &root_mount, 1, opts->show_hidden, opts->show_system, opts->timeout_seconds);
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
        return scan_mounts(db, NULL, 0, opts->show_hidden, opts->show_system, opts->timeout_seconds);
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
    return scan_mounts(db, mounts, mcount, opts->show_hidden, opts->show_system, opts->timeout_seconds);
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
    if (!db_metadata_exists() && !opts.config_edit && !opts.show_help) {
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
        
        if (db_group_set(meta, opts.group_name, cwd)) {
            db_metadata_save(meta);
            ncd_printf("Group '%s' -> '%s'\r\n", opts.group_name, cwd);
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

    /* ------------------------------------------------ directory history navigation */
    if (opts.history_nav || (!opts.has_search && argc == 1)) {
        /* No args: treat as /0 (ping-pong) */
        int idx = opts.history_nav ? opts.history_index : 0;
        
        NcdMetadata *meta = db_metadata_load();
        if (!meta) meta = db_metadata_create();
        
        if (idx == 0) {
            /* /0 or no args: ping-pong between [0] and [1], swapping them */
            if (db_dir_history_count(meta) < 2) {
                ncd_println("NCD: Not enough history entries to ping-pong.\r\n");
                ncd_println("Run 'ncd /1' first to add current directory to history.\r\n");
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
            
        } else if (idx == 1) {
            /* /1: add current directory to top of history */
            char cwd[MAX_PATH] = {0};
            if (!platform_get_current_dir(cwd, sizeof(cwd))) {
                result_error("Could not determine current directory.");
                db_metadata_free(meta);
                con_close();
                return 1;
            }
            
            char drive = platform_get_drive_letter(cwd);
            if (drive == 0) {
                /* On Linux, use first char or default */
                drive = cwd[0];
            }
            
            if (db_dir_history_add(meta, cwd, drive)) {
                meta->dir_history_dirty = true;
                db_metadata_save(meta);
                ncd_printf("Added to history: %s\r\n", cwd);
            } else {
                ncd_println("Failed to add to history.");
            }
            db_metadata_free(meta);
            con_close();
            return 0;
            
        } else {
            /* /2, /3, etc.: jump to Nth entry (1-indexed, so /2 = index 1) */
            int entry_idx = idx - 1;  /* Convert to 0-indexed */
            
            if (entry_idx >= db_dir_history_count(meta)) {
                ncd_printf("NCD: History entry /%d not available (only %d entries).\r\n",
                           idx, db_dir_history_count(meta));
                db_metadata_free(meta);
                con_close();
                return 1;
            }
            
            const NcdDirHistoryEntry *entry = db_dir_history_get(meta, entry_idx);
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

        int n = run_requested_rescan(db, &opts);
        
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
        if (meta) {
            const NcdGroupEntry *entry = db_group_get(meta, group_name);
            if (entry) {
                /* Verify the directory still exists */
                if (platform_dir_exists(entry->path)) {
                    char drive_letter = entry->path[0];
                    add_current_dir_to_history();
                    result_ok(entry->path, drive_letter);
                    db_metadata_free(meta);
                    con_close();
                    return 0;
                } else {
                    ncd_printf("Group '%s' points to non-existent directory: %s\r\n",
                               group_name, entry->path);
                    db_metadata_free(meta);
                    con_close();
                    return 1;
                }
            }
            db_metadata_free(meta);
        }
        
        /* Group not found */
        result_error("Unknown group: %s", group_name);
        con_close();
        return 1;
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
                 ? run_requested_rescan(scan_db, &opts)
                 : scan_mounts(scan_db, NULL, 0, opts.show_hidden, opts.show_system, opts.timeout_seconds));
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
                                        opts.timeout_seconds);
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
                                        opts.timeout_seconds);
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
    
    /* Now load the target database */
    NcdDatabase *primary_db = db_load_auto(target_db);
    if (!primary_db && target_version_status != DB_VERSION_SKIPPED) {
        /* Corrupt target DB (not just wrong version) -- rebuild that file */
        ncd_printf("NCD: Database for %c: is corrupt -- rebuilding...\r\n",
                   target_drive);
        NcdDatabase *scan_db = db_create();
        scan_db->default_show_hidden = opts.show_hidden;
        scan_db->default_show_system = opts.show_system;
        scan_db->last_scan = time(NULL);
        int n = scan_mounts(scan_db, NULL, 0, opts.show_hidden, opts.show_system, opts.timeout_seconds);
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
        if (primary_db) { db_free(primary_db); primary_db = NULL; }

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
        if (primary_db) db_free(primary_db);
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
        if (primary_db) db_free(primary_db);
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
        if (primary_db) db_free(primary_db);
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
            if (primary_db) db_free(primary_db);
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
        if (primary_db) db_free(primary_db);
        con_close();
        return 2;
    }

    /* -------------------------------------------- write success result   */
    heur_note_choice(opts.search, resolved_path);
    add_current_dir_to_history();
    result_ok(resolved_path, resolved_drive);

    free(matches);

    /* --------- schedule background rescan if DB is more than 24h old ---- */
    bool needs_rescan = false;
    if (primary_db && primary_db->last_scan > 0) {
        time_t age_seconds = time(NULL) - primary_db->last_scan;
        needs_rescan = (age_seconds > (time_t)(NCD_RESCAN_HOURS * 3600));
    }
    if (!chose_custom_path && needs_rescan) {
        ncd_printf("NCD: Database is over %d hours old -- "
                   "spawning background rescan...\r\n", NCD_RESCAN_HOURS);
        spawn_background_rescan(NULL);
    }

    /* Cleanup global metadata before exit */
    save_metadata_if_dirty();
    if (g_metadata) {
        db_metadata_free(g_metadata);
        g_metadata = NULL;
    }

    con_close();
    if (primary_db) db_free(primary_db);
    return 0;
}
