/* ================================================================
 * Agent 1 — Enhanced --agent:mkdir and --agent:mkdirs
 *
 * This file contains the complete replacement implementations for:
 *   agent_mode_mkdir()
 *   agent_mode_mkdirs()
 * and all new helper types / functions they require.
 *
 * Paste this block into src/main.c in place of the existing
 * agent_mode_mkdir / agent_mode_mkdirs / mkdirs_create_recursive
 * and associated helpers.
 * ================================================================ */

/* ------------------------------------------------------------------
 * Additional result codes (the original AgentMkdirResult enum ends
 * at AGENT_MKDIR_ERROR_OTHER == 5).
 * ------------------------------------------------------------------ */
#define AGENT_MKDIR_VERIFIED        6
#define AGENT_MKDIR_ERROR_NOT_EMPTY 7

/* ------------------------------------------------------------------
 * AgentJsonBuffer  —  lightweight growable char buffer used to
 * accumulate a complete JSON document before emitting it with a
 * single agent_print() call.
 * ------------------------------------------------------------------ */
typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} AgentJsonBuffer;

static void ajb_init(AgentJsonBuffer *b)
{
    b->cap = 1024;
    b->buf = (char *)malloc(b->cap);
    b->len = 0;
    if (b->buf) b->buf[0] = '\0';
}

static void ajb_free(AgentJsonBuffer *b)
{
    free(b->buf);
    b->buf = NULL;
    b->len = 0;
    b->cap = 0;
}

static bool ajb_ensure(AgentJsonBuffer *b, size_t need)
{
    if (!b->buf) return false;
    if (b->len + need + 1 <= b->cap) return true;
    size_t new_cap = b->cap * 2;
    while (new_cap < b->len + need + 1) new_cap *= 2;
    char *new_buf = (char *)realloc(b->buf, new_cap);
    if (!new_buf) return false;
    b->buf = new_buf;
    b->cap = new_cap;
    return true;
}

static void ajb_append(AgentJsonBuffer *b, const char *s)
{
    if (!b->buf) return;
    size_t n = strlen(s);
    if (!ajb_ensure(b, n)) return;
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = '\0';
}

static void ajb_appendf(AgentJsonBuffer *b, const char *fmt, ...)
{
    if (!b->buf) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (!ajb_ensure(b, (size_t)n + 1)) return;
    va_start(ap, fmt);
    vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    b->buf[b->len] = '\0';
}

static void ajb_append_json_escape(AgentJsonBuffer *b, const char *s)
{
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '\\': ajb_append(b, "\\\\"); break;
            case '"':  ajb_append(b, "\\\""); break;
            case '\b': ajb_append(b, "\\b"); break;
            case '\f': ajb_append(b, "\\f"); break;
            case '\n': ajb_append(b, "\\n"); break;
            case '\r': ajb_append(b, "\\r"); break;
            case '\t': ajb_append(b, "\\t"); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char tmp[7];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned char)*p);
                    ajb_append(b, tmp);
                } else {
                    char tmp[2] = { *p, '\0' };
                    ajb_append(b, tmp);
                }
                break;
        }
    }
}

/* ------------------------------------------------------------------
 * AgentTxn  —  tracks directories created during an atomic mkdirs
 * so they can be rolled back on failure.
 * ------------------------------------------------------------------ */
typedef struct {
    char **paths;
    int    count;
    int    capacity;
} AgentTxn;

static void agent_txn_init(AgentTxn *txn)
{
    txn->paths = NULL;
    txn->count = 0;
    txn->capacity = 0;
}

static void agent_txn_free(AgentTxn *txn)
{
    for (int i = 0; i < txn->count; i++) {
        free(txn->paths[i]);
    }
    free(txn->paths);
    txn->paths = NULL;
    txn->count = 0;
    txn->capacity = 0;
}

static void agent_txn_add(AgentTxn *txn, const char *path)
{
    if (txn->count >= txn->capacity) {
        int new_cap = txn->capacity ? txn->capacity * 2 : 4;
        char **new_paths = (char **)realloc(txn->paths, (size_t)new_cap * sizeof(char *));
        if (!new_paths) return;
        txn->paths = new_paths;
        txn->capacity = new_cap;
    }
    txn->paths[txn->count] = strdup(path);
    if (txn->paths[txn->count]) {
        txn->count++;
    }
}

