/*
 * cli.c  --  Command-line interface parsing for NCD
 *
 * New syntax (hard cutover):
 *   - Short flags: -i, -s, -a, -z, -v, -c, -f, -r, -h, -0..-9
 *   - Bundled booleans: -asz -> -a -s -z
 *   - Parameterized flags MUST use colon: -r:d, -h:c3, -g:@proj, -x:pat
 *   - Long flags: --help, --hidden, --rescan, etc.
 *   - Long with value: --agent:query, --database:path
 *   - On Windows: /i acts as -i (single flag only, no bundling)
 *   - On Linux: /foo is a positional path, never a flag
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#include "ncd.h"
#include "cli.h"
#include "platform.h"

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#endif

/* External console output functions (from main.c) */
extern void ncd_print(const char *s);
extern void ncd_println(const char *s);
extern void ncd_printf(const char *fmt, ...);

/* ============================================================= tokenization */

typedef enum {
    TOK_SHORT,      /* -i, -s, -r  (single char boolean) */
    TOK_LONG,       /* --help, --hidden */
    TOK_SHORT_VAL,  /* -r:d, -h:c3 */
    TOK_LONG_VAL,   /* --agent:query, --database:path */
    TOK_POSitional
} TokType;

typedef struct {
    TokType type;
    char    s;           /* for TOK_SHORT and TOK_SHORT_VAL */
    const char *name;    /* for TOK_LONG and TOK_LONG_VAL (allocated in synth_buf) */
    const char *val;     /* for TOK_SHORT_VAL, TOK_LONG_VAL, TOK_POSitional */
    int     orig_idx;    /* index in original argv */
} CliToken;

#define MAX_TOKENS 256
#define MAX_SYNTH  1024

/* Characters allowed in short-flag bundles (booleans only) */
static bool is_bundle_bool(char c)
{
    switch (c) {
        case 'a': case 'A':
        case 'i': case 'I':
        case 's': case 'S':
        case 'z': case 'Z':
        case 'v': case 'V':
        case 'c': case 'C':
        case 'f': case 'F':
        case 'r': case 'R':
        case 'h': case 'H':
        case '?':
            return true;
    }
    if (c >= '0' && c <= '9') return true;
    return false;
}

