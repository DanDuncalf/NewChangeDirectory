# NCD Service vs Standalone Mode: Comprehensive Analysis

## Executive Summary

NCD provides two execution paths: **standalone mode** (client-only, reads/writes local disk) and **service mode** (connects to background `NCDService` process via IPC). The state backend abstraction (`state_backend.h`) unifies access, but the underlying behavior differs significantly in 6 major areas.

---

## 1. State Initialization

### Function: `state_backend_open_best_effort()` → `state_backend_open_local()` vs `state_backend_open_service()`

| Aspect | Standalone Mode | Service Mode |
|--------|-----------------|--------------|
| **Entry point** | `state_backend_open_local()` (line 134 in `state_backend_local.c`) | `state_backend_open_service()` (line 355 in `state_backend_service.c`) |
| **Source** | `info.from_service = false` | `info.from_service = true` |
| **Metadata** | `db_metadata_load()` → heap-allocated `NcdMetadata` | IPC connect → map shared memory → deserialize into `metadata_view` |
| **Database** | `load_all_drive_databases()` merges all per-drive files | IPC connect → map `meta_shm` + `db_shm` → zero-copy pointers |
| **Failure handling** | Returns error if can't create empty metadata/DB | Falls back to local if service unavailable/busy |

**Key code paths:**

```c
// state_backend_local.c:134-182
int state_backend_open_local(NcdStateView **out, NcdStateSourceInfo *info) {
    view->info.from_service = false;
    LOCAL(view).metadata = db_metadata_load();           // Direct disk read
    LOCAL(view).database = load_all_drive_databases();   // Merge all drives
}

// state_backend_service.c:355-636
int state_backend_open_service(NcdStateView **out, NcdStateSourceInfo *info) {
    view->info.from_service = true;
    connect_with_retry(...);                              // IPC connection with retry
    shm_open_existing(meta_name, ...);                    // Map metadata SHM
    shm_open_existing(db_name, ...);                     // Map database SHM
    load_metadata_from_snapshot(view);                     // Deserialize from SHM
    load_database_from_snapshot(view);                     // Zero-copy DB access
}
```

---

## 2. Metadata Access

### Function: `state_view_metadata()`

| Aspect | Standalone Mode | Service Mode |
|--------|-----------------|--------------|
| **Return** | Direct pointer to `LOCAL(view).metadata` | Parsed copy from `metadata_view` |
| **Ownership** | Owned by view, freed on `state_backend_close()` | Copy via `deserialize_metadata_from_shm()` |
| **Mutability** | Modifiable in-memory | Read-only (modifications must go through IPC) |
| **Memory** | Heap-allocated `NcdMetadata` structure | Deserialized copy with own allocations |

**Key code:**

```c
// state_backend_local.c:216-227
const NcdMetadata *state_view_metadata(const NcdStateView *view) {
    if (!view || !LOCAL(view).metadata_loaded) return NULL;
    return LOCAL(view).metadata;  // Direct access
}

// state_backend_service.c:726-736
const NcdMetadata *state_view_metadata_service(const NcdStateView *view) {
    return SERVICE(view).metadata_view;  // Parsed copy
}
```

---

## 3. Database Access

### Function: `state_view_database()`

| Aspect | Standalone Mode | Service Mode |
|--------|-----------------|--------------|
| **Return** | Merged `NcdDatabase*` from all per-drive files | Lightweight `database_view` pointing into SHM |
| **Dir entries** | Heap-allocated `DirEntry[]` arrays | `DirEntry*` pointing directly into SHM offsets |
| **Name pool** | Heap-allocated `char[]` | `char*` pointing into SHM |
| **Blob flag** | `is_blob = false` (normal heap) | `is_blob = true` (SHM pointers, no free) |
| **Path map** | Built lazily via `db_ensure_path_map()` | Built from SHM pointers |

**Key code:**

```c
// state_backend_service.c:295-333 - Shows zero-copy setup
for (uint32_t i = 0; i < hdr->mount_count; i++) {
    drv->dirs = (DirEntry *)SHM_MOUNT_DIRS(SERVICE(view).db_addr, mount);
    drv->name_pool = (char *)SHM_MOUNT_POOL(SERVICE(view).db_addr, mount);
}
db->is_blob = true;  // Prevents double-free
```

---

## 4. Mutations (Heuristics, Groups, Exclusions, Config, Dir History)

### Functions: `state_backend_submit_heuristic_update()`, `state_backend_submit_metadata_update()`

