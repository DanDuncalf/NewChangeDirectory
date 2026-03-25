/*
 * cli.c  --  Command-line interface parsing for NCD
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

/* ============================================================= drive list parsing */

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

/* ============================================================= agent mode arg parsing */

bool parse_agent_args(int argc, char *argv[], int *consumed, NcdOptions *opts)
{
    if (argc < 2) {
        ncd_println("NCD: /agent requires a subcommand");
        return false;
    }
    
    const char *sub = argv[1];
    *consumed = 0;
    
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
            } else if (strcmp(opt, "--depth") == 0) {
                if (i + 1 < argc) {
                    opts->agent_depth_sort = true;
                    (*consumed) += 2;
                    i++;
                }
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
            ncd_println("NCD: /agent ls requires a path");
            return false;
        }
        opts->agent_subcommand = AGENT_SUB_LS;
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
            } else if (strcmp(opt, "--depth") == 0 && i + 1 < argc) {
                opts->agent_depth = atoi(argv[i + 1]);
                (*consumed) += 2;
                i++;
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
                opts->agent_depth = atoi(argv[i + 1]);
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
            ncd_println("NCD: /agent complete requires a partial path");
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

/* ============================================================= main arg parsing */

bool parse_args(int argc, char *argv[], NcdOptions *opts)
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

        /* Combined flags: /i, /s, /a, /z, /v */
        if (arg[0] == '/' || arg[0] == '-') {
            const char *p = arg + 1;
            bool unknown = false;
            
            while (*p) {
                char c = (char)toupper((unsigned char)*p);
                
                switch (c) {
                    case 'I':
                        opts->show_hidden = true;
                        break;
                    case 'S':
                        opts->show_system = true;
                        break;
                    case 'A':
                        opts->show_hidden = true;
                        opts->show_system = true;
                        break;
                    case 'Z':
                        opts->fuzzy_match = true;
                        break;
                    case 'V':
                        opts->show_version = true;
                        break;
                    case 'R':
                        /* /r alone - rescan all */
                        if (p[1] == '\0') {
                            opts->force_rescan = true;
                        }
                        break;
                    default:
                        unknown = true;
                        break;
                }
                p++;
            }
            
            if (!unknown || opts->show_hidden || opts->show_system || 
                opts->fuzzy_match || opts->show_version || opts->force_rescan) {
                continue;
            }
            /* Unknown option - fall through to error */
        }

        /* Tag list: /gl or /gL */
        if (_stricmp(arg, "/gl") == 0 || _stricmp(arg, "-gl") == 0 ||
            _stricmp(arg, "/gL") == 0 || _stricmp(arg, "-gL") == 0) {
            opts->group_list = true;
            continue;
        }

        /* Tag remove: /g- @name */
        if (_stricmp(arg, "/g-") == 0 || _stricmp(arg, "-g-") == 0) {
            if (i + 1 < argc) {
                const char *next = argv[++i];
                if (next[0] == '@') {
                    opts->group_remove = true;
                    platform_strncpy_s(opts->group_name, sizeof(opts->group_name), next + 1);
                    continue;
                }
            }
            ncd_println("NCD: /g- requires @name argument");
            return false;
        }

        /* Tag set: /g @name */
        if (_stricmp(arg, "/g") == 0 || _stricmp(arg, "-g") == 0) {
            if (i + 1 < argc) {
                const char *next = argv[++i];
                if (next[0] == '@') {
                    opts->group_set = true;
                    platform_strncpy_s(opts->group_name, sizeof(opts->group_name), next + 1);
                    continue;
                }
            }
            ncd_println("NCD: /g requires @name argument");
            return false;
        }

        /* Config editor: /c */
        if (_stricmp(arg, "/c") == 0 || _stricmp(arg, "-c") == 0) {
            opts->config_edit = true;
            continue;
        }

        /* Database override: /d <path> */
        if (_stricmp(arg, "/d") == 0 || _stricmp(arg, "-d") == 0) {
            if (i + 1 < argc) {
                platform_strncpy_s(opts->db_override, sizeof(opts->db_override), argv[++i]);
                continue;
            }
            ncd_println("NCD: /d requires a path argument");
            return false;
        }

        /* Timeout: /t <seconds> */
        if (_stricmp(arg, "/t") == 0 || _stricmp(arg, "-t") == 0 ||
            (_strnicmp(arg, "/t", 2) == 0 && isdigit((unsigned char)arg[2]))) {
            if (arg[2] && isdigit((unsigned char)arg[2])) {
                /* /t30 form (no space) */
                opts->timeout_seconds = atoi(arg + 2);
            } else if (i + 1 < argc) {
                /* /t 30 form (with space) */
                opts->timeout_seconds = atoi(argv[++i]);
            } else {
                ncd_println("NCD: /t requires a seconds argument");
                return false;
            }
            continue;
        }

        /* Agent mode: /agent <subcommand> */
        if (_stricmp(arg, "/agent") == 0 || _stricmp(arg, "-agent") == 0 ||
            _stricmp(arg, "/a") == 0 || _stricmp(arg, "-a") == 0) {
            opts->agent_mode = true;
            
            if (i + 1 < argc) {
                int consumed = 0;
                int remaining = argc - i;
                bool ok = parse_agent_args(remaining, &argv[i], &consumed, opts);
                if (!ok) return false;
                i += consumed;
                continue;
            } else {
                ncd_println("NCD: /agent requires a subcommand");
                return false;
            }
        }

        /* Exclusion add: -x <pattern> */
        if (strcmp(arg, "-x") == 0) {
            if (i + 1 < argc) {
                opts->exclusion_add = true;
                platform_strncpy_s(opts->exclusion_pattern, sizeof(opts->exclusion_pattern), argv[++i]);
                continue;
            }
            ncd_println("NCD: -x requires a pattern argument");
            return false;
        }

        /* Exclusion remove: -x- <pattern> */
        if (strcmp(arg, "-x-") == 0) {
            if (i + 1 < argc) {
                opts->exclusion_remove = true;
                platform_strncpy_s(opts->exclusion_pattern, sizeof(opts->exclusion_pattern), argv[++i]);
                continue;
            }
            ncd_println("NCD: -x- requires a pattern argument");
            return false;
        }

        /* Exclusion list: -xl */
        if (strcmp(arg, "-xl") == 0) {
            opts->exclusion_list = true;
            continue;
        }

        /* Retry count: /retry <n> */
        if (_stricmp(arg, "/retry") == 0 || _stricmp(arg, "-retry") == 0) {
            if (i + 1 < argc) {
                opts->service_retry_count = atoi(argv[++i]);
                opts->service_retry_set = true;
                continue;
            }
            ncd_println("NCD: /retry requires a count argument");
            return false;
        }

        /* Unknown option */
        if (arg[0] == '/' || arg[0] == '-') {
            ncd_printf("NCD: unknown option: %s\r\n", arg);
            return false;
        }

        /* Default: search term */
        platform_strncpy_s(opts->search, sizeof(opts->search), arg);
        opts->has_search = true;
    }

    return true;
}