static bool tokenize_args(int argc, char *argv[],
                          CliToken *tokens, int *out_count,
                          char *synth_buf, size_t synth_cap)
{
    int tc = 0;
    size_t synth_used = 0;

    for (int i = 1; i < argc && tc < MAX_TOKENS - 1; i++) {
        const char *arg = argv[i];
        bool from_slash = false;
        const char *body = arg;

        /* Determine prefix type */
        if (arg[0] == '-' && arg[1] == '-') {
            /* Long flag or long flag with value */
            body = arg + 2;
            const char *colon = strchr(body, ':');
            if (colon && colon != body) {
                size_t name_len = (size_t)(colon - body);
                char *name = synth_buf + synth_used;
                if (synth_used + name_len + 1 > synth_cap) return false;
                memcpy(name, body, name_len);
                name[name_len] = '\0';
                synth_used += name_len + 1;
                tokens[tc].type = TOK_LONG_VAL;
                tokens[tc].name = name;
                tokens[tc].val = colon + 1;
                tokens[tc].s = 0;
                tokens[tc].orig_idx = i;
            } else if (body[0] == '\0') {
                /* lone "--" - positional */
                tokens[tc].type = TOK_POSitional;
                tokens[tc].val = arg;
                tokens[tc].orig_idx = i;
            } else {
                tokens[tc].type = TOK_LONG;
                tokens[tc].name = body;  /* points into argv, safe because no colon split */
                tokens[tc].val = NULL;
                tokens[tc].s = 0;
                tokens[tc].orig_idx = i;
            }
            tc++;
            continue;
        }

        if (arg[0] == '-') {
            body = arg + 1;
            from_slash = false;
        }
#if NCD_PLATFORM_WINDOWS
        else if (arg[0] == '/') {
            body = arg + 1;
            from_slash = true;
        }
#endif
        else {
            /* Positional argument */
            tokens[tc].type = TOK_POSitional;
            tokens[tc].val = arg;
            tokens[tc].orig_idx = i;
            tc++;
            continue;
        }

        /* After - or / */
        if (body[0] == '\0') {
            tokens[tc].type = TOK_POSitional;
            tokens[tc].val = arg;
            tokens[tc].orig_idx = i;
            tc++;
            continue;
        }

        /* Check for colon form: -r:d or /r:d (value may be empty) */
        const char *colon = strchr(body, ':');
        if (colon && colon == body + 1) {
            tokens[tc].type = TOK_SHORT_VAL;
            tokens[tc].s = body[0];  /* preserve case to distinguish -g vs -G */
            tokens[tc].val = colon + 1;
            tokens[tc].name = NULL;
            tokens[tc].orig_idx = i;
            tc++;
            continue;
        }

        /* Multi-char token after dash/slash */
        if (from_slash) {
            if (colon && colon != body) {
                size_t name_len = (size_t)(colon - body);
                char *name = synth_buf + synth_used;
                if (synth_used + name_len + 1 > synth_cap) return false;
                memcpy(name, body, name_len);
                name[name_len] = '\0';
                synth_used += name_len + 1;
                tokens[tc].type = TOK_LONG_VAL;
                tokens[tc].name = name;
                tokens[tc].val = colon + 1;
                tokens[tc].s = 0;
                tokens[tc].orig_idx = i;
            } else if (body[1] == '\0') {
                tokens[tc].type = TOK_SHORT;
                tokens[tc].s = body[0];  /* preserve case */
                tokens[tc].orig_idx = i;
            } else {
                /* On Windows, /r. and /t5 are short options with attached values.
                 * Distinguish from long options like /agent by checking if body
                 * matches a known long option name exactly. */
                bool is_known_long = false;
                if (_stricmp(body, "agent") == 0 ||
                    _stricmp(body, "help") == 0 ||
                    _stricmp(body, "hidden") == 0 ||
                    _stricmp(body, "system") == 0 ||
                    _stricmp(body, "all") == 0 ||
                    _stricmp(body, "fuzzy") == 0 ||
                    _stricmp(body, "version") == 0 ||
                    _stricmp(body, "config") == 0 ||
                    _stricmp(body, "frequent") == 0 ||
                    _stricmp(body, "rescan") == 0 ||
                    _stricmp(body, "history") == 0 ||
                    _stricmp(body, "group") == 0 ||
                    _stricmp(body, "database") == 0 ||
                    _stricmp(body, "timeout") == 0 ||
                    _stricmp(body, "encoding") == 0 ||
                    _stricmp(body, "conf") == 0 ||
                    _stricmp(body, "retry") == 0) {
                    is_known_long = true;
                }

                if (!is_known_long) {
                    /* Check if first char is a short option that takes a value */
                    char c = body[0];
                    bool is_short_with_val = false;
                    switch (c) {
                        case 'r': case 'R':  /* /r., /rC, /rC-a */
                        case 't': case 'T':  /* /t5, /t10 */
                        case 'd': case 'D':  /* /dpath */
                        case 'u': case 'U':  /* /u8, /u16 */
                        case 'g': case 'G':  /* /g@proj, /gl */
                        case 'x': case 'X':  /* /xpat, /xl */
                        case 'h': case 'H':  /* /hl, /hc, /hc3 */
                        case 'f': case 'F':  /* /fc */
                            is_short_with_val = true;
                            break;
                    }
                    if (is_short_with_val) {
                        tokens[tc].type = TOK_SHORT_VAL;
                        tokens[tc].s = c;
                        tokens[tc].val = body + 1;
                        tokens[tc].orig_idx = i;
                    } else {
                        tokens[tc].type = TOK_LONG;
                        tokens[tc].name = body;
                        tokens[tc].orig_idx = i;
                    }
                } else {
                    tokens[tc].type = TOK_LONG;
                    tokens[tc].name = body;
                    tokens[tc].orig_idx = i;
                }
            }
            tc++;
            continue;
        }

        /* Dash prefix: may be a bundle or a long flag */
        if (body[1] == '\0') {
            tokens[tc].type = TOK_SHORT;
            tokens[tc].s = body[0];  /* preserve case */
            tokens[tc].orig_idx = i;
            tc++;
            continue;
        }

        /* Check if it's a known long option name (e.g., -conf, -agent, -hidden) */
        bool is_known_long = false;
        if (_stricmp(body, "agent") == 0 ||
            _stricmp(body, "help") == 0 ||
            _stricmp(body, "hidden") == 0 ||
            _stricmp(body, "system") == 0 ||
            _stricmp(body, "all") == 0 ||
            _stricmp(body, "fuzzy") == 0 ||
            _stricmp(body, "version") == 0 ||
            _stricmp(body, "config") == 0 ||
            _stricmp(body, "frequent") == 0 ||
            _stricmp(body, "rescan") == 0 ||
            _stricmp(body, "history") == 0 ||
            _stricmp(body, "group") == 0 ||
            _stricmp(body, "database") == 0 ||
            _stricmp(body, "timeout") == 0 ||
            _stricmp(body, "encoding") == 0 ||
            _stricmp(body, "conf") == 0 ||
            _stricmp(body, "retry") == 0) {
            is_known_long = true;
        }

        if (is_known_long) {
            if (colon && colon != body) {
                size_t name_len = (size_t)(colon - body);
                char *name = synth_buf + synth_used;
                if (synth_used + name_len + 1 > synth_cap) return false;
                memcpy(name, body, name_len);
                name[name_len] = '\0';
                synth_used += name_len + 1;
                tokens[tc].type = TOK_LONG_VAL;
                tokens[tc].name = name;
                tokens[tc].val = colon + 1;
                tokens[tc].s = 0;
                tokens[tc].orig_idx = i;
            } else {
                tokens[tc].type = TOK_LONG;
                tokens[tc].name = body;
                tokens[tc].orig_idx = i;
            }
            tc++;
            continue;
        }

        /* Check if first char is a short option that takes an attached value */
        {
            char c = body[0];
            bool is_short_with_val = false;
            switch (c) {
                case 'r': case 'R':  /* -rC, -r., -rC-a */
                case 't': case 'T':  /* -t5, -t10 */
                case 'd': case 'D':  /* -dpath */
                case 'u': case 'U':  /* -u8, -u16 */
                case 'g': case 'G':  /* -g@proj, -gl, -g- */
                case 'x': case 'X':  /* -xpat, -xl, -x- */
                case 'h': case 'H':  /* -hl, -hc, -hc3 */
                case 'f': case 'F':  /* -fc */
                    is_short_with_val = true;
                    break;
            }
            if (is_short_with_val) {
                tokens[tc].type = TOK_SHORT_VAL;
                tokens[tc].s = c;
                tokens[tc].val = body + 1;
                tokens[tc].orig_idx = i;
                tc++;
                continue;
            }
        }

        /* Try to expand as bundle of booleans */
        bool all_bundle = true;
        for (const char *p = body; *p; p++) {
            if (!is_bundle_bool(*p)) {
                all_bundle = false;
                break;
            }
        }
        if (all_bundle) {
            for (const char *p = body; *p && tc < MAX_TOKENS - 1; p++) {
                tokens[tc].type = TOK_SHORT;
                tokens[tc].s = (char)toupper((unsigned char)*p);  /* bundles are case-insensitive */
                tokens[tc].orig_idx = i;
                tc++;
            }
            continue;
        }

        /* Not a valid bundle: treat as long flag (e.g., -conf is already handled above) */
        if (colon && colon != body) {
            size_t name_len = (size_t)(colon - body);
            char *name = synth_buf + synth_used;
            if (synth_used + name_len + 1 > synth_cap) return false;
            memcpy(name, body, name_len);
            name[name_len] = '\0';
            synth_used += name_len + 1;
            tokens[tc].type = TOK_LONG_VAL;
            tokens[tc].name = name;
            tokens[tc].val = colon + 1;
            tokens[tc].s = 0;
            tokens[tc].orig_idx = i;
        } else {
            tokens[tc].type = TOK_LONG;
            tokens[tc].name = body;
            tokens[tc].orig_idx = i;
        }
        tc++;
    }

    *out_count = tc;
    return true;
}