| Mutation Type | Standalone Mode | Service Mode |
|---------------|-----------------|--------------|
| **Heuristics** | Direct `db_heur_note_choice()` + auto-save | IPC `ipc_client_submit_heuristic()` with retry |
| **Groups** | `db_group_set()` + `db_metadata_save()` | IPC `ipc_client_submit_metadata()` with `NCD_META_UPDATE_GROUP_*` |
| **Exclusions** | `db_exclusion_add()` + save | IPC with `NCD_META_UPDATE_EXCLUSION_*` |
| **Config** | Direct config write | IPC with `NCD_META_UPDATE_CONFIG` |
| **Dir History** | `db_dir_history_add()` + save | IPC with `NCD_META_UPDATE_DIR_HISTORY_ADD` |
| **Persistence** | Immediate save to disk | Service handles async write |

**Standalone mutation code (`state_backend_local.c:244-270`):**

```c
int state_backend_submit_heuristic_update(NcdStateView *view, ...) {
    db_heur_note_choice(LOCAL(view).metadata, search, target);
    LOCAL(view).metadata_dirty = true;
    save_metadata_if_dirty(view);  // Immediate disk write
    return 0;
}
```

**Service mutation code (`state_backend_service.c:795-818`):**

```c
int state_backend_submit_heuristic_update_service(...) {
    do {
        result = ipc_client_submit_heuristic(client, search, target);
        result = retry_on_busy(result, ...);  // Retry on BUSY states
    } while (result == NCD_IPC_ERROR_BUSY);
    return (result == NCD_IPC_OK) ? 0 : -1;
}
```

### Service Flush Behavior

The service accumulates changes in memory and flushes to disk periodically:

| Trigger | Condition | Interval |
|---------|-----------|----------|
| **Immediate** | Requested via IPC (`REQUEST_FLUSH`) | Immediate |
| **Deferred** | Database mutated (rescan completed) | 120 seconds (2 min) |
| **Periodic** | Only metadata dirty (heuristics, groups, etc.) | 120 seconds (2 min) |
| **Final** | On service shutdown | Immediate |

**Note**: Both deferred and periodic intervals are set to 2 minutes (120 seconds) for consistency. Database mutations (rescans) are flushed after 2 minutes to avoid excessive disk I/O. Metadata-only changes are also flushed after 2 minutes of inactivity.

---

## 5. IPC Communication

### Functions: `state_backend_request_rescan()`, `state_backend_request_flush()`

| Function | Standalone Mode | Service Mode |
|----------|-----------------|--------------|
| **`rescan`** | Returns 0 (caller handles locally in `main.c`) | IPC `REQUEST_RESCAN` → service handles |
| **`flush`** | Saves metadata if dirty | IPC `REQUEST_FLUSH` → service writes SHM to disk |
| **Busy handling** | N/A | Auto-retry with exponential backoff |

**Key differences in `main.c` (line 5676-5701):**

```c
if (ensure_state_initialized() && g_state_info.from_service) {
    // Service mode: delegate to service
    int rc = state_backend_request_rescan(g_state_view, drive_mask, false);
    if (rc == 0) { /* request sent */ return 0; }
    // Fall through on failure
}
// Standalone mode: perform local scan
NcdDatabase *db = db_create();
run_requested_rescan(db, &opts, &meta->exclusions);
```

---

## 6. Other Behavioral Differences

### 6.1 Heuristic Updates (`main.c:511-528`)

```c
void heur_note_choice(const char *search_raw, const char *target_path) {
    if (g_state_view) {
        state_backend_submit_heuristic_update(g_state_view, key, target);  // Service mode
    } else {
        NcdMetadata *meta = get_metadata();
        db_heur_note_choice(meta, key, target);
        save_metadata_if_dirty();  // Standalone: immediate save
    }
}
```

### 6.2 Directory History (`main.c:277-311`)

```c
static void add_current_dir_to_history(void) {
    if (is_service_backend()) {
        // Service: IPC with formatted data
        size_t data_len = path_len + 1;
        state_backend_submit_metadata_update(g_state_view, 
            NCD_META_UPDATE_DIR_HISTORY_ADD, data, data_len);
    } else {
        // Standalone: load, modify, save
        NcdMetadata *meta = metadata_load_or_create();
        db_dir_history_add(meta, cwd, drive);
        db_metadata_save(meta);
    }
}
```

### 6.3 History Deletion Callback (`main.c:328-338`)

```c
static bool history_delete_service_cb(int index, ...) {
    if (g_state_view && g_state_info.from_service) {
        // Service: IPC callback
        state_backend_submit_metadata_update(g_state_view,
            NCD_META_UPDATE_DIR_HISTORY_REMOVE, &index, sizeof(index));
    }
    return false;
}

// Usage in history browse:
ui_history_delete_cb delete_cb = is_service_backend() ? history_delete_service_cb : NULL;
```