static bool agent_txn_rollback(AgentTxn *txn)
{
    bool ok = true;
    for (int i = txn->count - 1; i >= 0; i--) {
        if (!platform_remove_dir(txn->paths[i])) {
            ok = false;
        }
    }
    agent_txn_free(txn);
    return ok;
}

/* ------------------------------------------------------------------
 * read_stdin_all  —  slurp entire stdin into a malloc'd string.
 * ------------------------------------------------------------------ */
static char *read_stdin_all(void)
{
    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) return NULL;

    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (len + 1 >= cap) {
            size_t new_cap = cap * 2;
            char *new_buf = (char *)realloc(buf, new_cap);
            if (!new_buf) { free(buf); return NULL; }
            buf = new_buf;
            cap = new_cap;
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

/* ------------------------------------------------------------------
 * MkdirsJsonBuilder  —  incremental JSON builder for mkdirs output.
 * ------------------------------------------------------------------ */
typedef struct {
    AgentJsonBuffer buf;
    int             dir_count;
    int             rollback_count;
    bool            use_json;
} MkdirsJsonBuilder;

static void mjb_init(MkdirsJsonBuilder *b, bool use_json, bool atomic)
{
    b->use_json = use_json;
    b->dir_count = 0;
    b->rollback_count = 0;
    if (use_json) {
        ajb_init(&b->buf);
        ajb_appendf(&b->buf, "{\"v\":1,\"atomic\":%s,\"dirs\":[",
                    atomic ? "true" : "false");
    }
}

static void mjb_add_dir(MkdirsJsonBuilder *b, const char *path,
                        const char *result, const char *message)
{
    if (!b->use_json) return;
    if (b->dir_count > 0) ajb_append(&b->buf, ",");
    ajb_append(&b->buf, "{\"path\":\"");
    ajb_append_json_escape(&b->buf, path);
    ajb_append(&b->buf, "\",\"result\":\"");
    ajb_append_json_escape(&b->buf, result);
    ajb_append(&b->buf, "\"");
    if (message && message[0]) {
        ajb_append(&b->buf, ",\"message\":\"");
        ajb_append_json_escape(&b->buf, message);
        ajb_append(&b->buf, "\"");
    }
    ajb_append(&b->buf, "}");
    b->dir_count++;
}

static void mjb_start_rollback(MkdirsJsonBuilder *b)
{
    if (!b->use_json) return;
    ajb_append(&b->buf, "],\"rollback\":[");
}

static void mjb_add_rollback(MkdirsJsonBuilder *b, const char *path)
{
    if (!b->use_json) return;
    if (b->rollback_count > 0) ajb_append(&b->buf, ",");
    ajb_append(&b->buf, "{\"path\":\"");
    ajb_append_json_escape(&b->buf, path);
    ajb_append(&b->buf, "\",\"result\":\"removed\"}");
    b->rollback_count++;
}

static void mjb_finish(MkdirsJsonBuilder *b, int created, int existed,
                       int failed, int rolled_back)
{
    if (!b->use_json) return;
    if (b->rollback_count == 0) {
        ajb_append(&b->buf, "],\"rollback\":[]");
    }
    ajb_appendf(&b->buf, ",\"summary\":{\"created\":%d,\"exists\":%d,\"failed\":%d",
                created, existed, failed);
    if (rolled_back >= 0) {
        ajb_appendf(&b->buf, ",\"rolled_back\":%d", rolled_back);
    }
    ajb_append(&b->buf, "}}\r\n");
}

static void mjb_emit(MkdirsJsonBuilder *b)
{
    if (b->use_json && b->buf.buf) {
        agent_print(b->buf.buf);
    }
}

static void mjb_free(MkdirsJsonBuilder *b)
{
    if (b->use_json) {
        ajb_free(&b->buf);
    }
}

/* ------------------------------------------------------------------
 * MkdirsStats  —  simple accumulator used during tree traversal.
 * ------------------------------------------------------------------ */
typedef struct {
    int  created;
    int  existed;
    int  failed;
    bool stopped;
} MkdirsStats;

/* ------------------------------------------------------------------
 * mkdirs_validate_atomic  —  first pass for --atomic.
 * Checks structural conflicts (file-in-the-way, non-empty dir when
 * --force is set) before anything is created.
 * ------------------------------------------------------------------ */
static bool mkdirs_validate_atomic(const char *base_path, MkdirsNode *node,
                                   const NcdOptions *opts,
                                   char *fail_path, size_t fail_path_size,
                                   const char **fail_msg)
{
    for (int i = 0; i < node->child_count; i++) {
        MkdirsNode *child = &node->children[i];
        char path[NCD_MAX_PATH];
        if (base_path[0]) {
            snprintf(path, sizeof(path), "%s%s%s",
                     base_path, NCD_PATH_SEP, child->name);
        } else {
            platform_strncpy_s(path, sizeof(path), child->name);
        }

        if (platform_file_exists(path)) {
            platform_strncpy_s(fail_path, fail_path_size, path);
            *fail_msg = "Path exists but is not a directory";
            return false;
        }

        if (platform_dir_exists(path)) {
            if (opts->agent_force) {
                if (!platform_dir_is_empty(path)) {
                    platform_strncpy_s(fail_path, fail_path_size, path);
                    *fail_msg = "Directory exists and is not empty";
                    return false;
                }
            }
        }

        if (child->child_count > 0) {
            if (!mkdirs_validate_atomic(path, child, opts,
                                        fail_path, fail_path_size, fail_msg)) {
                return false;
            }
        }
    }
    return true;
}

/* ------------------------------------------------------------------
 * mkdirs_process  —  recursive worker for mkdirs.
 * Handles verify, dry-run, and actual creation.
 * ------------------------------------------------------------------ */
static bool mkdirs_process(const char *base_path, MkdirsNode *node,
                           const NcdOptions *opts,
                           MkdirsJsonBuilder *json,
                           AgentTxn *txn,
                           MkdirsStats *stats)
{
    for (int i = 0; i < node->child_count; i++) {
        MkdirsNode *child = &node->children[i];
        char path[NCD_MAX_PATH];
        if (base_path[0]) {
            snprintf(path, sizeof(path), "%s%s%s",
                     base_path, NCD_PATH_SEP, child->name);
        } else {
            platform_strncpy_s(path, sizeof(path), child->name);
        }

        const char *result_code = "created";
        const char *message = "Directory created";
        bool success = true;
        bool dir_existed = false;
        bool was_created = false;

        if (opts->agent_verify) {
            if (platform_dir_exists(path)) {
                if (opts->agent_mode_octal >= 0) {
                    int actual = platform_get_mode(path);
                    if (actual >= 0 && actual == opts->agent_mode_octal) {
                        result_code = "exists";
                        message = "Directory exists and mode matches";
                        dir_existed = true;
                    } else {
                        result_code = "error";
                        message = "Directory exists but mode does not match";
                        success = false;
                    }
                } else {
                    result_code = "exists";
                    message = "Directory already exists";
                    dir_existed = true;
                }
            } else {
                result_code = "error";
                message = "Directory does not exist";
                success = false;
            }
        } else if (opts->agent_dry_run) {
            if (platform_dir_exists(path)) {
                if (opts->agent_force) {
                    if (platform_dir_is_empty(path)) {
                        result_code = "created";
                        message = "Directory would be recreated";
                    } else {
                        result_code = "error_not_empty";
                        message = "Directory exists and is not empty";
                        success = false;
                    }
                } else {
                    result_code = "exists";
                    message = "Directory already exists";
                    dir_existed = true;
                }
            } else {
                char parent[NCD_MAX_PATH];
                bool parent_ok = true;
                if (path_parent(path, parent, sizeof(parent))) {
                    if (!platform_dir_exists(parent) && opts->agent_parents_required) {
                        parent_ok = false;
                    }
                }
                if (parent_ok) {
                    result_code = "created";
                    message = "Directory would be created";
                } else {
                    result_code = "error_parent";
                    message = "Parent directory does not exist";
                    success = false;
                }
            }
        } else {
            /* ---- actual creation ---- */
            if (platform_dir_exists(path)) {
                if (opts->agent_force) {
                    if (platform_dir_is_empty(path)) {
                        if (platform_remove_dir(path) && platform_create_dir(path)) {
                            result_code = "created";
                            message = "Directory recreated";
                            was_created = true;
                            if (opts->agent_mode_octal >= 0)
                                platform_set_mode(path, opts->agent_mode_octal);
                        } else {
                            result_code = "error";
                            message = "Failed to recreate directory";
                            success = false;
                        }
                    } else {
                        result_code = "error_not_empty";
                        message = "Directory exists and is not empty";
                        success = false;
                    }
                } else {
                    result_code = "exists";
                    message = "Directory already exists";
                    dir_existed = true;
                }
            } else {
                char parent[NCD_MAX_PATH];
                bool parent_ok = true;
                if (path_parent(path, parent, sizeof(parent))) {
                    if (!platform_dir_exists(parent)) {
                        if (opts->agent_parents_required) {
                            parent_ok = false;
                        } else {
                            if (!platform_create_dir(parent)) {
                                parent_ok = false;
                            }
                        }
                    }
                }
                if (parent_ok) {
                    if (platform_create_dir(path)) {
                        result_code = "created";
                        message = "Directory created";
                        was_created = true;
                        if (opts->agent_mode_octal >= 0)
                            platform_set_mode(path, opts->agent_mode_octal);
                    } else {
#if NCD_PLATFORM_WINDOWS
                        DWORD err = GetLastError();
                        if (err == ERROR_ACCESS_DENIED) {
                            result_code = "error_perms";
                            message = "Permission denied";
                        } else {
                            result_code = "error";
                            message = "Failed to create directory";
                        }
#else
                        if (errno == EACCES || errno == EPERM) {
                            result_code = "error_perms";
                            message = "Permission denied";
                        } else {
                            result_code = "error";
                            message = "Failed to create directory";
                        }
#endif
                        success = false;
                    }
                } else {
                    result_code = "error_parent";
                    message = "Parent directory does not exist";
                    success = false;
                }
            }
        }

        if (was_created) {
            stats->created++;
            if (txn) {
                agent_txn_add(txn, path);
            } else {
                add_path_to_database(path);
            }
        } else if (dir_existed) {
            stats->existed++;
        } else if (!success) {
            stats->failed++;
        }

        if (json && json->use_json) {
            mjb_add_dir(json, path, result_code, message);
        } else if (!opts->agent_json) {
            agent_print(path);
            agent_print(": ");
            agent_print(message);
            agent_print("\r\n");
        }

        /* recurse */
        if (child->child_count > 0) {
            bool child_ok = mkdirs_process(path, child, opts, json, txn, stats);
            if (!child_ok) {
                stats->stopped = true;
                return false;
            }
        }

        if (!success && opts->agent_stop_on_error) {
            stats->stopped = true;
            return false;
        }
    }
    return true;
}

/* ==================================================================
 * agent_mode_mkdir  —  enhanced single-directory creation
 * ================================================================== */
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
    int    result = AGENT_MKDIR_ERROR_OTHER;
    char   result_msg[256] = {0};
    char   mode_str[16] = {0};

    /* ---- --verify mode (read-only) ---- */
    if (opts->agent_verify) {
        if (platform_dir_exists(path)) {
            if (opts->agent_mode_octal >= 0) {
                int actual_mode = platform_get_mode(path);
                if (actual_mode >= 0 && actual_mode == opts->agent_mode_octal) {
                    result = AGENT_MKDIR_VERIFIED;
                    platform_strncpy_s(result_msg, sizeof(result_msg),
                                       "Directory exists and mode matches");
                    snprintf(mode_str, sizeof(mode_str), "%04o",
                             opts->agent_mode_octal);
                } else {
                    result = AGENT_MKDIR_ERROR_OTHER;
                    platform_strncpy_s(result_msg, sizeof(result_msg),
                                       "Directory exists but mode does not match");
                    snprintf(mode_str, sizeof(mode_str), "%04o",
                             opts->agent_mode_octal);
                }
            } else {
                result = AGENT_MKDIR_VERIFIED;
                platform_strncpy_s(result_msg, sizeof(result_msg),
                                   "Directory exists");
            }
        } else {
            result = AGENT_MKDIR_ERROR_OTHER;
            platform_strncpy_s(result_msg, sizeof(result_msg),
                               "Directory does not exist");
        }

        if (opts->agent_json) {
            const char *rs = (result == AGENT_MKDIR_VERIFIED) ? "verified" : "error";
            agent_print("{\"v\":1,\"path\":\"");
            agent_json_escape(path);
            agent_printf("\",\"result\":\"%s\",\"message\":\"", rs);
            agent_json_escape(result_msg);
            agent_print("\"");
            if (mode_str[0]) {
                agent_printf(",\"mode\":\"%s\"", mode_str);
            }
            agent_print("}\r\n");
        } else {
            agent_print(result_msg);
            agent_print("\r\n");
        }
        return (result == AGENT_MKDIR_VERIFIED) ? 0 : 1;
    }

    /* ---- determine what would happen / do it ---- */
    if (platform_dir_exists(path)) {
        if (opts->agent_force) {
            if (platform_dir_is_empty(path)) {
                if (opts->agent_dry_run) {
                    result = AGENT_MKDIR_OK;
                    platform_strncpy_s(result_msg, sizeof(result_msg),
                                       "Directory would be recreated");
                } else {
                    if (platform_remove_dir(path) && platform_create_dir(path)) {
                        result = AGENT_MKDIR_OK;
                        platform_strncpy_s(result_msg, sizeof(result_msg),
                                           "Directory recreated");
                        if (opts->agent_mode_octal >= 0)
                            platform_set_mode(path, opts->agent_mode_octal);
                    } else {
                        result = AGENT_MKDIR_ERROR_OTHER;
                        platform_strncpy_s(result_msg, sizeof(result_msg),
                                           "Failed to recreate directory");
                    }
                }
            } else {
                result = AGENT_MKDIR_ERROR_NOT_EMPTY;
                platform_strncpy_s(result_msg, sizeof(result_msg),
                                   "Directory exists and is not empty");
            }
        } else {
            result = AGENT_MKDIR_EXISTS;
            platform_strncpy_s(result_msg, sizeof(result_msg),
                               "Directory already exists");
        }
    } else {
        char parent_path[NCD_MAX_PATH];
        bool parent_exists = path_parent(path, parent_path, sizeof(parent_path))
                          && platform_dir_exists(parent_path);

        if (opts->agent_parents_required && !parent_exists) {
            result = AGENT_MKDIR_ERROR_PARENT;
            platform_strncpy_s(result_msg, sizeof(result_msg),
                               "Parent directory does not exist");
        } else {
            if (opts->agent_dry_run) {
                result = AGENT_MKDIR_OK;
                platform_strncpy_s(result_msg, sizeof(result_msg),
                                   "Directory would be created");
            } else {
                if (!parent_exists) {
                    mkdir_create_parents(parent_path);
                }
                if (platform_create_dir(path)) {
                    result = AGENT_MKDIR_OK;
                    platform_strncpy_s(result_msg, sizeof(result_msg),
                                       "Directory created successfully");
                    if (opts->agent_mode_octal >= 0)
                        platform_set_mode(path, opts->agent_mode_octal);
                } else {
#if NCD_PLATFORM_WINDOWS
                    DWORD err = GetLastError();
                    if (err == ERROR_ACCESS_DENIED) {
                        result = AGENT_MKDIR_ERROR_PERMS;
                        platform_strncpy_s(result_msg, sizeof(result_msg),
                                           "Permission denied");
                    } else {
                        result = AGENT_MKDIR_ERROR_OTHER;
                        platform_strncpy_s(result_msg, sizeof(result_msg),
                                           "Failed to create directory");
                    }
#else
                    if (errno == EACCES || errno == EPERM) {
                        result = AGENT_MKDIR_ERROR_PERMS;
                        platform_strncpy_s(result_msg, sizeof(result_msg),
                                           "Permission denied");
                    } else {
                        result = AGENT_MKDIR_ERROR_OTHER;
                        platform_strncpy_s(result_msg, sizeof(result_msg),
                                           "Failed to create directory");
                    }
#endif
                }
            }
        }
    }

    /* database update */
    if (result == AGENT_MKDIR_OK || result == AGENT_MKDIR_EXISTS) {
        add_path_to_database(path);
    }

    /* output */
    if (opts->agent_json) {
        const char *result_str;
        switch (result) {
            case AGENT_MKDIR_OK:         result_str = "created"; break;
            case AGENT_MKDIR_EXISTS:     result_str = "exists"; break;
            case AGENT_MKDIR_VERIFIED:   result_str = "verified"; break;
            case AGENT_MKDIR_ERROR_PERMS:    result_str = "error_perms"; break;
            case AGENT_MKDIR_ERROR_PATH:     result_str = "error_path"; break;
            case AGENT_MKDIR_ERROR_PARENT:   result_str = "error_parent"; break;
            case AGENT_MKDIR_ERROR_NOT_EMPTY: result_str = "error_not_empty"; break;
            default:                     result_str = "error"; break;
        }
        agent_print("{\"v\":1,\"path\":\"");
        agent_json_escape(path);
        agent_printf("\",\"result\":\"%s\",\"message\":\"", result_str);
        agent_json_escape(result_msg);
        agent_print("\"");
        if (opts->agent_mode_octal >= 0) {
            agent_printf(",\"mode\":\"%04o\"", opts->agent_mode_octal);
        }
        agent_print("}\r\n");
    } else {
        agent_print(result_msg);
        agent_print("\r\n");
    }

    return (result == AGENT_MKDIR_OK || result == AGENT_MKDIR_EXISTS
            || result == AGENT_MKDIR_VERIFIED) ? 0 : 1;
}

