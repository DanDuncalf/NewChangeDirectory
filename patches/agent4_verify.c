/*
 * patches/agent4_verify.c
 *
 * Agent 4 implementation: --agent:verify and --agent:chmod
 *
 * This file contains complete implementations to be integrated into the
 * following source files.  Each function is labeled with its target file.
 */

/* ================================================================
 * Target file: ../shared/platform.c
 * Replace the existing stub: platform_set_mode
 * ================================================================ */
bool platform_set_mode(const char *path, int mode)
{
    if (!path) return false;
#if PLATFORM_WINDOWS
    (void)mode;
    return true; /* No-op on Windows */
#else
    return chmod(path, (mode_t)mode) == 0;
#endif
}

/* ================================================================
 * Target file: ../shared/platform.c
 * Replace the existing stub: platform_get_mode
 * ================================================================ */
int platform_get_mode(const char *path)
{
    if (!path) return -1;
#if PLATFORM_WINDOWS
    return -1; /* Not supported on Windows */
#else
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int)(st.st_mode & 0777);
#endif
}

/* ================================================================
 * Target file: src/main.c
 * Replace the existing stub: agent_mode_verify
 *
 * Helpers declared below: verify_dir_is_empty, verify_tree_recursive
 * ================================================================ */

/* Check whether a directory contains any entries other than . and .. */
static bool verify_dir_is_empty(const char *path)
{
    if (!path) return false;
#if NCD_PLATFORM_WINDOWS
    char search_path[NCD_MAX_PATH];
    size_t len = strlen(path);
    if (len > 0 && (path[len - 1] == '\\' || path[len - 1] == '/')) {
        snprintf(search_path, sizeof(search_path), "%s*", path);
    } else {
        snprintf(search_path, sizeof(search_path), "%s\\*", path);
    }
    WIN32_FIND_DATAA find_data;
    HANDLE h = FindFirstFileA(search_path, &find_data);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool empty = true;
    do {
        const char *name = find_data.cFileName;
        if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileA(h, &find_data));
    FindClose(h);
    return empty;
#else
    DIR *dir = opendir(path);
    if (!dir) return false;
    struct dirent *ent;
    bool empty = true;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            empty = false;
            break;
        }
    }
    closedir(dir);
    return empty;
#endif
}

/* Recursively verify that every node in 'node' exists as a directory
 * under 'base_path'.  The tree spec root is a dummy; its children are
 * the first level to verify. */
static bool verify_tree_recursive(const char *base_path, const MkdirsNode *node)
{
    if (!node || !base_path) return true;

    char base[NCD_MAX_PATH];
    platform_strncpy_s(base, sizeof(base), base_path);
    size_t base_len = strlen(base);
    while (base_len > 0 && (base[base_len - 1] == '\\' || base[base_len - 1] == '/')) {
        base[base_len - 1] = '\0';
        base_len--;
    }

    for (int i = 0; i < node->child_count; i++) {
        const MkdirsNode *child = &node->children[i];
        char path[NCD_MAX_PATH];
        snprintf(path, sizeof(path), "%s%s%s", base, NCD_PATH_SEP, child->name);
        if (!platform_dir_exists(path))
            return false;
        if (!verify_tree_recursive(path, child))
            return false;
    }
    return true;
}