### 6.4 Database Override Flag (`main.c:6214-6218`)

```c
// Standalone: supports -d flag for custom database
if (opts.db_override[0] && !is_service_backend()) {
    primary_db = db_load_auto(target_db);
}
// Service: ignores -d (service provides database)
```

### 6.5 Exclusion Filtering (`main.c:6234-6239`)

```c
// Standalone: filter loaded database
if (primary_db && search_meta && search_meta->exclusions.count > 0 && !is_service_backend()) {
    int removed = db_filter_excluded(primary_db, search_meta);
}
// Service: already filtered by service during scan
```

### 6.6 Cross-Drive Fallback Search (`main.c:6261-6351`)

```c
// Standalone: search all other drives
if ((!matches || match_count == 0) && !is_service_backend()) {
    // Load all drive databases and search
    for (int i = 0; i < drive_count; i++) {
        NcdDatabase *d = db_load_auto(drv_path);
        // ... search and merge
    }
}
// Service: service provides merged DB, no fallback needed
```

### 6.7 Path Map Cache (`main.c:1951-1972`)

```c
// Standalone: build path map for O(1) lookup
PathMap *path_map = db_ensure_path_map(db);
int found_dir_idx = path_map_get(path_map, search_path);
// Service: uses same function, data from SHM
```

### 6.8 Background Rescan Spawning (`main.c:659-676`)

```c
static void spawn_background_rescan(const char *db_path) {
    if (ncd_test_mode_active()) return;  // Skip in test mode
    // Spawn detached ncd /r process
    char cmd[MAX_PATH + 8];
    snprintf(cmd, sizeof(cmd), "\"%s\" /r", exe_path);
    platform_spawn_detached(cmd);
}
// Service mode: no spawning (service handles rescan)
```

### 6.9 Version Checking (`main.c:688-746`)

```c
static bool check_service_version(...) {
    // Only runs in service mode
    NcdIpcClient *client = ipc_client_connect();
    ipc_client_check_version(client, NCD_BUILD_VER, NCD_BUILD_STAMP, &result);
    // Standalone: skip version check
}
```

### 6.10 Encoding Resolution (`main.c:761-819`)

```c
static uint8_t resolve_text_encoding(const NcdOptions *opts) {
    // 1. CLI override
    // 2. Service-advertised encoding (service mode only)
    if (ipc_service_exists()) {
        NcdIpcClient *client = ipc_client_connect();
        ipc_client_get_state_info(client, &info);
        return info.text_encoding;  // Service-advertised
    }
    // 3-5. Fallback to metadata/config/default
}
```

---

## Summary Table

| Feature | Standalone | Service |
|---------|-----------|---------|
| State source | Local disk files | Shared memory (SHM) |
| Database loading | Per-drive files merged | Mapped from SHM |
| Metadata access | Direct pointer | Deserialized copy |
| Mutations | Direct write + save | IPC to service |
| Persistence | Immediate disk write | Service handles async (15s interval) |
| Rescan | Local scan | IPC to service |
| Flush | Save metadata | IPC flush request |
| Busy handling | N/A | Auto-retry |
| Custom DB (-d) | Supported | Ignored |
| Exclusion filtering | Local | Pre-filtered by service |
| Cross-drive fallback | Searches all drives | Service DB is pre-merged |
| Version check | Skipped | IPC check |

---

## Key Architectural Patterns

1. **Service-First Fallback**: `state_backend_open_best_effort()` always tries service first, falls back to local.

2. **Zero-Copy Service Access**: Service mode uses offset-based pointers into shared memory rather than copying data.

3. **Unified Mutation Interface**: `state_backend_submit_metadata_update()` abstracts the IPC vs direct-write difference.

4. **Lazy Service Loading**: Service loads databases asynchronously after initial metadata load.

5. **Atomic Shutdown**: Service flushes all dirty state on exit before cleanup.

---

## Data Flow Diagrams

### Standalone Mode
```
Client Process
    │
    ├── Load: db_metadata_load() → heap metadata
    │         db_load_auto() → merged per-drive databases
    │
    └── Mutate: db_heur_note_choice() → in-memory
                save_metadata_if_dirty() → disk write (immediate)
```

### Service Mode
```
Client Process                      Service Process
    │                                    │
    ├── Connect: IPC handshake            │
    │         Map SHM regions            │
    │                                    │
    ├── Access: Deserialize from SHM     │
    │         (read-only view)           │
    │                                    │
    └── Mutate: IPC msg ────────────────→ db_heur_note_choice() → in-memory
                                                     │
                                                     └── Flush: Every 15s or on shutdown
```