/* ==================================================================
 * agent_mode_mkdirs  —  enhanced tree creation
 * ================================================================== */
static int agent_mode_mkdirs(const NcdOptions *opts)
{
    const char *content = NULL;
    char       *file_content = NULL;
    char       *stdin_content = NULL;
    bool        is_json = false;

    /* ---- determine input source ---- */
    if (opts->agent_mkdirs_file[0]) {
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
        const char *p = content;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        is_json = (*p == '[' || *p == '{');
    } else if (opts->has_search) {
        content = opts->search;
        const char *p = content;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        is_json = (*p == '[' || *p == '{');
    } else {
        /* read from stdin */
        stdin_content = read_stdin_all();
        if (!stdin_content || !stdin_content[0]) {
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"error\":\"no input provided\",\"result\":\"error\"}\r\n");
            } else {
                agent_print("ERROR: No input provided. Use --file, provide content as argument, or pipe via stdin.\r\n");
            }
            free(stdin_content);
            return 1;
        }
        content = stdin_content;
        const char *p = content;
        while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        is_json = (*p == '[' || *p == '{');
    }

    /* ---- parse tree ---- */
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

    free(file_content);
    free(stdin_content);

    if (!parse_ok) {
        mkdirs_free_tree(&root);
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"error\":\"failed to parse input\",\"result\":\"error\"}\r\n");
        } else {
            agent_print("ERROR: Failed to parse input\r\n");
        }
        return 1;
    }

    /* ---- prepare buffered JSON output ---- */
    bool buffer_json = opts->agent_json || opts->agent_atomic;
    MkdirsJsonBuilder json;
    mjb_init(&json, buffer_json, opts->agent_atomic);

    MkdirsStats stats = {0};
    AgentTxn    txn;
    agent_txn_init(&txn);

    /* ---- atomic validation pass ---- */
    if (opts->agent_atomic) {
        char fail_path[NCD_MAX_PATH] = {0};
        const char *fail_msg = NULL;
        if (!mkdirs_validate_atomic("", &root, opts,
                                    fail_path, sizeof(fail_path), &fail_msg)) {
            mjb_add_dir(&json, fail_path, "error_not_empty", fail_msg);
            stats.failed++;
            mjb_start_rollback(&json);
            mjb_finish(&json, 0, 0, 1, 0);
            mjb_emit(&json);
            mjb_free(&json);
            mkdirs_free_tree(&root);
            agent_txn_free(&txn);
            return 1;
        }
    }

    /* ---- process tree ---- */
    mkdirs_process("", &root, opts, &json,
                   opts->agent_atomic ? &txn : NULL, &stats);

    /* ---- rollback on atomic failure ---- */
    bool rolled_back = false;
    if (opts->agent_atomic && stats.failed > 0) {
        mjb_start_rollback(&json);
        for (int i = txn.count - 1; i >= 0; i--) {
            if (platform_remove_dir(txn.paths[i])) {
                mjb_add_rollback(&json, txn.paths[i]);
            }
        }
        rolled_back = true;
    }

    /* ---- database update (only on success or non-atomic) ---- */
    if (!opts->agent_dry_run && !opts->agent_verify
        && stats.failed == 0 && txn.count > 0) {
        for (int i = 0; i < txn.count; i++) {
            add_path_to_database(txn.paths[i]);
        }
    }

    /* ---- emit output ---- */
    if (buffer_json) {
        mjb_finish(&json, stats.created, stats.existed, stats.failed,
                   rolled_back ? txn.count : -1);
        mjb_emit(&json);
    } else {
        agent_printf("\r\nCreated %d directories, %d existed, %d failed\r\n",
                     stats.created, stats.existed, stats.failed);
    }

    mjb_free(&json);
    agent_txn_free(&txn);
    mkdirs_free_tree(&root);

    if (opts->agent_verify) {
        return (stats.failed == 0) ? 0 : 1;
    }
    return (stats.failed == 0 || opts->agent_dry_run) ? 0 : 1;
}