/* ============================================================= drive list parsing */

static bool parse_drive_list_token(const char *tok, bool *mask, int *count)
{
    if (!tok || !tok[0] || !mask || !count) return false;

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
            /* Separator - skip it */
            i++;
            continue;
        }
        return false;
    }
    return saw_letter;  /* Allow single drive without separator */
}

static bool parse_skip_drive_list(const char *tok, bool *mask, int *count)
{
    if (!tok || !tok[0]) return false;
    for (int i = 0; tok[i]; i++) {
        char c = tok[i];
        if (c == '-' || c == ',') continue;
        if (!isalpha((unsigned char)c)) return false;
        char u = (char)toupper((unsigned char)c);
        int idx = u - 'A';
        if (!mask[idx]) {
            mask[idx] = true;
            (*count)++;
        }
    }
    return *count > 0;
}

/* ============================================================= agent mode arg parsing */

bool parse_agent_args(int argc, char *argv[], int *consumed, NcdOptions *opts)
{
    if (argc < 2) {
        ncd_println("NCD: --agent requires a subcommand");
        return false;
    }
    
    const char *sub = argv[1];
    *consumed = 0;
    
    if (_stricmp(sub, "query") == 0) {
        if (argc < 3) {
            ncd_println("NCD: --agent query requires a search term");
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
            } else if (strcmp(opt, "--all") == 0) {
                opts->show_hidden = true;
                opts->show_system = true;
                (*consumed)++;
            } else if (strcmp(opt, "--depth") == 0 || strcmp(opt, "--depth-sort") == 0) {
                opts->agent_depth_sort = true;
                (*consumed)++;
            } else if (strncmp(opt, "--limit=", 8) == 0) {
                opts->agent_limit = atoi(opt + 8);
                (*consumed)++;
            } else if (strcmp(opt, "--limit") == 0 && i + 1 < argc) {
                opts->agent_limit = atoi(argv[i + 1]);
                (*consumed) += 2;
                i++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "ls") == 0) {
        if (argc < 3) {
            ncd_println("NCD: --agent ls requires a path");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_LS;
        opts->agent_depth = 1;  /* default ls depth */
        platform_strncpy_s(opts->search, sizeof(opts->search), argv[2]);
        opts->has_search = true;
        *consumed = 2;
        
        /* Parse ls options */
        for (int i = 3; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--dirs-only") == 0) {
                opts->agent_dirs_only = true;
                (*consumed)++;
            } else if (strcmp(opt, "--files-only") == 0) {
                opts->agent_files_only = true;
                (*consumed)++;
            } else if (strncmp(opt, "--pattern=", 10) == 0) {
                platform_strncpy_s(opts->agent_pattern, sizeof(opts->agent_pattern), opt + 10);
                (*consumed)++;
            } else if (strcmp(opt, "--pattern") == 0 && i + 1 < argc) {
                platform_strncpy_s(opts->agent_pattern, sizeof(opts->agent_pattern), argv[i + 1]);
                (*consumed) += 2;
                i++;
            } else if (strcmp(opt, "--depth") == 0 && i + 1 < argc) {
                int depth = atoi(argv[i + 1]);
                if (depth < 1) {
                    ncd_println("NCD: --agent ls --depth must be >= 1");
                    return false;
                }
                opts->agent_depth = depth;
                (*consumed) += 2;
                i++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "tree") == 0) {
        if (argc < 3) {
            ncd_println("NCD: --agent tree requires a path");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_TREE;
        opts->agent_depth = 3;  /* default tree depth */
        platform_strncpy_s(opts->search, sizeof(opts->search), argv[2]);
        opts->has_search = true;
        *consumed = 2;
        
        /* Parse tree options */
        for (int i = 3; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--flat") == 0) {
                opts->agent_flat = true;
                (*consumed)++;
            } else if (strcmp(opt, "--depth") == 0 && i + 1 < argc) {
                int depth = atoi(argv[i + 1]);
                if (depth < 1) {
                    ncd_println("NCD: --agent tree --depth must be >= 1");
                    return false;
                }
                opts->agent_depth = depth;
                (*consumed) += 2;
                i++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "check") == 0) {
        opts->agent_subcommand = AGENT_SUB_CHECK;
        *consumed = 1;
        
        /* Parse check options */
        for (int i = 2; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--db-age") == 0) {
                opts->agent_check_db_age = true;
                (*consumed)++;
            } else if (strcmp(opt, "--stats") == 0) {
                opts->agent_check_stats = true;
                (*consumed)++;
            } else if (strcmp(opt, "--service-status") == 0) {
                opts->agent_check_service_status = true;
                (*consumed)++;
            } else if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (opt[0] != '-') {
                /* Path argument */
                platform_strncpy_s(opts->search, sizeof(opts->search), opt);
                opts->has_search = true;
                (*consumed)++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "complete") == 0) {
        if (argc < 3) {
            ncd_println("NCD: --agent complete requires a partial path");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_COMPLETE;
        platform_strncpy_s(opts->search, sizeof(opts->search), argv[2]);
        opts->has_search = true;
        *consumed = 2;
        
        /* Parse complete options */
        for (int i = 3; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--limit") == 0 && i + 1 < argc) {
                opts->agent_limit = atoi(argv[i + 1]);
                (*consumed) += 2;
                i++;
            } else {
                break;
            }
        }
        return true;
        
    } else if (_stricmp(sub, "mkdir") == 0) {
        if (argc < 3) {
            ncd_println("NCD: --agent mkdir requires a path");
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
        
    } else if (_stricmp(sub, "mkdirs") == 0) {
        /* mkdirs creates a directory tree from JSON or flat file format */
        opts->agent_subcommand = AGENT_SUB_MKDIRS;
        *consumed = 1;
        
        /* Parse mkdirs options */
        for (int i = 2; i < argc; i++) {
            const char *opt = argv[i];
            if (strcmp(opt, "--json") == 0) {
                opts->agent_json = true;
                (*consumed)++;
            } else if (strcmp(opt, "--file") == 0 && i + 1 < argc) {
                platform_strncpy_s(opts->agent_mkdirs_file, sizeof(opts->agent_mkdirs_file), argv[i + 1]);
                (*consumed) += 2;
                i++;
            } else if (opt[0] != '-') {
                /* JSON content directly as argument */
                platform_strncpy_s(opts->search, sizeof(opts->search), opt);
                opts->has_search = true;
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
        ncd_printf("NCD: unknown --agent subcommand: %s\r\n", sub);
        return false;
    }
}

/* ============================================================= glob matching */

bool glob_match(const char *pattern, const char *text)
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

/* ============================================================= main arg parsing helpers */

static bool apply_short_val(char key, const char *val, NcdOptions *opts)
{
    switch (key) {
        case 'r': case 'R':
            if (strcmp(val, ".") == 0) {
                opts->force_rescan = true;
                platform_get_current_dir(opts->scan_subdirectory, NCD_MAX_PATH);
            } else if (val[0] == '-') {
                if (!parse_skip_drive_list(val + 1, opts->skip_drive_mask, &opts->skip_drive_count)) {
                    ncd_printf("NCD: invalid skip drive list: %s\r\n", val + 1);
                    return false;
                }
                opts->force_rescan = true;
            } else {
                if (!parse_drive_list_token(val, opts->scan_drive_mask, &opts->scan_drive_count)) {
                    ncd_printf("NCD: invalid drive list: %s\r\n", val);
                    return false;
                }
                opts->force_rescan = true;
            }
            break;

        case 'h': case 'H': {
            if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
                opts->history_list = true;
            } else if (_strnicmp(val, "c", 1) == 0) {
                const char *rest = val + 1;
                if (*rest == '\0') {
                    opts->history_clear = true;
                } else if (isdigit((unsigned char)*rest) && rest[1] == '\0') {
                    int idx = *rest - '0';
                    if (idx >= 1 && idx <= 9) {
                        opts->history_remove = idx;
                    } else {
                        ncd_println("NCD: -h:c# index must be 1-9");
                        return false;
                    }
                } else {
                    ncd_println("NCD: invalid history command");
                    return false;
                }
            } else {
                ncd_println("NCD: invalid history command");
                return false;
            }
            break;
        }

        case 'g':
            if (val[0] == '@') {
                opts->group_set = true;
                platform_strncpy_s(opts->group_name, sizeof(opts->group_name), val + 1);
            } else if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
                opts->group_list = true;
            } else {
                ncd_println("NCD: -g requires @name or l/list");
                return false;
            }
            break;

        case 'G':
            if (val[0] == '@') {
                opts->group_remove = true;
                platform_strncpy_s(opts->group_name, sizeof(opts->group_name), val + 1);
            } else if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
                opts->group_list = true;
            } else {
                ncd_println("NCD: -G requires @name or l/list");
                return false;
            }
            break;

        case 'x':
            if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
                opts->exclusion_list = true;
            } else {
                opts->exclusion_add = true;
                platform_strncpy_s(opts->exclusion_pattern, sizeof(opts->exclusion_pattern), val);
            }
            break;

        case 'X':
            if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
                opts->exclusion_list = true;
            } else {
                opts->exclusion_remove = true;
                platform_strncpy_s(opts->exclusion_pattern, sizeof(opts->exclusion_pattern), val);
            }
            break;

        case 'd': case 'D':
            platform_strncpy_s(opts->db_override, sizeof(opts->db_override), val);
            break;

        case 't': case 'T':
            opts->timeout_seconds = atoi(val);
            break;

        case 'u': case 'U':
            if (strcmp(val, "8") == 0) {
                opts->encoding_switch = true;
                opts->text_encoding = NCD_TEXT_UTF8;
            } else if (strcmp(val, "16") == 0) {
                opts->encoding_switch = true;
                opts->text_encoding = NCD_TEXT_UTF16LE;
            } else {
                ncd_println("NCD: -u requires 8 or 16");
                return false;
            }
            break;

        case 'f': case 'F':
            if (_stricmp(val, "c") == 0) {
                opts->clear_history = true;
            } else {
                ncd_println("NCD: -f requires c (clear)");
                return false;
            }
            break;

        default:
            ncd_printf("NCD: unknown option with value: -%c:%s\r\n", key, val);
            return false;
    }
    return true;
}

