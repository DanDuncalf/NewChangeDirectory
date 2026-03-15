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

/* ============================================================= heuristics */

#define NCD_HEUR_FILE "ncd.history"
#define NCD_HEUR_MAX  20

typedef struct {
    char search[NCD_MAX_PATH];
    char target[NCD_MAX_PATH];
} NcdHeurEntry;

typedef struct {
    NcdHeurEntry items[NCD_HEUR_MAX];
    int          count;
} NcdHeuristics;

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

static bool heur_path(char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';

#if NCD_PLATFORM_WINDOWS
    char local[MAX_PATH] = {0};
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return false;

    size_t len = strlen(local);
    bool has_sep = (len > 0 && (local[len - 1] == '\\' || local[len - 1] == '/'));
    return snprintf(out, out_size, "%s%s%s", local, has_sep ? "" : "\\", NCD_HEUR_FILE)
           < (int)out_size;
#else
    char base[MAX_PATH] = {0};
    if (!platform_get_env("XDG_DATA_HOME", base, sizeof(base)) || !base[0]) {
        char home[MAX_PATH] = {0};
        if (!platform_get_env("HOME", home, sizeof(home)) || !home[0])
            return false;
        snprintf(base, sizeof(base), "%s/.local/share", home);
    }
    int w = snprintf(out, out_size, "%s/ncd/%s", base, NCD_HEUR_FILE);
    return w > 0 && (size_t)w < out_size;
#endif
}

