/*
 * patches/agent2_delete.c
 * Agent 2 implementation: --agent:rmdir and --agent:rmdirs
 *
 * Apply these functions to the indicated source files.
 */

/* ================================================================ */
/* FILE: ../shared/platform.c                                       */
/* ================================================================ */

bool platform_dir_is_empty(const char *path)
{
    if (!path) return false;
#if PLATFORM_WINDOWS
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    bool has_entries = false;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        has_entries = true;
        break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return !has_entries;
#else
    DIR *d = opendir(path);
    if (!d) return false;

    struct dirent *ent;
    bool has_entries = false;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        has_entries = true;
        break;
    }
    closedir(d);
    return !has_entries;
#endif
}

bool platform_remove_dir(const char *path)
{
    if (!path) return false;
#if PLATFORM_WINDOWS
    return RemoveDirectoryA(path) != 0;
#else
    return rmdir(path) == 0;
#endif
}

bool platform_remove_tree(const char *path)
{
    if (!path) return false;
#if PLATFORM_WINDOWS
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        return DeleteFileA(path) != 0;
    }

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        return RemoveDirectoryA(path) != 0;
    }

    bool success = true;
    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;

        char child[MAX_PATH];
        path_join(child, sizeof(child), path, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!platform_remove_tree(child)) {
                success = false;
            }
        } else {
            if (!DeleteFileA(child)) {
                success = false;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    if (success) {
        if (!RemoveDirectoryA(path)) {
            success = false;
        }
    }
    return success;
#else
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) {
        return unlink(path) == 0;
    }

    DIR *d = opendir(path);
    if (!d) return rmdir(path) == 0;

    struct dirent *ent;
    bool success = true;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char child[MAX_PATH];
        path_join(child, sizeof(child), path, ent->d_name);

        struct stat child_st;
        if (stat(child, &child_st) == 0) {
            if (S_ISDIR(child_st.st_mode)) {
                if (!platform_remove_tree(child)) {
                    success = false;
                }
            } else {
                if (unlink(child) != 0) {
                    success = false;
                }
            }
        }
    }
    closedir(d);

    if (success) {
        if (rmdir(path) != 0) {
            success = false;
        }
    }
    return success;
#endif
}

/* ================================================================ */
/* FILE: src/database.c                                             */
/* ================================================================ */

bool db_remove_path(NcdDatabase *db, const char *path)
{
    if (!db || !path || !path[0]) return false;

    if (db->is_blob) db_make_mutable(db);

    char norm_path[NCD_MAX_PATH];
    platform_strncpy_s(norm_path, sizeof(norm_path), path);
    path_normalize_separators(norm_path);

    for (int d = 0; d < db->drive_count; d++) {
        DriveData *drv = &db->drives[d];
        for (int i = 0; i < drv->dir_count; i++) {
            char full_path[NCD_MAX_PATH];
            db_full_path(drv, i, full_path, sizeof(full_path));
            path_normalize_separators(full_path);

            if (_stricmp(full_path, norm_path) == 0) {
                int removed_parent = drv->dirs[i].parent;

                /* Update parent references for remaining entries */
                for (int j = 0; j < drv->dir_count; j++) {
                    if (j == i) continue;
                    if (drv->dirs[j].parent == i) {
                        /* Children of removed entry are promoted */
                        int new_parent = removed_parent;
                        if (new_parent > i) new_parent--;
                        drv->dirs[j].parent = new_parent;
                    } else if (drv->dirs[j].parent > i) {
                        drv->dirs[j].parent--;
                    }
                }

                /* Shift remaining entries down to fill the gap */
                if (i < drv->dir_count - 1) {
                    memmove(&drv->dirs[i], &drv->dirs[i + 1],
                            (size_t)(drv->dir_count - i - 1) * sizeof(DirEntry));
                }
                drv->dir_count--;

                /* Invalidate cached name index */
                db->name_index_generation++;

                return true;
            }
        }
    }
    return false;
}

/* ================================================================ */
/* FILE: src/main.c                                                 */
/* ================================================================ */

/*
 * Helper: Remove a path from the database (best-effort).
 * Loads the per-drive database, removes the entry, and marks dirty.
 */