static bool apply_long_val(const char *name, const char *val, NcdOptions *opts)
{
    if (_stricmp(name, "rescan") == 0) {
        if (strcmp(val, ".") == 0) {
            opts->force_rescan = true;
            platform_get_current_dir(opts->scan_subdirectory, NCD_MAX_PATH);
        } else if (val[0] == '-') {
            if (!parse_skip_drive_list(val + 1, opts->skip_drive_mask, &opts->skip_drive_count)) {
                ncd_printf("NCD: invalid skip drive list: %s\r\n", val + 1);
                return false;
            }
            opts->force_rescan = true;
        } else {
            if (!parse_drive_list_token(val, opts->scan_drive_mask, &opts->scan_drive_count)) {
                ncd_printf("NCD: invalid drive list: %s\r\n", val);
                return false;
            }
            opts->force_rescan = true;
        }
        return true;
    }

    if (_stricmp(name, "history") == 0) {
        if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
            opts->history_list = true;
        } else if (_strnicmp(val, "c", 1) == 0) {
            const char *rest = val + 1;
            if (*rest == '\0') {
                opts->history_clear = true;
            } else if (isdigit((unsigned char)*rest) && rest[1] == '\0') {
                int idx = *rest - '0';
                if (idx >= 1 && idx <= 9) {
                    opts->history_remove = idx;
                } else {
                    ncd_println("NCD: --history:c# index must be 1-9");
                    return false;
                }
            } else {
                ncd_println("NCD: invalid history command");
                return false;
            }
        } else {
            ncd_println("NCD: invalid history command");
            return false;
        }
        return true;
    }

    if (_stricmp(name, "group") == 0) {
        if (val[0] == '@') {
            opts->group_set = true;
            platform_strncpy_s(opts->group_name, sizeof(opts->group_name), val + 1);
        } else if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
            opts->group_list = true;
        } else {
            ncd_println("NCD: --group requires @name or l/list");
            return false;
        }
        return true;
    }

    if (_stricmp(name, "database") == 0) {
        platform_strncpy_s(opts->db_override, sizeof(opts->db_override), val);
        return true;
    }

    if (_stricmp(name, "timeout") == 0) {
        opts->timeout_seconds = atoi(val);
        return true;
    }

    if (_stricmp(name, "retry") == 0) {
        opts->service_retry_count = atoi(val);
        opts->service_retry_set = true;
        return true;
    }

    if (_stricmp(name, "encoding") == 0) {
        if (strcmp(val, "8") == 0) {
            opts->encoding_switch = true;
            opts->text_encoding = NCD_TEXT_UTF8;
        } else if (strcmp(val, "16") == 0) {
            opts->encoding_switch = true;
            opts->text_encoding = NCD_TEXT_UTF16LE;
        } else {
            ncd_println("NCD: --encoding requires 8 or 16");
            return false;
        }
        return true;
    }

    if (_stricmp(name, "conf") == 0) {
        platform_strncpy_s(opts->conf_override, sizeof(opts->conf_override), val);
        return true;
    }

    ncd_printf("NCD: unknown long option: --%s:%s\r\n", name, val);
    return false;
}