static int agent_mode_verify(const NcdOptions *opts)
{
    if (!opts->has_search || !opts->search[0]) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"error\":\"no path specified\",\"verified\":false}\r\n");
        } else {
            agent_print("ERROR: No path specified\r\n");
        }
        return 1;
    }

    const char *path = opts->search;
    bool all_passed = true;
    int check_count = 0;

    #define MAX_CHECKS 8
    struct {
        const char *name;
        bool passed;
        const char *expected;
        const char *actual;
    } checks[MAX_CHECKS];

    #define ADD_CHECK(n, p, e, a) do { \
        if (check_count < MAX_CHECKS) { \
            checks[check_count].name = (n); \
            checks[check_count].passed = (p); \
            checks[check_count].expected = (e); \
            checks[check_count].actual = (a); \
            check_count++; \
            if (!(p)) all_passed = false; \
        } \
    } while (0)

    /* 1. Exists */
    bool exists = platform_dir_exists(path);
    ADD_CHECK("exists", exists, "true", exists ? "true" : "false");

    /* 2. Is directory */
    ADD_CHECK("is_directory", exists, "true", exists ? "true" : "false");

    if (!exists) {
        all_passed = false;
    } else {
        /* 3. Empty */
        if (opts->agent_dirs_only) { /* --empty reuses agent_dirs_only */
            bool empty = verify_dir_is_empty(path);
            ADD_CHECK("empty", empty, "true", empty ? "true" : "false");
        }

        /* 4. Mode */
        if (opts->agent_mode_octal >= 0) {
#if NCD_PLATFORM_WINDOWS
            ADD_CHECK("mode", true, "n/a", "n/a");
#else
            int actual_mode = platform_get_mode(path);
            char expected_str[8];
            char actual_str[8];
            snprintf(expected_str, sizeof(expected_str), "%04o", opts->agent_mode_octal);
            snprintf(actual_str, sizeof(actual_str), "%04o", actual_mode);
            bool mode_ok = (actual_mode == opts->agent_mode_octal);
            ADD_CHECK("mode", mode_ok, expected_str,
                      actual_mode >= 0 ? actual_str : "error");
#endif
        }

        /* 5. Tree structure */
        if (opts->agent_tree_file[0]) {
            char *file_content = read_file_contents(opts->agent_tree_file);
            if (!file_content) {
                ADD_CHECK("tree_structure", false, "readable spec",
                          "failed to read file");
            } else {
                MkdirsNode root;
                memset(&root, 0, sizeof(root));

                const char *p = file_content;
                while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
                bool is_json = (*p == '[' || *p == '{');

                bool parse_ok;
                if (is_json) {
                    const char *jp = json_skip_ws(file_content);
                    parse_ok = (json_parse_dirs(jp, &root) != NULL);
                } else {
                    parse_ok = parse_flat_format(file_content, &root);
                }

                free(file_content);

                if (!parse_ok) {
                    ADD_CHECK("tree_structure", false, "valid spec", "parse error");
                } else {
                    bool tree_ok = verify_tree_recursive(path, &root);
                    ADD_CHECK("tree_structure", tree_ok, "matches spec",
                              tree_ok ? "matches" : "mismatch");
                    mkdirs_free_tree(&root);
                }
            }
        }
    }

    #undef ADD_CHECK
    #undef MAX_CHECKS

    /* Output */
    if (opts->agent_json) {
        agent_print("{\"v\":1,\"path\":\"");
        agent_json_escape(path);
        agent_printf("\",\"verified\":%s,\"checks\":[", all_passed ? "true" : "false");
        for (int i = 0; i < check_count; i++) {
            if (i > 0) agent_print(",");
            agent_print("{\"check\":\"");
            agent_json_escape(checks[i].name);
            agent_printf("\",\"passed\":%s", checks[i].passed ? "true" : "false");
            if (checks[i].expected) {
                agent_print(",\"expected\":\"");
                agent_json_escape(checks[i].expected);
                agent_print("\"");
            }
            if (checks[i].actual) {
                agent_print(",\"actual\":\"");
                agent_json_escape(checks[i].actual);
                agent_print("\"");
            }
            agent_print("}");
        }
        agent_print("]}\r\n");
    } else {
        agent_printf("Verify: %s\r\n", all_passed ? "PASSED" : "FAILED");
        for (int i = 0; i < check_count; i++) {
            agent_printf("  [%s] %s", checks[i].passed ? "PASS" : "FAIL",
                         checks[i].name);
            if (checks[i].expected && checks[i].actual) {
                agent_printf(" (expected: %s, actual: %s)",
                             checks[i].expected, checks[i].actual);
            }
            agent_print("\r\n");
        }
    }

    return all_passed ? 0 : 1;
}

/* ================================================================
 * Target file: src/main.c
 * Replace the existing stub: agent_mode_chmod
 *
 * Helper: chmod_recursive (Linux only)
 * ================================================================ */

#if NCD_PLATFORM_LINUX
static int chmod_recursive(const char *path, int mode)
{
    int changed = 0;
    if (platform_set_mode(path, mode))
        changed++;
    else
        return -1;

    DIR *dir = opendir(path);
    if (!dir) return changed;

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char child_path[NCD_MAX_PATH];
        snprintf(child_path, sizeof(child_path), "%s/%s", path, ent->d_name);

        struct stat st;
        if (stat(child_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            int sub = chmod_recursive(child_path, mode);
            if (sub < 0) {
                closedir(dir);
                return -1;
            }
            changed += sub;
        }
    }
    closedir(dir);
    return changed;
}
#else
static int chmod_recursive(const char *path, int mode)
{
    (void)path;
    (void)mode;
    return 0;
}
#endif

static int agent_mode_chmod(const NcdOptions *opts)
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
    int mode = opts->agent_mode_octal;

#if NCD_PLATFORM_WINDOWS
    (void)mode;
    if (opts->agent_json) {
        agent_print("{\"v\":1,\"path\":\"");
        agent_json_escape(path);
        agent_print("\",\"mode\":\"n/a\",\"result\":\"error_unsupported\",\"changed\":0}\r\n");
    } else {
        agent_print("ERROR: chmod is not supported on Windows\r\n");
    }
    return 1;
#else
    if (mode < 0) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"path\":\"");
            agent_json_escape(path);
            agent_print("\",\"error\":\"no mode specified\",\"result\":\"error\"}\r\n");
        } else {
            agent_print("ERROR: No mode specified\r\n");
        }
        return 1;
    }

    int changed = 0;
    bool ok = true;

    if (opts->agent_recursive) {
        changed = chmod_recursive(path, mode);
        ok = (changed >= 0);
        if (changed < 0) changed = 0;
    } else {
        if (platform_set_mode(path, mode)) {
            changed = 1;
        } else {
            ok = false;
        }
    }

    char mode_str[8];
    snprintf(mode_str, sizeof(mode_str), "%04o", mode);

    if (opts->agent_json) {
        agent_print("{\"v\":1,\"path\":\"");
        agent_json_escape(path);
        agent_printf("\",\"mode\":\"%s\",\"result\":\"%s\",\"changed\":%d}\r\n",
                     mode_str, ok ? "changed" : "error", changed);
    } else {
        if (ok) {
            agent_printf("Changed mode to %s for %d entries\r\n", mode_str, changed);
        } else {
            agent_printf("ERROR: Failed to change mode for %s\r\n", path);
        }
    }

    return ok ? 0 : 1;
#endif
}