static bool remove_path_from_database(const char *path)
{
    if (!path || !path[0]) return false;

    char target_drive = path_get_drive(path);
    if (target_drive == 0 && path[0] != '\0') {
        target_drive = (char)toupper((unsigned char)path[0]);
    }

    if (target_drive == 0) return false;

    char target_db[NCD_MAX_PATH] = {0};
    NcdDatabase *db = NULL;

    if (db_drive_path(target_drive, target_db, sizeof(target_db))) {
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

    if (!db) return false;

    char norm_path[NCD_MAX_PATH];
    platform_strncpy_s(norm_path, sizeof(norm_path), path);
    path_normalize_separators(norm_path);

    bool found = db_remove_path(db, norm_path);
    if (found) {
        db_mark_dirty_standalone(target_drive, target_db, db);
    } else {
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

    return found;
}

/* Agent rmdir result codes */
typedef enum {
    AGENT_RMDIR_OK,
    AGENT_RMDIR_NOT_FOUND,
    AGENT_RMDIR_NOT_EMPTY,
    AGENT_RMDIR_PERMS,
    AGENT_RMDIR_ERROR
} AgentRmdirResult;

/* Directory list for rmdirs tree collection */
typedef struct {
    char **paths;
    int count;
    int capacity;
} RmdirsList;

static void rmdirs_list_init(RmdirsList *list)
{
    list->paths = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void rmdirs_list_free(RmdirsList *list)
{
    if (!list) return;
    for (int i = 0; i < list->count; i++) {
        free(list->paths[i]);
    }
    free(list->paths);
    list->paths = NULL;
    list->count = 0;
    list->capacity = 0;
}

static bool rmdirs_list_add(RmdirsList *list, const char *path)
{
    if (list->count >= list->capacity) {
        int new_cap = list->capacity ? list->capacity * 2 : 16;
        char **new_paths = ncd_realloc(list->paths, sizeof(char*) * (size_t)new_cap);
        list->paths = new_paths;
        list->capacity = new_cap;
    }
    list->paths[list->count] = ncd_strdup(path);
    list->count++;
    return true;
}

/*
 * Collect all directory paths under `path` into `list`.
 * The root is added first, then children (pre-order).
 */
static bool rmdirs_collect_dirs(const char *path, RmdirsList *list)
{
    if (!rmdirs_list_add(list, path)) return false;

#if NCD_PLATFORM_WINDOWS
    char pattern[NCD_MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return true;

    do {
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0)
            continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            char child[NCD_MAX_PATH];
            path_join(child, sizeof(child), path, fd.cFileName);
            if (!rmdirs_collect_dirs(child, list)) {
                FindClose(h);
                return false;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(path);
    if (!d) return true;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        char child[NCD_MAX_PATH];
        path_join(child, sizeof(child), path, ent->d_name);

        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (!rmdirs_collect_dirs(child, list)) {
                closedir(d);
                return false;
            }
        }
    }
    closedir(d);
#endif
    return true;
}

static int agent_mode_rmdir(const NcdOptions *opts)
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
    AgentRmdirResult result = AGENT_RMDIR_ERROR;
    char result_msg[256] = {0};

    if (!platform_dir_exists(path)) {
        result = AGENT_RMDIR_NOT_FOUND;
        platform_strncpy_s(result_msg, sizeof(result_msg), "Directory not found");
    } else {
        bool is_empty = platform_dir_is_empty(path);
        if (!is_empty && !opts->agent_force) {
            result = AGENT_RMDIR_NOT_EMPTY;
            platform_strncpy_s(result_msg, sizeof(result_msg), "Directory is not empty");
        } else {
            bool removed;
            if (opts->agent_force) {
                removed = platform_remove_tree(path);
            } else {
                removed = platform_remove_dir(path);
            }

            if (removed) {
                result = AGENT_RMDIR_OK;
                platform_strncpy_s(result_msg, sizeof(result_msg), "Directory removed");
                remove_path_from_database(path);
            } else {
#if NCD_PLATFORM_WINDOWS
                DWORD err = GetLastError();
                if (err == ERROR_ACCESS_DENIED) {
                    result = AGENT_RMDIR_PERMS;
                    platform_strncpy_s(result_msg, sizeof(result_msg), "Permission denied");
                } else {
                    result = AGENT_RMDIR_ERROR;
                    platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to remove directory");
                }
#else
                if (errno == EACCES || errno == EPERM) {
                    result = AGENT_RMDIR_PERMS;
                    platform_strncpy_s(result_msg, sizeof(result_msg), "Permission denied");
                } else {
                    result = AGENT_RMDIR_ERROR;
                    platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to remove directory");
                }
#endif
            }
        }
    }

    if (opts->agent_json) {
        const char *result_str;
        switch (result) {
            case AGENT_RMDIR_OK:         result_str = "removed"; break;
            case AGENT_RMDIR_NOT_FOUND:  result_str = "error_not_found"; break;
            case AGENT_RMDIR_NOT_EMPTY:  result_str = "error_not_empty"; break;
            case AGENT_RMDIR_PERMS:      result_str = "error_perms"; break;
            default:                     result_str = "error"; break;
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

    return (result == AGENT_RMDIR_OK) ? 0 : 1;
}

static int agent_mode_rmdirs(const NcdOptions *opts)
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

    /* Preserve root check (default true per spec) */
    {
        bool is_root = false;
#if NCD_PLATFORM_WINDOWS
        size_t len = strlen(path);
        if (len >= 2 && path[1] == ':') {
            if (len == 2 || (len == 3 && (path[2] == '\\' || path[2] == '/')))
                is_root = true;
        }
#else
        if (strcmp(path, "/") == 0)
            is_root = true;
#endif
        if (is_root) {
            if (opts->agent_json) {
                agent_print("{\"v\":1,\"path\":\"");
                agent_json_escape(path);
                agent_print("\",\"result\":\"error\",\"message\":\"Cannot remove root directory\"}\r\n");
            } else {
                agent_print("ERROR: Cannot remove root directory\r\n");
            }
            return 1;
        }
    }

    /* Safety guard: --force is required unless --dry-run */
    if (!opts->agent_force && !opts->agent_dry_run) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"path\":\"");
            agent_json_escape(path);
            agent_print("\",\"result\":\"error\",\"message\":\"--force is required to remove a directory tree. Use --dry-run to preview.\"}\r\n");
        } else {
            agent_print("ERROR: --force is required to remove a directory tree. Use --dry-run to preview.\r\n");
        }
        return 1;
    }

    if (!platform_dir_exists(path)) {
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"path\":\"");
            agent_json_escape(path);
            agent_print("\",\"result\":\"error_not_found\",\"message\":\"Directory not found\"}\r\n");
        } else {
            agent_print("Directory not found\r\n");
        }
        return 1;
    }

    /* Collect all directories in the tree */
    RmdirsList list;
    rmdirs_list_init(&list);
    if (!rmdirs_collect_dirs(path, &list)) {
        rmdirs_list_free(&list);
        if (opts->agent_json) {
            agent_print("{\"v\":1,\"result\":\"error\",\"message\":\"Failed to collect directories\"}\r\n");
        } else {
            agent_print("ERROR: Failed to collect directories\r\n");
        }
        return 1;
    }

    /* Perform the filesystem deletion (unless dry-run) */
    bool delete_success = true;
    if (!opts->agent_dry_run) {
        delete_success = platform_remove_tree(path);
    }

    int removed_count = 0;
    int failed_count = 0;

    if (opts->agent_json) {
        agent_print("{\"v\":1,\"dirs\":[");
    }

    bool first_output = true;

    /* Process from leaf to root (reverse of pre-order collection) */
    for (int i = list.count - 1; i >= 0; i--) {
        const char *dir_path = list.paths[i];
        const char *result_str;
        char result_msg[256] = {0};

        if (opts->agent_dry_run) {
            result_str = "would_remove";
            platform_strncpy_s(result_msg, sizeof(result_msg), "Would remove");
        } else if (delete_success) {
            result_str = "removed";
            platform_strncpy_s(result_msg, sizeof(result_msg), "Directory removed");
            remove_path_from_database(dir_path);
            removed_count++;
        } else {
            result_str = "error";
            platform_strncpy_s(result_msg, sizeof(result_msg), "Failed to remove directory");
            failed_count++;
        }

        if (opts->agent_json) {
            if (!first_output) agent_print(",");
            first_output = false;
            agent_print("{\"path\":\"");
            agent_json_escape(dir_path);
            agent_printf("\",\"result\":\"%s\",\"message\":\"", result_str);
            agent_json_escape(result_msg);
            agent_print("\"}");
        } else {
            agent_print(dir_path);
            agent_print(": ");
            agent_print(result_msg);
            agent_print("\r\n");
        }
    }

    if (opts->agent_json) {
        agent_printf("],\"summary\":{\"removed\":%d,\"failed\":%d}}\r\n", removed_count, failed_count);
    }

    rmdirs_list_free(&list);
    return (failed_count > 0) ? 1 : 0;
}