/* ============================================================= main arg parsing */

bool parse_args(int argc, char *argv[], NcdOptions *opts)
{
    memset(opts, 0, sizeof(NcdOptions));
    opts->timeout_seconds = 300;  /* default 5 minute timeout */

    if (argc <= 1) {
        opts->show_help = true;
        return true;
    }

    CliToken tokens[MAX_TOKENS];
    int token_count = 0;
    char synth_buf[MAX_SYNTH];

    if (!tokenize_args(argc, argv, tokens, &token_count, synth_buf, sizeof(synth_buf))) {
        ncd_println("NCD: failed to parse command line");
        return false;
    }

    for (int ti = 0; ti < token_count; ti++) {
        CliToken *tok = &tokens[ti];

        switch (tok->type) {
            case TOK_SHORT: {
                char c = tok->s;
                bool needs_val = false;
                bool optional_val = false;
                switch (c) {
                    case 'd': case 'D':
                    case 't': case 'T':
                    case 'u': case 'U':
                    case 'g': case 'G':
                    case 'x': case 'X':
                        needs_val = true;
                        break;
                    case 'h': case 'H':
                    case 'f': case 'F':
                    case 'r': case 'R':
                        optional_val = true;
                        break;
                }
                if (needs_val || optional_val) {
                    if (ti + 1 < token_count && tokens[ti + 1].type == TOK_POSitional) {
                        ti++;
                        if (!apply_short_val(c, tokens[ti].val, opts)) return false;
                        break;
                    } else if (needs_val) {
                        ncd_printf("NCD: -%c requires a value\r\n", c);
                        return false;
                    }
                }
                switch (c) {
                    case 'i': case 'I': opts->show_hidden = true; break;
                    case 's': case 'S': opts->show_system = true; break;
                    case 'a': case 'A': opts->show_hidden = true; opts->show_system = true; break;
                    case 'z': case 'Z': opts->fuzzy_match = true; break;
                    case 'v': case 'V': opts->show_version = true; break;
                    case 'c': case 'C': opts->config_edit = true; break;
                    case 'f': case 'F': opts->show_history = true; break;
                    case 'r': case 'R': opts->force_rescan = true; break;
                    case 'h': case 'H': opts->history_browse = true; break;
                    case '?': opts->show_help = true; break;
                    case '0': opts->history_pingpong = true; break;
                    case '1': case '2': case '3': case '4': case '5':
                    case '6': case '7': case '8': case '9':
                        opts->history_index = c - '0'; break;
                    default:
                        ncd_printf("NCD: unknown option: -%c\r\n", c);
                        return false;
                }
                break;
            }

            case TOK_LONG: {
                const char *name = tok->name;
                bool bool_handled = false;
                if (_stricmp(name, "help") == 0) { opts->show_help = true; bool_handled = true; }
                else if (_stricmp(name, "hidden") == 0) { opts->show_hidden = true; bool_handled = true; }
                else if (_stricmp(name, "system") == 0) { opts->show_system = true; bool_handled = true; }
                else if (_stricmp(name, "all") == 0) { opts->show_hidden = true; opts->show_system = true; bool_handled = true; }
                else if (_stricmp(name, "fuzzy") == 0) { opts->fuzzy_match = true; bool_handled = true; }
                else if (_stricmp(name, "version") == 0) { opts->show_version = true; bool_handled = true; }
                else if (_stricmp(name, "config") == 0) { opts->config_edit = true; bool_handled = true; }
                else if (_stricmp(name, "frequent") == 0) { opts->show_history = true; bool_handled = true; }
                else if (_stricmp(name, "agdb") == 0) { opts->agentic_debug = true; bool_handled = true; }

                if (bool_handled) break;

                /* Long options that may take a space-separated value */
                bool needs_val = false;
                if (_stricmp(name, "conf") == 0 ||
                    _stricmp(name, "rescan") == 0 ||
                    _stricmp(name, "history") == 0 ||
                    _stricmp(name, "group") == 0 ||
                    _stricmp(name, "database") == 0 ||
                    _stricmp(name, "timeout") == 0 ||
                    _stricmp(name, "retry") == 0 ||
                    _stricmp(name, "encoding") == 0) {
                    needs_val = true;
                } else if (_stricmp(name, "agent") == 0) {
                    if (ti + 1 < token_count && tokens[ti + 1].type == TOK_POSitional) {
                        ti++;
                        opts->agent_mode = true;
                        /* Reconstruct argv so parse_agent_args sees subcommand in argv[1] */
                        int remaining = argc - tok->orig_idx;
                        char **agent_argv = (char **)malloc((size_t)(remaining + 1) * sizeof(char *));
                        if (!agent_argv) return false;
                        agent_argv[0] = argv[0];
                        agent_argv[1] = (char *)tokens[ti].val;
                        for (int j = 2; j < remaining; j++) {
                            agent_argv[j] = argv[tok->orig_idx + j];
                        }
                        agent_argv[remaining] = NULL;
                        int consumed = 0;
                        bool ok = parse_agent_args(remaining, agent_argv, &consumed, opts);
                        free(agent_argv);
                        if (!ok) return false;
                        /* Advance past consumed tokens */
                        int end_idx = tok->orig_idx + consumed;
                        while (ti + 1 < token_count && tokens[ti + 1].orig_idx <= end_idx) {
                            ti++;
                        }
                        break;
                    } else {
                        ncd_println("NCD: --agent requires a subcommand");
                        return false;
                    }
                }

                if (needs_val) {
                    if (ti + 1 < token_count && tokens[ti + 1].type == TOK_POSitional) {
                        ti++;
                        if (!apply_long_val(name, tokens[ti].val, opts)) return false;
                        break;
                    } else {
                        ncd_printf("NCD: --%s requires a value\r\n", name);
                        return false;
                    }
                }

                ncd_printf("NCD: unknown option: --%s\r\n", name);
                return false;
            }

            case TOK_SHORT_VAL: {
                /* Special case: -g-, /g-, -x-, /x- mean "remove" and the next token is the value */
                if ((tok->s == 'g' || tok->s == 'G' || tok->s == 'x' || tok->s == 'X') && strcmp(tok->val, "-") == 0) {
                    if (ti + 1 < token_count && tokens[ti + 1].type == TOK_POSitional) {
                        ti++;
                        const char *val = tokens[ti].val;
                        if (tok->s == 'g' || tok->s == 'G') {
                            if (val[0] == '@') {
                                opts->group_remove = true;
                                platform_strncpy_s(opts->group_name, sizeof(opts->group_name), val + 1);
                            } else if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
                                opts->group_list = true;
                            } else {
                                ncd_println("NCD: -g- requires @name or l/list");
                                return false;
                            }
                        } else {
                            if (_stricmp(val, "l") == 0 || _stricmp(val, "list") == 0) {
                                opts->exclusion_list = true;
                            } else {
                                opts->exclusion_remove = true;
                                platform_strncpy_s(opts->exclusion_pattern, sizeof(opts->exclusion_pattern), val);
                            }
                        }
                        break;
                    } else {
                        ncd_printf("NCD: -%c- requires a value\r\n", tok->s);
                        return false;
                    }
                }
                if (!apply_short_val(tok->s, tok->val, opts)) return false;
                break;
            }

            case TOK_LONG_VAL: {
                if (_stricmp(tok->name, "agent") == 0) {
                    opts->agent_mode = true;
                    /* Reconstruct argv so parse_agent_args sees subcommand in argv[1] */
                    int remaining = argc - tok->orig_idx + 1;
                    char **agent_argv = (char **)malloc((size_t)(remaining + 1) * sizeof(char *));
                    if (!agent_argv) return false;
                    agent_argv[0] = argv[0];
                    agent_argv[1] = (char *)tok->val;
                    for (int j = 2; j < remaining; j++) {
                        agent_argv[j] = argv[tok->orig_idx + j - 1];
                    }
                    agent_argv[remaining] = NULL;
                    int consumed = 0;
                    bool ok = parse_agent_args(remaining, agent_argv, &consumed, opts);
                    free(agent_argv);
                    if (!ok) return false;
                    /* Advance past consumed tokens */
                    int end_idx = tok->orig_idx + consumed - 1;
                    while (ti + 1 < token_count && tokens[ti + 1].orig_idx <= end_idx) {
                        ti++;
                    }
                } else {
                    if (!apply_long_val(tok->name, tok->val, opts)) return false;
                }
                break;
            }

            case TOK_POSitional:
                platform_strncpy_s(opts->search, sizeof(opts->search), tok->val);
                opts->has_search = true;
                break;
        }
    }

    return true;
}