static void heur_load(NcdHeuristics *h)
{
    memset(h, 0, sizeof(*h));

    char path[NCD_MAX_PATH];
    if (!heur_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[(NCD_MAX_PATH * 2) + 32];
    while (fgets(line, sizeof(line), f)) {
        if (h->count >= NCD_HEUR_MAX) break;

        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0') continue;

        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        const char *search = line;
        const char *target = tab + 1;
        if (search[0] == '\0' || target[0] == '\0') continue;

        heur_sanitize(search, h->items[h->count].search,
                      sizeof(h->items[h->count].search), true);
        heur_sanitize(target, h->items[h->count].target,
                      sizeof(h->items[h->count].target), false);
        if (h->items[h->count].search[0] && h->items[h->count].target[0])
            h->count++;
    }

    fclose(f);
}

static void heur_save(const NcdHeuristics *h)
{
    char path[NCD_MAX_PATH];
    if (!heur_path(path, sizeof(path))) return;

    FILE *f = fopen(path, "w");
    if (!f) return;

    for (int i = 0; i < h->count; i++)
#if NCD_PLATFORM_WINDOWS
        fprintf(f, "%s\t%s\r\n", h->items[i].search, h->items[i].target);
#else
        fprintf(f, "%s\t%s\n", h->items[i].search, h->items[i].target);
#endif

    fclose(f);
}

static int heur_find(const NcdHeuristics *h, const char *search_key)
{
    for (int i = 0; i < h->count; i++) {
        if (_stricmp(h->items[i].search, search_key) == 0)
            return i;
    }
    return -1;
}

static bool heur_get_preferred(const char *search_raw, char *out_path, size_t out_size)
{
    if (!out_path || out_size == 0) return false;
    out_path[0] = '\0';

    char key[NCD_MAX_PATH];
    heur_sanitize(search_raw, key, sizeof(key), true);
    if (key[0] == '\0') return false;

    NcdHeuristics h;
    heur_load(&h);
    int idx = heur_find(&h, key);
    if (idx < 0) return false;

    strncpy(out_path, h.items[idx].target, out_size - 1);
    out_path[out_size - 1] = '\0';
    return out_path[0] != '\0';
}

static void heur_note_choice(const char *search_raw, const char *target_path)
{
    char key[NCD_MAX_PATH];
    char target[NCD_MAX_PATH];
    heur_sanitize(search_raw, key, sizeof(key), true);
    heur_sanitize(target_path, target, sizeof(target), false);
    if (key[0] == '\0' || target[0] == '\0') return;

    NcdHeuristics h;
    heur_load(&h);

    int idx = heur_find(&h, key);
    if (idx >= 0) {
        /* Update only if target changed. */
        if (_stricmp(h.items[idx].target, target) == 0) return;

        snprintf(h.items[idx].target, sizeof(h.items[idx].target), "%s", target);

        if (idx > 0) {
            NcdHeurEntry tmp = h.items[idx];
            memmove(&h.items[1], &h.items[0], (size_t)idx * sizeof(NcdHeurEntry));
            h.items[0] = tmp;
        }
        heur_save(&h);
        return;
    }

    /* New unique search term: insert at front, keep max NCD_HEUR_MAX. */
    int new_count = h.count < NCD_HEUR_MAX ? h.count + 1 : NCD_HEUR_MAX;
    if (new_count > 1)
        memmove(&h.items[1], &h.items[0],
                (size_t)(new_count - 1) * sizeof(NcdHeurEntry));
    h.count = new_count;

    strncpy(h.items[0].search, key, sizeof(h.items[0].search) - 1);
    h.items[0].search[sizeof(h.items[0].search) - 1] = '\0';
    strncpy(h.items[0].target, target, sizeof(h.items[0].target) - 1);
    h.items[0].target[sizeof(h.items[0].target) - 1] = '\0';

    heur_save(&h);
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
    NcdHeuristics h;
    heur_load(&h);

    if (h.count == 0) {
        ncd_println("NCD history: empty");
        return;
    }

    ncd_printf("NCD history (%d entries):\r\n", h.count);
    for (int i = 0; i < h.count; i++)
        ncd_printf("  %2d. %-30s -> %s\r\n",
                   i + 1, h.items[i].search, h.items[i].target);
}

static void heur_clear(void)
{
    char path[NCD_MAX_PATH];
    if (!heur_path(path, sizeof(path))) {
        ncd_println("NCD history: could not resolve path.");
        return;
    }
    if (platform_delete_file(path) || !platform_file_exists(path))
        ncd_println("NCD history cleared.");
    else
        ncd_printf("NCD history: could not clear %s\r\n", path);
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
    /* Escape for: @set "VAR=value" -- disallow embedded quotes and newlines */
    size_t j = 0;
    for (size_t i = 0; src_drive[i] && j + 2 < sizeof(safe_drive); i++) {
        char c = src_drive[i];
        if (c == '"') c = '\'';
        if (c == '\r' || c == '\n') c = ' ';
        safe_drive[j++] = c;
    }
    safe_drive[j] = '\0';

    j = 0;
    for (size_t i = 0; src_path[i] && j + 2 < sizeof(safe_path); i++) {
        char c = src_path[i];
        if (c == '"') c = '\'';
        if (c == '\r' || c == '\n') c = ' ';
        safe_path[j++] = c;
    }
    safe_path[j] = '\0';

    j = 0;
    for (size_t i = 0; src_msg[i] && j + 2 < sizeof(safe_msg); i++) {
        char c = src_msg[i];
        if (c == '"') c = '\'';
        if (c == '\r' || c == '\n') c = ' ';
        safe_msg[j++] = c;
    }
    safe_msg[j] = '\0';

    fprintf(f, "@set \"NCD_STATUS=%s\"\r\n", ok ? "OK" : "ERROR");
    fprintf(f, "@set \"NCD_DRIVE=%s\"\r\n",  safe_drive);
    fprintf(f, "@set \"NCD_PATH=%s\"\r\n",   safe_path);
    fprintf(f, "@set \"NCD_MESSAGE=%s\"\r\n", safe_msg);
#else
    /* Escape for: export VAR='value'  -- replace ' with '"'"' and strip newlines */
    size_t j = 0;
    for (size_t i = 0; src_drive[i] && j + 2 < sizeof(safe_drive); i++) {
        char c = src_drive[i];
        if (c == '\'' || c == '\r' || c == '\n') c = '_';
        safe_drive[j++] = c;
    }
    safe_drive[j] = '\0';

    j = 0;
    for (size_t i = 0; src_path[i] && j + 2 < sizeof(safe_path); i++) {
        char c = src_path[i];
        if (c == '\'' || c == '\r' || c == '\n') c = '_';
        safe_path[j++] = c;
    }
    safe_path[j] = '\0';

    j = 0;
    for (size_t i = 0; src_msg[i] && j + 2 < sizeof(safe_msg); i++) {
        char c = src_msg[i];
        if (c == '\'' || c == '\r' || c == '\n') c = '_';
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
    ncd_print(
        "NewChangeDirectory (NCD) -- Norton CD clone for Windows 64-bit\r\n"
        "\r\n"
        "Usage:\r\n"
        "  ncd <search>\r\n"
        "  ncd [/r|/r<drives>] [/i] [/s] [/a] [/d <dbpath>] <search>\r\n"
        "  ncd /r|/r<drives>\r\n"
        "  ncd /f | /fc\r\n"
        "  ncd /h | /?\r\n"
        "\r\n"
        "Command Key:\r\n"
        "  /h, /?      Help\r\n"
        "  /v          Print version (continues if combined with work)\r\n"
        "  /f          Show Frequent history (last 20 unique searches)\r\n"
        "  /fc         Clear Frequent history\r\n"
        "  /r          Force rescan of all drives now\r\n"
        "  /rBDE       Force rescan of specific drives only (B:,D:,E:)\r\n"
        "  /r-b-d      Force rescan of all drives EXCEPT B: and D:\r\n"
        "  /r-b,d      Same as /r-b-d (comma separators supported)\r\n"
        "  /i          Include hidden directories in search\r\n"
        "  /s          Include system directories in search\r\n"
        "  /a          Include all directories (hidden + system)\r\n"
        "  /d <path>   Use alternate database file\r\n"
        "  <search>    Partial path, e.g. downloads or scott\\downloads\r\n"
        "              Special: '.' opens navigator from current directory\r\n"
        "                       'X:' or 'X:\\' opens navigator from drive root\r\n"
        "\r\n"
        "Examples:\r\n"
        "  ncd downloads\r\n"
        "  ncd scott\\downloads\r\n"
        "  ncd /i scott\\appdata\r\n"
        "  ncd /f\r\n"
        "  ncd /fc\r\n"
        "  ncd /rBDE       (force immediate rescan of B:,D:,E:)\r\n"
        "  ncd /r-b-d      (force immediate rescan excluding B:,D:)\r\n"
        "  ncd /r-b,d      (same exclude syntax using commas)\r\n"
        "  ncd /r e,p      (same as /rEP)\r\n"
        "  ncd /r          (force immediate rescan)\r\n"
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

        /*
         * /r <drives> form:
         *   ncd /r e,p
         *   ncd /r e-p
         */
        if ((_stricmp(arg, "/r") == 0 || _stricmp(arg, "-r") == 0) &&
            i + 1 < argc &&
            argv[i + 1][0] != '/' && argv[i + 1][0] != '-') {
            bool parsed = parse_drive_list_token(argv[i + 1],
                                                 opts->scan_drive_mask,
                                                 &opts->scan_drive_count);
            if (parsed) {
                opts->force_rescan = true;
                i++;  /* consume drive-list token */
                continue;
            }
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
                    case 'd':
                        /* /d requires a path as the next argument */
                        if (arg[j + 1] == '\0' && i + 1 < argc) {
                            i++;
                            strncpy(opts->db_override, argv[i],
                                    NCD_MAX_PATH - 1);
                            goto next_arg;
                        } else if (arg[j + 1]) {
                            /* /d<path> (no space) */
                            strncpy(opts->db_override, arg + j + 1,
                                    NCD_MAX_PATH - 1);
                            goto next_arg;
                        } else {
                            ncd_println("NCD: /d requires a path argument");
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
                strncat(opts->search,
                        "\\",
                        NCD_MAX_PATH - strlen(opts->search) - 1);
                strncat(opts->search,
                        arg,
                        NCD_MAX_PATH - strlen(opts->search) - 1);
            } else {
                strncpy(opts->search, arg, NCD_MAX_PATH - 1);
                opts->has_search = true;
            }
        }
next_arg:;
    }
    return true;
}

#if NCD_PLATFORM_LINUX
/*
 * Progress callback for the selected-drive rescan path (/r c,e etc.).
 * Since scan_drive() runs on the main thread, the callback fires every
 * ~PROGRESS_INTERVAL directories and overwrites the current console line.
 */
typedef struct {
    char letter;
    char mnt_path[64];
    long approx_count;
} WslSelScanProg;

static void wsl_sel_scan_progress_cb(char dl, const char *path, void *ud)
{
    (void)dl;
    WslSelScanProg *p = (WslSelScanProg *)ud;
    p->approx_count += 100;   /* one callback ≈ PROGRESS_INTERVAL dirs */
    ncd_printf("\r  [%c: -> %s] %5ld scanned  %.50s\x1b[K",
               p->letter, p->mnt_path, p->approx_count, path ? path : "");
}
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
#if NCD_PLATFORM_LINUX
    /*
     * On Linux / WSL, drive letters map to /mnt/X paths.
     * If the user specified a drive list (e.g. /r c-e or /rCE) only scan
     * the corresponding /mnt/X mount points; otherwise scan all mounts.
     */
    if (opts->scan_drive_count > 0 || opts->skip_drive_count > 0) {
        int total = 0;
        for (int i = 0; i < 26; i++) {
            if (opts->scan_drive_count > 0 && !opts->scan_drive_mask[i]) continue;
            if (opts->skip_drive_mask[i]) continue;

            char letter   = (char)('A' + i);
            char mnt_path[64];
            snprintf(mnt_path, sizeof(mnt_path), "/mnt/%c", (char)('a' + i));

            if (!platform_dir_exists(mnt_path)) {
                ncd_printf("  [%c:] %s not found, skipping\r\n", letter, mnt_path);
                continue;
            }

            /* Pre-register the drive so scan_drive() picks up our mount path. */
            DriveData *drv = db_add_drive(db, letter);
            drv->type = PLATFORM_DRIVE_FIXED;
            snprintf(drv->label, sizeof(drv->label), "%s", mnt_path);

            /* Print without newline so the progress callback can overwrite
             * this line with \r on each update.                           */
            ncd_printf("  [%c: -> %s] (starting...)", letter, mnt_path);

            WslSelScanProg prog;
            prog.letter       = letter;
            prog.approx_count = 0;
            snprintf(prog.mnt_path, sizeof(prog.mnt_path), "%s", mnt_path);

            int n = scan_drive(db, letter,
                               opts->show_hidden, opts->show_system,
                               wsl_sel_scan_progress_cb, &prog);
            if (n >= 0) {
                ncd_printf("\r  [%c: -> %s] COMPLETE (%d directories)\x1b[K\r\n",
                           letter, mnt_path, n);
                total += n;
            } else {
                ncd_printf("\r  [%c: -> %s] WARNING: scan failed/skipped\x1b[K\r\n",
                           letter, mnt_path);
            }
        }
        return total;
    }

    /* No drive filter -- scan all mount points. */
    return scan_all_drives(db, opts->show_hidden, opts->show_system);
#else
    if (opts->scan_drive_count <= 0 && opts->skip_drive_count <= 0)
        return scan_all_drives(db, opts->show_hidden, opts->show_system);

    if (opts->scan_drive_count > 0) {
        char list[160] = {0};
        strncat(list, "  Scanning selected drives:", sizeof(list) - 1);
        for (int i = 0; i < 26; i++) {
            if (!opts->scan_drive_mask[i]) continue;
            if (opts->skip_drive_mask[i]) continue;
            char tmp[5];
            snprintf(tmp, sizeof(tmp), " %c:", (char)('A' + i));
            strncat(list, tmp, sizeof(list) - strlen(list) - 1);
        }
        ncd_printf("%s\r\n", list);
    } else {
        char list[160] = {0};
        strncat(list, "  Scanning all drives except:", sizeof(list) - 1);
        for (int i = 0; i < 26; i++) {
            if (!opts->skip_drive_mask[i]) continue;
            char tmp[5];
            snprintf(tmp, sizeof(tmp), " %c:", (char)('A' + i));
            strncat(list, tmp, sizeof(list) - strlen(list) - 1);
        }
        ncd_printf("%s\r\n", list);
    }

    int total = 0;
    for (int i = 0; i < 26; i++) {
        if (opts->scan_drive_count > 0 && !opts->scan_drive_mask[i]) continue;
        if (opts->skip_drive_mask[i]) continue;
        char letter = (char)('A' + i);
        ncd_printf("  [%c:] (starting...)\r\n", letter);
        SingleScanProgress pg = { .drive = letter };
        int n = scan_drive(db, letter, opts->show_hidden, opts->show_system,
                           single_scan_progress_cb, &pg);
        if (n >= 0) {
            ncd_printf("\r  [%c:] COMPLETE (%d directories)\r\n", letter, n);
        } else {
            ncd_printf("\r  [%c:] WARNING: scan failed/skipped\r\n", letter);
        }
        if (n > 0) total += n;
    }
    return total;
#endif
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
        NcdDatabase *db = db_create();
        db->default_show_hidden = opts.show_hidden;
        db->default_show_system = opts.show_system;
        db->last_scan = time(NULL);

        int n = run_requested_rescan(db, &opts);
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
        result_ok(chosen, (char)toupper((unsigned char)chosen[0]));
        con_close();
        return 0;
    }

#if NCD_PLATFORM_WINDOWS
    /* -------------------------- drive-root navigation mode (X: / X:\)   */
    if (isalpha((unsigned char)opts.search[0]) &&
        opts.search[1] == ':' &&
        (opts.search[2] == '\0' ||
         ((opts.search[2] == '\\' || opts.search[2] == '/') &&
          opts.search[3] == '\0'))) {
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
        result_ok(chosen, root[0]);
        con_close();
        return 0;
    }
#endif

    /* ----------------------------------------- parse drive from search   */
    char target_drive;
    char clean_search[NCD_MAX_PATH];

#if NCD_PLATFORM_WINDOWS
    /*
     * If the search string starts with "X:" (a drive letter), search only
     * that drive first; otherwise use the current working directory's drive.
     * Fall back to all drives if nothing matches on the primary drive.
     */
    if (isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
        target_drive = (char)toupper((unsigned char)opts.search[0]);
        strncpy(clean_search, opts.search + 2, sizeof(clean_search) - 1);
        clean_search[sizeof(clean_search) - 1] = '\0';
    } else {
        char cwd[MAX_PATH] = {0};
        platform_get_current_dir(cwd, sizeof(cwd));
        target_drive = (char)toupper((unsigned char)cwd[0]);
        strncpy(clean_search, opts.search, sizeof(clean_search) - 1);
        clean_search[sizeof(clean_search) - 1] = '\0';
    }
#else
    /*
     * On Linux / WSL: if the search starts with "X:" treat X as a Windows
     * drive letter mapping to /mnt/X and search that database first.
     * Otherwise start with the first available database (mount index 0x01).
     */
    if (isalpha((unsigned char)opts.search[0]) && opts.search[1] == ':') {
        target_drive = (char)toupper((unsigned char)opts.search[0]);
        /* opts.search+2 is always shorter than clean_search; use explicit
         * precision cap to let GCC prove no truncation occurs.            */
        snprintf(clean_search, sizeof(clean_search), "%.*s",
                 (int)(sizeof(clean_search) - 1), opts.search + 2);
    } else {
        target_drive = '\x01';
        snprintf(clean_search, sizeof(clean_search), "%s", opts.search);
    }
#endif

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
        if (opts.force_rescan && opts.scan_drive_count > 0)
            ncd_printf("NCD: Forced rescan -- scanning requested drives...\r\n");
        else
            ncd_printf("NCD: %s -- scanning all drives...\r\n",
                       db_exists ? "Forced rescan" : "No database found");

        NcdDatabase *scan_db = db_create();
        scan_db->default_show_hidden = opts.show_hidden;
        scan_db->default_show_system = opts.show_system;
        scan_db->last_scan = time(NULL);

        int n = (opts.force_rescan
                 ? run_requested_rescan(scan_db, &opts)
                 : scan_all_drives(scan_db, opts.show_hidden, opts.show_system));
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

    /* -------------------- load target drive and search it               */
    NcdDatabase *primary_db = db_load_auto(target_db);
    if (!primary_db) {
        /* Corrupt target DB -- rebuild that file */
        ncd_printf("NCD: Database for %c: is corrupt -- rebuilding...\r\n",
                   target_drive);
        NcdDatabase *scan_db = db_create();
        scan_db->default_show_hidden = opts.show_hidden;
        scan_db->default_show_system = opts.show_system;
        scan_db->last_scan = time(NULL);
        int n = scan_all_drives(scan_db, opts.show_hidden, opts.show_system);
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
        matches = matcher_find(primary_db,
                               clean_search,
                               opts.show_hidden,
                               opts.show_system,
                               &match_count);
    }

    /* -------------------- fallback: search all other drive databases     */
    if (!matches || match_count == 0) {
        if (primary_db) { db_free(primary_db); primary_db = NULL; }

#if NCD_PLATFORM_WINDOWS
        for (int i = 0; i < 26; i++) {
            char letter = (char)('A' + i);
            if (letter == target_drive) continue;   /* already searched    */
#else
        /* On Linux, mount indices run from 0x01 to 0x1A. */
        for (int i = 1; i <= 26; i++) {
            char letter = (char)i;
            if (letter == target_drive) continue;   /* already searched    */
#endif
            char drv_path[NCD_MAX_PATH];
            if (!db_drive_path(letter, drv_path, sizeof(drv_path))) continue;
            if (!platform_file_exists(drv_path))
                continue;

            NcdDatabase *d = db_load_auto(drv_path);
            if (!d) continue;

            int cnt = 0;
            NcdMatch *m = matcher_find(d, clean_search,
                                       opts.show_hidden, opts.show_system,
                                       &cnt);
            db_free(d);

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

    con_close();
    if (primary_db) db_free(primary_db);
    return 0;
}
