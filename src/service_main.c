/*
 * service_main.c  --  NCD State Service entry point
 *
 * Need _GNU_SOURCE for pthread functions on Linux */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

/*
 * service_main.c  --  NCD State Service entry point
 *
 * This is the background service executable that:
 * - Loads metadata and database from disk
 * - Publishes snapshots to shared memory
 * - Handles IPC requests from clients
 * - Performs lazy persistence
 */

#include "ncd.h"
#include "database.h"
#include "scanner.h"
#include "platform.h"
#include "service_state.h"
#include "service_publish.h"
#include "control_ipc.h"
#include "shm_platform.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#include <io.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <ctype.h>
#include <dirent.h>
#endif

/* --------------------------------------------------------- globals            */

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
static LONG g_running = 1;
static LONG g_flush_requested = 0;
static LONG g_rescan_requested = 0;
static LONG g_shutdown_requested = 0;
#define ATOMIC_LOAD(v)  InterlockedCompareExchange(&(v), 0, 0)
#define ATOMIC_STORE(v, val) InterlockedExchange(&(v), (val))
#define ATOMIC_CLEAR(v) InterlockedExchange(&(v), 0)
#else
#include <stdatomic.h>
static atomic_int g_running = 1;
static atomic_int g_flush_requested = 0;
static atomic_int g_rescan_requested = 0;
static atomic_int g_shutdown_requested = 0;
#define ATOMIC_LOAD(v)  atomic_load(&(v))
#define ATOMIC_STORE(v, val) atomic_store(&(v), (val))
#define ATOMIC_CLEAR(v) atomic_exchange(&(v), 0)
#endif
static int g_debug_mode = 0;  /* Set to 1 for verbose IPC logging */

/* Init-database option: block startup and scan drives synchronously */
static bool g_init_db = false;
static char g_init_drives[26];
static int g_init_drive_count = 0;

#if NCD_PLATFORM_WINDOWS
/* Named objects for service instance detection and control */
#define SERVICE_MUTEX_NAME      "NCDService_Instance_7D3F9A2E"
static HANDLE g_service_mutex = NULL;
static char g_exe_path[MAX_PATH] = {0};
#define CLOSE_SERVICE_MUTEX() do { if (g_service_mutex) { CloseHandle(g_service_mutex); g_service_mutex = NULL; } } while(0)
#else
#define CLOSE_SERVICE_MUTEX() ((void)0)
#endif

/* Version info - must match NCD_APP_VERSION in control_ipc.h */
#define SERVICE_VERSION     "1.3"
#define SERVICE_BUILD_STAMP __DATE__ " " __TIME__

/* Debug logging macro */
#define DBG_LOG(...) do { if (g_debug_mode) printf(__VA_ARGS__); } while(0)

/* --------------------------------------------------------- IPC payload validators */

static bool validate_heuristic_payload(const NcdSubmitHeuristicPayload *payload, size_t payload_len)
{
    if (payload_len < sizeof(NcdSubmitHeuristicPayload)) return false;
    size_t max_var_len = payload_len - sizeof(NcdSubmitHeuristicPayload);
    if (payload->search_len > max_var_len) return false;
    if (payload->target_len > max_var_len - payload->search_len) return false;
    if (payload->search_len > NCD_IPC_MAX_MSG_SIZE) return false;
    if (payload->target_len > NCD_IPC_MAX_MSG_SIZE) return false;
    return true;
}

static bool validate_metadata_payload(const NcdSubmitMetadataPayload *payload, size_t payload_len)
{
    if (payload_len < sizeof(NcdSubmitMetadataPayload)) return false;
    if (payload->data_len > payload_len - sizeof(NcdSubmitMetadataPayload)) return false;
    if (payload->data_len > NCD_IPC_MAX_MSG_SIZE) return false;
    return true;
}

/* --------------------------------------------------------- logging system     */

/*
 * Service Logging Levels:
 * -1 = disabled (default)
 *  0 = service start, rescan requests, client requests (high-level events)
 *  1 = level 0 + responses sent to clients
 *  2 = level 1 + detailed startup/shutdown steps, internal operations (for diagnosing crashes)
 *  3-5 = reserved for future debugging use
 */
typedef enum {
    NCD_LOG_DISABLED = -1,
    NCD_LOG_LEVEL_0  = 0,   /* High-level events */
    NCD_LOG_LEVEL_1  = 1,   /* + client responses */
    NCD_LOG_LEVEL_2  = 2,   /* + detailed operations */
    NCD_LOG_LEVEL_3  = 3,   /* Reserved */
    NCD_LOG_LEVEL_4  = 4,   /* Reserved */
    NCD_LOG_LEVEL_5  = 5    /* Reserved */
} NcdLogLevel;

static int g_log_level = NCD_LOG_DISABLED;  /* Current log level */
static FILE *g_log_file = NULL;

#if NCD_PLATFORM_WINDOWS
static CRITICAL_SECTION g_log_lock;
static int g_log_lock_initialized = 0;
#else
static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

/* Get string representation of IPC message type */
static const char *msg_type_to_string(int msg_type) {
    switch (msg_type) {
        case NCD_MSG_PING: return "PING";
        case NCD_MSG_GET_STATE_INFO: return "GET_STATE_INFO";
        case NCD_MSG_GET_VERSION: return "GET_VERSION";
        case NCD_MSG_SUBMIT_HEURISTIC: return "SUBMIT_HEURISTIC";
        case NCD_MSG_SUBMIT_METADATA: return "SUBMIT_METADATA";
        case NCD_MSG_REQUEST_RESCAN: return "REQUEST_RESCAN";
        case NCD_MSG_REQUEST_FLUSH: return "REQUEST_FLUSH";
        case NCD_MSG_REQUEST_SHUTDOWN: return "REQUEST_SHUTDOWN";
        case NCD_MSG_RESPONSE: return "RESPONSE";
        case NCD_MSG_ERROR: return "ERROR";
        case NCD_MSG_VERSION_MISMATCH: return "VERSION_MISMATCH";
        default: return "UNKNOWN";
    }
}

/* Get string representation of runtime state */
static const char *runtime_state_to_string(ServiceRuntimeState state) {
    switch (state) {
        case SERVICE_STATE_STOPPED: return "STOPPED";
        case SERVICE_STATE_STARTING: return "STARTING";
        case SERVICE_STATE_LOADING: return "LOADING";
        case SERVICE_STATE_READY: return "READY";
        case SERVICE_STATE_SCANNING: return "SCANNING";
        default: return "UNKNOWN";
    }
}

/* Initialize logging lock */
static void log_lock_init(void) {
#if NCD_PLATFORM_WINDOWS
    if (!g_log_lock_initialized) {
        InitializeCriticalSection(&g_log_lock);
        g_log_lock_initialized = 1;
    }
#endif
}

/* Acquire logging lock */
static void log_lock_acquire(void) {
    log_lock_init();
#if NCD_PLATFORM_WINDOWS
    EnterCriticalSection(&g_log_lock);
#else
    pthread_mutex_lock(&g_log_lock);
#endif
}

/* Release logging lock */
static void log_lock_release(void) {
#if NCD_PLATFORM_WINDOWS
    LeaveCriticalSection(&g_log_lock);
#else
    pthread_mutex_unlock(&g_log_lock);
#endif
}

/* Initialize logging system */
static void log_init(void) {
    if (g_log_level == NCD_LOG_DISABLED) {
        return;
    }

    char log_path[MAX_PATH];
    char logs_dir[MAX_PATH];

    /* Get the logs directory (creates it if needed) */
    if (!db_logs_path(logs_dir, sizeof(logs_dir))) {
        /* Fallback to platform temp directory */
        if (!platform_get_temp_path(logs_dir, sizeof(logs_dir))) {
#if NCD_PLATFORM_WINDOWS
            snprintf(logs_dir, sizeof(logs_dir), "C:\\Windows\\Temp");
#else
            snprintf(logs_dir, sizeof(logs_dir), "/tmp");
#endif
        }
    }

    snprintf(log_path, sizeof(log_path), "%s%sncd_service.log", logs_dir, NCD_PATH_SEP);

    log_lock_acquire();
    g_log_file = fopen(log_path, "a");
    if (g_log_file) {
        time_t now = time(NULL);
        char timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(g_log_file, "\n");
        fprintf(g_log_file, "============================================================\n");
        fprintf(g_log_file, "NCD Service Starting at %s\n", timebuf);
        fprintf(g_log_file, "Version: %s (Build: %s)\n", SERVICE_VERSION, SERVICE_BUILD_STAMP);
        fprintf(g_log_file, "Log File: %s\n", log_path);
        fprintf(g_log_file, "Log Level: %d (%s)\n", g_log_level,
                g_log_level == 0 ? "high-level events only" :
                g_log_level == 1 ? "+ client responses" :
                g_log_level >= 2 ? "+ detailed operations" : "unknown");
        fprintf(g_log_file, "============================================================\n");
        fflush(g_log_file);
    }
    log_lock_release();
}

/* Write a log message (thread-safe) */
static void log_msg(int level, const char *fmt, ...) {
    if (g_log_level < level || !g_log_file) {
        return;
    }

    va_list args;
    va_start(args, fmt);

    log_lock_acquire();

    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%H:%M:%S", tm_info);

    /* Get thread ID */
#if NCD_PLATFORM_WINDOWS
    DWORD tid = GetCurrentThreadId();
#else
    pthread_t tid = pthread_self();
#endif
    fprintf(g_log_file, "[%s] [%lu] [L%d] ", timebuf, (unsigned long)tid, level);

    vfprintf(g_log_file, fmt, args);
    fprintf(g_log_file, "\n");
    fflush(g_log_file);

    log_lock_release();

    va_end(args);
}

/* Close logging */
static void log_close(void) {
    if (g_log_file) {
        log_msg(0, "=== Service shutting down ===");
        log_lock_acquire();
        fclose(g_log_file);
        g_log_file = NULL;
        log_lock_release();
    }
}

/* Convenience macros for different log levels */
#define LOG_EVENT(...)   log_msg(0, __VA_ARGS__)   /* Level 0: Service start, requests */
#define LOG_RESPONSE(...) log_msg(1, __VA_ARGS__)  /* Level 1: Responses to clients */
#define LOG_DETAIL(...)  log_msg(2, __VA_ARGS__)   /* Level 2: Detailed operations */
#define LOG_DEBUG(...)   log_msg(3, __VA_ARGS__)   /* Level 3+: Debug info */

/* Forward declaration for background loader */
static void start_background_loader(ServiceState *state, SnapshotPublisher *pub);
static void wait_for_loader(void);
static void signal_loader_stop(void);

/* Parse a comma/space separated drive list into individual letters */
static int parse_init_drive_list(const char *str, char *out_drives, int max_drives) {
    int count = 0;
    const char *p = str;
    while (*p && count < max_drives) {
        while (*p && (*p == ' ' || *p == ',' || *p == ';')) p++;
        if (isalpha((unsigned char)*p)) {
            out_drives[count++] = (char)toupper((unsigned char)*p);
            p++;
        } else if (*p) {
            p++;
        }
    }
    return count;
}

/*
 * perform_init_scan  --  Synchronous blocking database initialization scan
 *
 * When -init is specified, the service blocks in STARTING/SCANNING state
 * and scans the requested drives (or all drives) before becoming READY.
 * This prevents clients from using NCD before the database is built.
 */
static void perform_init_scan(ServiceState *state, SnapshotPublisher *pub) {
    LOG_EVENT("Init scan starting...");
    LOG_DETAIL("Entering perform_init_scan (drives=%d)", g_init_drive_count);

    service_state_set_runtime_state(state, SERVICE_STATE_SCANNING);
    service_state_set_status_message(state, "Initializing database scan...");

    /* Skip init scan in test mode to prevent scanning user drives */
    const char *test_mode = getenv("NCD_TEST_MODE");
    if (test_mode && test_mode[0]) {
        LOG_EVENT("Init scan skipped: NCD_TEST_MODE is set");
        LOG_DETAIL("Test mode detected, skipping init scan");
        service_state_set_runtime_state(state, SERVICE_STATE_READY);
        service_state_set_status_message(state, "Ready (test mode)");
        LOG_EVENT("Service state changed to READY (test mode)");
        return;
    }

    NcdDatabase *new_db = db_create();
    if (!new_db) {
        LOG_DETAIL("ERROR - Failed to create database for init scan");
        LOG_EVENT("Init scan failed: database creation error");
        service_state_set_status_message(state, "Init scan failed");
        service_state_set_runtime_state(state, SERVICE_STATE_READY);
        return;
    }

    /* Use metadata defaults for scan options */
    const NcdMetadata *meta = service_state_get_metadata(state);
    bool show_hidden = meta ? meta->cfg.default_show_hidden : false;
    bool show_system = meta ? meta->cfg.default_show_system : false;
    int timeout = 300; /* default timeout */

    /* Default exclusions from metadata */
    const NcdExclusionList *exclusions = meta ? &meta->exclusions : NULL;

    int scanned = 0;

    if (g_init_drive_count > 0) {
        /* Build mount paths from specified drives */
        const char *mounts[26];
        char mount_bufs[26][MAX_PATH];
        int mcount = 0;

        char status[256];
        snprintf(status, sizeof(status), "Scanning %d drive(s)...", g_init_drive_count);
        service_state_set_status_message(state, status);

        for (int i = 0; i < g_init_drive_count; i++) {
            if (platform_build_mount_path(g_init_drives[i], mount_bufs[mcount], MAX_PATH)) {
                mounts[mcount] = mount_bufs[mcount];
                mcount++;
            } else {
                LOG_DETAIL("Failed to build mount path for drive %c:", g_init_drives[i]);
            }
        }

        if (mcount > 0) {
            LOG_DETAIL("Init scan: scanning %d specified drives", mcount);
            scanned = scan_mounts(new_db, mounts, mcount, show_hidden, show_system, timeout, exclusions);
        }
    } else {
        /* No drives specified - scan all (same as ncd -r) */
        service_state_set_status_message(state, "Scanning all drives...");
        LOG_DETAIL("Init scan: scanning all drives (no drive list specified)");
        scanned = scan_mounts(new_db, NULL, 0, show_hidden, show_system, timeout, exclusions);
    }

    if (!ATOMIC_LOAD(g_running)) {
        LOG_EVENT("Service stopping, discarding init scan results");
        db_free(new_db);
        return;
    }

    LOG_EVENT("Init scan complete: %d directories found", scanned);
    LOG_DETAIL("Updating service database with init scan results...");

    /* Update service state with new database */
    service_state_update_database(state, new_db, false);
    service_state_bump_db_generation(state);

    /* Publish snapshot so clients see it immediately */
    LOG_DETAIL("Publishing database snapshot after init scan...");
    if (!snapshot_publisher_publish_db(pub, state)) {
        LOG_DETAIL("ERROR - Failed to publish database snapshot after init scan");
    } else {
        LOG_DETAIL("Database snapshot published after init scan");
    }

    /* Save metadata only if it existed before service start.
     * On first run we let the user configure via interactive prompt
     * before creating the metadata file. */
    if (service_state_metadata_existed(state)) {
        LOG_DETAIL("Saving metadata after init scan...");
        if (!service_state_save_metadata(state)) {
            LOG_DETAIL("WARNING - Failed to save metadata after init scan");
        }
    } else {
        LOG_DETAIL("Skipping metadata save: first run, not previously loaded from disk");
    }

    /* Flush database to disk immediately */
    LOG_DETAIL("Flushing database to disk after init scan...");
    if (service_state_flush(state)) {
        LOG_DETAIL("Database flushed successfully after init scan");
    } else {
        LOG_DETAIL("WARNING - Failed to flush database after init scan");
    }

    char status[256];
    snprintf(status, sizeof(status), "Ready (%d directories)", scanned);
    service_state_set_status_message(state, status);
    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    LOG_EVENT("Service state changed to READY after init scan");
    LOG_DETAIL("Exiting perform_init_scan");
}

/* Thread handle for background loader */
static PlatformHandle g_loader_thread = NULL;

#if NCD_PLATFORM_WINDOWS
#define CLOSE_THREAD_HANDLE(t) do { if (t) { CloseHandle((HANDLE)(t)); } (t) = NULL; } while(0)
#else
#define CLOSE_THREAD_HANDLE(t) do { (t) = NULL; } while(0)
#endif

/* --------------------------------------------------------- signal handling    */

#if NCD_PLATFORM_WINDOWS
static BOOL WINAPI signal_handler(DWORD sig) {
    if (sig == CTRL_C_EVENT || sig == CTRL_BREAK_EVENT) {
        ATOMIC_STORE(g_running, 0);
        return TRUE;
    }
    return FALSE;
}
#else
static void signal_handler(int sig) {
    (void)sig;
    ATOMIC_STORE(g_running, 0);
}
#endif

static void setup_signal_handlers(void) {
#if NCD_PLATFORM_WINDOWS
    SetConsoleCtrlHandler(signal_handler, TRUE);
#else
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    /* Ignore SIGHUP so daemon doesn't die when parent exits */
    signal(SIGHUP, SIG_IGN);
    /* Ignore SIGPIPE so service doesn't die when client disconnects
     * during send() on Linux Unix domain sockets */
    signal(SIGPIPE, SIG_IGN);
#endif
}

/* --------------------------------------------------------- IPC request handlers */

static void handle_get_version(NcdIpcConnection *conn, uint32_t sequence) {
    LOG_EVENT("GET_VERSION request received (seq=%u)", sequence);

    NcdVersionInfoPayload payload;
    memset(&payload, 0, sizeof(payload));

    strncpy(payload.app_version, SERVICE_VERSION, sizeof(payload.app_version) - 1);
    strncpy(payload.build_stamp, SERVICE_BUILD_STAMP, sizeof(payload.build_stamp) - 1);
    payload.protocol_version = NCD_IPC_VERSION;

    ipc_server_send_response(conn, sequence, &payload, sizeof(payload));
    LOG_RESPONSE("GET_VERSION response sent (seq=%u)", sequence);
}

static void handle_request_shutdown(NcdIpcConnection *conn, uint32_t sequence, ServiceState *state) {
    LOG_EVENT("REQUEST_SHUTDOWN received (seq=%u) - initiating graceful shutdown", sequence);
    ATOMIC_STORE(g_shutdown_requested, 1);

    /* Mark service state as shutting down to reject new operations */
    if (state) {
        service_state_request_shutdown(state);
        service_state_set_runtime_state(state, SERVICE_STATE_STOPPED);
        service_state_set_status_message(state, "Shutting down...");
    }

    ipc_server_send_response(conn, sequence, NULL, 0);
    LOG_RESPONSE("REQUEST_SHUTDOWN response sent (seq=%u)", sequence);
}

static void handle_get_state_info(NcdIpcConnection *conn,
                                   uint32_t sequence,
                                   const ServiceState *state,
                                   const SnapshotPublisher *pub) {
    LOG_EVENT("GET_STATE_INFO request received (seq=%u)", sequence);

    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);

    /* Get metadata to read text encoding */
    const NcdMetadata *meta = service_state_get_metadata(state);
    uint8_t text_encoding = NCD_TEXT_UTF8;
    if (meta && meta->cfg.text_encoding) {
        text_encoding = meta->cfg.text_encoding;
    }

    /* Build response payload with service state */
    NcdStateInfoPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.protocol_version = NCD_IPC_VERSION;
    payload.text_encoding = text_encoding;
    payload.meta_generation = info.meta_generation;
    payload.db_generation = info.db_generation;
    payload.meta_size = (uint32_t)info.meta_size;
    payload.db_size = (uint32_t)info.db_size;
    payload.meta_name_len = (uint32_t)strlen(info.meta_shm_name) + 1;
    payload.db_name_len = (uint32_t)strlen(info.db_shm_name) + 1;

    size_t total_size = sizeof(payload) + payload.meta_name_len + payload.db_name_len;
    uint8_t *response = (uint8_t *)malloc(total_size);
    if (!response) {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC, "Out of memory");
        return;
    }

    memcpy(response, &payload, sizeof(payload));
    memcpy(response + sizeof(payload), info.meta_shm_name, payload.meta_name_len);
    memcpy(response + sizeof(payload) + payload.meta_name_len,
           info.db_shm_name, payload.db_name_len);

    ipc_server_send_response(conn, sequence, response, total_size);
    LOG_RESPONSE("GET_STATE_INFO response sent (seq=%u, meta_gen=%llu, db_gen=%llu, state=%s)",
                 sequence, (unsigned long long)info.meta_generation,
                 (unsigned long long)info.db_generation,
                 runtime_state_to_string(service_state_get_runtime_state(state)));
    free(response);
}

static void handle_get_detailed_status(NcdIpcConnection *conn,
                                        uint32_t sequence,
                                        const ServiceState *state,
                                        const SnapshotPublisher *pub) {
    (void)pub;
    LOG_EVENT("GET_DETAILED_STATUS request received (seq=%u)", sequence);

    ServiceStats stats;
    service_state_get_stats(state, &stats);

    const char *status_msg = service_state_get_status_message(state);

    char meta_path[MAX_PATH] = {0};
    db_metadata_path(meta_path, sizeof(meta_path));

    char log_dir[MAX_PATH] = {0};
    db_logs_path(log_dir, sizeof(log_dir));
    char log_path[MAX_PATH] = {0};
    if (log_dir[0] != '\0') {
        snprintf(log_path, sizeof(log_path), "%s%sncd_service.log", log_dir, NCD_PATH_SEP);
    } else {
        char temp[MAX_PATH];
        if (platform_get_temp_path(temp, sizeof(temp))) {
            snprintf(log_path, sizeof(log_path), "%s%sncd_service.log", temp, NCD_PATH_SEP);
        }
    }

    const NcdDatabase *db = service_state_get_database(state);

    /* Calculate sizes */
    uint32_t status_msg_len = (uint32_t)strlen(status_msg) + 1;
    uint32_t meta_path_len = (uint32_t)strlen(meta_path) + 1;
    uint32_t log_path_len = (uint32_t)strlen(log_path) + 1;

    /* Cap drive count to fit in IPC message */
    int drive_count = 0;
    if (db) {
        drive_count = db->drive_count;
        if (drive_count > (int)NCD_IPC_MAX_DETAILED_DRIVES) {
            drive_count = (int)NCD_IPC_MAX_DETAILED_DRIVES;
        }
    }

    size_t total_size = sizeof(NcdDetailedStatusPayload);
    total_size += status_msg_len + meta_path_len + log_path_len;
    total_size += drive_count * sizeof(NcdDetailedStatusDriveHeader);

    for (int i = 0; i < drive_count; i++) {
        char db_path[MAX_PATH];
        if (ncd_platform_db_drive_path(db->drives[i].letter, db_path, sizeof(db_path))) {
            total_size += (uint32_t)strlen(db_path) + 1;
        } else {
            total_size += 1; /* empty string null terminator */
        }
    }

    if (total_size > NCD_IPC_MAX_MSG_SIZE - sizeof(NcdIpcHeader)) {
        /* Too large - reduce drive count until it fits */
        while (drive_count > 0) {
            total_size = sizeof(NcdDetailedStatusPayload);
            total_size += status_msg_len + meta_path_len + log_path_len;
            total_size += drive_count * sizeof(NcdDetailedStatusDriveHeader);
            for (int i = 0; i < drive_count; i++) {
                char db_path[MAX_PATH];
                if (ncd_platform_db_drive_path(db->drives[i].letter, db_path, sizeof(db_path))) {
                    total_size += (uint32_t)strlen(db_path) + 1;
                } else {
                    total_size += 1;
                }
            }
            if (total_size <= NCD_IPC_MAX_MSG_SIZE - sizeof(NcdIpcHeader)) {
                break;
            }
            drive_count--;
        }
    }

    uint8_t *response = (uint8_t *)calloc(1, total_size);
    if (!response) {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC, "Out of memory");
        return;
    }

    NcdDetailedStatusPayload *payload = (NcdDetailedStatusPayload *)response;
    payload->protocol_version = NCD_IPC_VERSION;
    payload->runtime_state = (uint16_t)service_state_get_runtime_state(state);
    payload->log_level = (int16_t)g_log_level;
    payload->pending_count = (uint32_t)service_state_get_pending_count(state);
    payload->dirty_flags = stats.dirty_flags;
    payload->meta_generation = stats.meta_generation;
    payload->db_generation = stats.db_generation;
    payload->drive_count = (uint32_t)drive_count;
    payload->status_msg_len = status_msg_len;
    payload->meta_path_len = meta_path_len;
    payload->log_path_len = log_path_len;
    strncpy(payload->app_version, SERVICE_VERSION, sizeof(payload->app_version));
    strncpy(payload->build_stamp, SERVICE_BUILD_STAMP, sizeof(payload->build_stamp));
#if NCD_PLATFORM_WINDOWS
    strncpy(payload->platform, "Windows", sizeof(payload->platform));
#else
    strncpy(payload->platform, "Linux", sizeof(payload->platform));
#endif
    strncpy(payload->arch, platform_get_arch_name(), sizeof(payload->arch));

    uint8_t *data = response + sizeof(NcdDetailedStatusPayload);

    memcpy(data, status_msg, status_msg_len);
    data += status_msg_len;
    memcpy(data, meta_path, meta_path_len);
    data += meta_path_len;
    memcpy(data, log_path, log_path_len);
    data += log_path_len;

    for (int i = 0; i < drive_count; i++) {
        NcdDetailedStatusDriveHeader *drv = (NcdDetailedStatusDriveHeader *)data;
        drv->letter = db->drives[i].letter;
        drv->dir_count = (uint32_t)db->drives[i].dir_count;

        char db_path[MAX_PATH] = {0};
        if (ncd_platform_db_drive_path(db->drives[i].letter, db_path, sizeof(db_path))) {
            drv->db_path_len = (uint32_t)strlen(db_path) + 1;
        } else {
            drv->db_path_len = 1;
        }
        data += sizeof(NcdDetailedStatusDriveHeader);
        if (drv->db_path_len > 1) {
            memcpy(data, db_path, drv->db_path_len);
        } else {
            data[0] = '\0';
        }
        data += drv->db_path_len;
    }

    ipc_server_send_response(conn, sequence, response, total_size);
    LOG_RESPONSE("GET_DETAILED_STATUS response sent (seq=%u, state=%s, drives=%d)",
                 sequence,
                 runtime_state_to_string(service_state_get_runtime_state(state)),
                 drive_count);
    free(response);
}

static void handle_submit_heuristic(NcdIpcConnection *conn,
                                      uint32_t sequence,
                                      ServiceState *state,
                                      SnapshotPublisher *pub,
                                      const NcdSubmitHeuristicPayload *payload,
                                      const char *data) {
    const char *search = data;
    const char *target = data + payload->search_len;

    LOG_EVENT("SUBMIT_HEURISTIC request received (seq=%u, search='%s', target='%s')",
              sequence, search, target);

    if (service_state_note_heuristic(state, search, target)) {
        /* Republish metadata snapshot */
        service_state_bump_meta_generation(state);
        snapshot_publisher_publish_meta(pub, state);
        ipc_server_send_response(conn, sequence, NULL, 0);
        LOG_RESPONSE("SUBMIT_HEURISTIC response sent (seq=%u, success)", sequence);
    } else {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC,
                              "Failed to record heuristic");
        LOG_RESPONSE("SUBMIT_HEURISTIC error sent (seq=%u, failed to record)", sequence);
    }
}

static const char *metadata_update_type_to_string(int type) {
    switch (type) {
        case NCD_META_UPDATE_GROUP_ADD: return "GROUP_ADD";
        case NCD_META_UPDATE_GROUP_REMOVE: return "GROUP_REMOVE";
        case NCD_META_UPDATE_EXCLUSION_ADD: return "EXCLUSION_ADD";
        case NCD_META_UPDATE_EXCLUSION_REMOVE: return "EXCLUSION_REMOVE";
        case NCD_META_UPDATE_CONFIG: return "CONFIG";
        case NCD_META_UPDATE_CLEAR_HISTORY: return "CLEAR_HISTORY";
        case NCD_META_UPDATE_DIR_HISTORY_ADD: return "DIR_HISTORY_ADD";
        case NCD_META_UPDATE_DIR_HISTORY_REMOVE: return "DIR_HISTORY_REMOVE";
        case NCD_META_UPDATE_DIR_HISTORY_SWAP: return "DIR_HISTORY_SWAP";
        case NCD_META_UPDATE_ENCODING_SWITCH: return "ENCODING_SWITCH";
        default: return "UNKNOWN";
    }
}

static bool apply_metadata_update(ServiceState *state, int update_type,
                                   const void *data, uint32_t data_len,
                                   bool *out_publish_db) {
    bool success = false;
    bool publish_db = false;

    switch (update_type) {
        case NCD_META_UPDATE_GROUP_ADD: {
            /* Try newline-delimited format first: "name\npath" */
            if (data && data_len > 0) {
                const char *p = (const char *)data;
                const char *nl = NULL;
                for (uint32_t i = 0; i < data_len; i++) {
                    if (p[i] == '\n') {
                        nl = p + i;
                        break;
                    }
                }
                if (nl) {
                    size_t name_len = nl - p;
                    size_t path_len = data_len - name_len - 1;
                    if (name_len > 0 && path_len > 0) {
                        char *name = (char *)malloc(name_len + 1);
                        char *path = (char *)malloc(path_len + 1);
                        if (name && path) {
                            memcpy(name, p, name_len);
                            name[name_len] = '\0';
                            memcpy(path, nl + 1, path_len);
                            path[path_len] = '\0';
                            success = service_state_add_group(state, name, path);
                        }
                        free(name);
                        free(path);
                        break;
                    }
                }
            }
            /* Try null-terminated concatenated format: "name\0path\0" */
            if (!success && data && data_len >= 2) {
                const char *p = (const char *)data;
                /* Find first null terminator within data */
                uint32_t name_len = 0;
                while (name_len < data_len && p[name_len] != '\0') {
                    name_len++;
                }
                if (name_len > 0 && name_len + 1 < data_len) {
                    uint32_t path_len = 0;
                    const char *path_start = p + name_len + 1;
                    uint32_t path_max = data_len - name_len - 1;
                    while (path_len < path_max && path_start[path_len] != '\0') {
                        path_len++;
                    }
                    if (path_len > 0) {
                        success = service_state_add_group(state, p, path_start);
                    }
                }
            }
            /* Fall back to binary length-prefixed format */
            if (!success && data && data_len >= 8) {
                uint32_t name_len = *(uint32_t *)data;
                uint32_t path_len = *(uint32_t *)((char *)data + 4);
                if (name_len > 0 && path_len > 0 && data_len >= 8 + name_len + path_len) {
                    const char *name = (const char *)data + 8;
                    const char *path = (const char *)data + 8 + name_len;
                    success = service_state_add_group(state, name, path);
                }
            }
            break;
        }
        case NCD_META_UPDATE_GROUP_REMOVE:
            if (!data || data_len < 1) {
                success = false;
            } else {
                success = service_state_remove_group(state, (const char *)data);
            }
            break;
        case NCD_META_UPDATE_EXCLUSION_ADD:
            if (!data || data_len < 1) {
                success = false;
            } else {
                success = service_state_add_exclusion(state, (const char *)data);
                publish_db = success;
            }
            break;
        case NCD_META_UPDATE_EXCLUSION_REMOVE:
            if (!data || data_len < 1) {
                success = false;
            } else {
                success = service_state_remove_exclusion(state, (const char *)data);
            }
            break;
        case NCD_META_UPDATE_CONFIG:
            if (!data || data_len < sizeof(NcdConfig)) {
                success = false;
            } else {
                success = service_state_update_config(state, (const NcdConfig *)data);
            }
            break;
        case NCD_META_UPDATE_CLEAR_HISTORY:
            success = service_state_clear_history(state);
            break;
        case NCD_META_UPDATE_DIR_HISTORY_ADD: {
            /* Data format: [path string (null-terminated)][drive byte] */
            if (data && data_len >= 2) {
                const char *p = (const char *)data;
                /* Find null terminator within bounds, or use full data_len */
                uint32_t path_len = 0;
                while (path_len < data_len && p[path_len] != '\0') {
                    path_len++;
                }
                if (path_len > 0) {
                    char path_buf[512];
                    if (path_len >= sizeof(path_buf)) path_len = sizeof(path_buf) - 1;
                    memcpy(path_buf, p, path_len);
                    path_buf[path_len] = '\0';
                    char drive = '\0';
                    if (data_len >= path_len + 2) {
                        drive = p[path_len + 1];
                    }
                    success = service_state_add_dir_history(state, path_buf, drive);
                }
            }
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_REMOVE: {
            if (!data || data_len < sizeof(int)) {
                success = false;
            } else {
                int idx = *(const int *)data;
                success = service_state_remove_dir_history(state, idx);
            }
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_SWAP: {
            success = service_state_swap_dir_history(state);
            break;
        }
        default:
            if (out_publish_db) *out_publish_db = false;
            return false;
    }

    if (out_publish_db) *out_publish_db = publish_db;
    return success;
}

static void handle_submit_metadata(NcdIpcConnection *conn,
                                    uint32_t sequence,
                                    ServiceState *state,
                                    SnapshotPublisher *pub,
                                    const NcdSubmitMetadataPayload *payload,
                                    const void *data) {
    bool publish_db = false;

    LOG_EVENT("SUBMIT_METADATA request received (seq=%u, type=%s)",
              sequence, metadata_update_type_to_string(payload->update_type));

    bool success = apply_metadata_update(state, payload->update_type, data,
                                          payload->data_len, &publish_db);

    if (!success && strcmp(metadata_update_type_to_string(payload->update_type), "UNKNOWN") == 0) {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID,
                              "Unknown update type");
        LOG_RESPONSE("SUBMIT_METADATA error sent (seq=%u, unknown type=%d)",
                     sequence, payload->update_type);
        return;
    }

    if (success) {
        service_state_bump_meta_generation(state);
        snapshot_publisher_publish_meta(pub, state);

        if (publish_db) {
            service_state_bump_db_generation(state);
            snapshot_publisher_publish_db(pub, state);
        }

        ipc_server_send_response(conn, sequence, NULL, 0);
        LOG_RESPONSE("SUBMIT_METADATA response sent (seq=%u, success)", sequence);
    } else {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC,
                              "Failed to update metadata");
        LOG_RESPONSE("SUBMIT_METADATA error sent (seq=%u, failed)", sequence);
    }
}

static void handle_request_rescan(NcdIpcConnection *conn,
                                   uint32_t sequence,
                                   ServiceState *state,
                                   SnapshotPublisher *pub,
                                   const NcdRequestRescanPayload *payload) {
    (void)conn; (void)sequence; (void)state; (void)pub; (void)payload;
    (void)state;
    (void)pub;
    LOG_EVENT("REQUEST_RESCAN received (seq=%u, drive_mask=0x%X, partial=%d)",
              sequence, payload->drive_mask, payload->is_partial);

    /* Signal main loop to perform rescan */
    ATOMIC_STORE(g_rescan_requested, 1);

    ipc_server_send_response(conn, sequence, NULL, 0);
    LOG_RESPONSE("REQUEST_RESCAN response sent (seq=%u)", sequence);
}

static void handle_request_flush(NcdIpcConnection *conn,
                                  uint32_t sequence,
                                  ServiceState *state) {
    (void)conn; (void)sequence; (void)state;
    (void)state;
    LOG_EVENT("REQUEST_FLUSH received (seq=%u)", sequence);

    /* Signal main loop to flush */
    ATOMIC_STORE(g_flush_requested, 1);

    ipc_server_send_response(conn, sequence, NULL, 0);
    LOG_RESPONSE("REQUEST_FLUSH response sent (seq=%u)", sequence);
}

/* --------------------------------------------------------- pending request handlers
 * These are called by service_state_process_pending() after service becomes READY
 */

void handle_pending_heuristic(ServiceState *state, void *pub,
                               const char *search, const char *target) {
    if (service_state_note_heuristic(state, search, target)) {
        service_state_bump_meta_generation(state);
        snapshot_publisher_publish_meta((SnapshotPublisher *)pub, state);
    }
}

void handle_pending_metadata(ServiceState *state, void *pub,
                              int update_type, const void *data, uint32_t data_len) {
    bool publish_db = false;

    bool success = apply_metadata_update(state, update_type, data, data_len, &publish_db);

    if (success) {
        service_state_bump_meta_generation(state);
        snapshot_publisher_publish_meta((SnapshotPublisher *)pub, state);
        if (publish_db) {
            service_state_bump_db_generation(state);
            snapshot_publisher_publish_db((SnapshotPublisher *)pub, state);
        }
    }
}

void handle_pending_rescan(ServiceState *state, void *pub) {
    (void)state;
    (void)pub;
    /* Just set the flag - main loop will handle it */
    ATOMIC_STORE(g_rescan_requested, 1);
}

void handle_pending_flush(ServiceState *state) {
    (void)state;
    ATOMIC_STORE(g_flush_requested, 1);
}

/*
 * process_all_pending  --  Process all pending requests in the queue
 *
 * This is called by the loader thread after transitioning to READY.
 */
static void process_all_pending(ServiceState *state, SnapshotPublisher *pub) {
    PendingRequestType type;
    void *data = NULL;
    size_t data_len = 0;

    int pending_count = service_state_get_pending_count(state);
    if (pending_count == 0) {
        return;
    }

    printf("NCD Service: Processing %d pending requests...\n", pending_count);

    int processed = 0;
    while (service_state_dequeue_pending(state, &type, &data, &data_len)) {
        processed++;

        switch (type) {
            case PENDING_HEURISTIC: {
                /* Data format: search_len (4 bytes) + target_len (4 bytes) + search + target */
                if (data && data_len >= 8) {
                    uint32_t search_len = *(uint32_t *)data;
                    uint32_t target_len = *(uint32_t *)((char *)data + 4);
                    if (search_len > 0 && target_len > 0 &&
                        data_len >= 8 + search_len + target_len) {
                        const char *search = (const char *)data + 8;
                        const char *target = search + search_len;
                        handle_pending_heuristic(state, pub, search, target);
                    }
                }
                break;
            }

            case PENDING_METADATA_UPDATE: {
                /* Data format: update_type (4 bytes) + data_len (4 bytes) + data */
                if (data && data_len >= 8) {
                    int update_type = *(int *)data;
                    uint32_t md_len = *(uint32_t *)((char *)data + 4);
                    if (md_len == 0 || data_len >= 8 + md_len) {
                        const void *update_data = (const char *)data + 8;
                        handle_pending_metadata(state, pub, update_type, update_data, md_len);
                    }
                }
                break;
            }

            case PENDING_RESCAN:
                handle_pending_rescan(state, pub);
                break;

            case PENDING_FLUSH:
                handle_pending_flush(state);
                break;
        }

        if (data) {
            free(data);
            data = NULL;
        }
    }

}

/*
 * try_queue_mutation  --  Try to queue a mutation request for later processing
 *
 * Returns true if queued, false if queue is full.
 */
static bool try_queue_mutation(ServiceState *state,
                                NcdMessageType msg_type,
                                const void *payload, size_t payload_len) {
    /* Only queue during LOADING or SCANNING states */
    ServiceRuntimeState runtime = service_state_get_runtime_state(state);
    if (runtime != SERVICE_STATE_LOADING && runtime != SERVICE_STATE_SCANNING) {
        return false;
    }

    PendingRequestType pending_type;
    void *data = NULL;
    size_t data_len = 0;

    switch (msg_type) {
        case NCD_MSG_SUBMIT_HEURISTIC: {
            if (!payload || !validate_heuristic_payload((const NcdSubmitHeuristicPayload *)payload, payload_len)) {
                return false;
            }
            const NcdSubmitHeuristicPayload *hp = (const NcdSubmitHeuristicPayload *)payload;
            pending_type = PENDING_HEURISTIC;
            data_len = 8 + hp->search_len + hp->target_len;
            data = malloc(data_len);
            if (!data) return false;
            *(uint32_t *)data = hp->search_len;
            *(uint32_t *)((char *)data + 4) = hp->target_len;
            memcpy((char *)data + 8,
                   (const char *)payload + sizeof(NcdSubmitHeuristicPayload),
                   hp->search_len + hp->target_len);
            break;
        }

        case NCD_MSG_SUBMIT_METADATA: {
            if (!payload || !validate_metadata_payload((const NcdSubmitMetadataPayload *)payload, payload_len)) {
                return false;
            }
            const NcdSubmitMetadataPayload *mp = (const NcdSubmitMetadataPayload *)payload;
            pending_type = PENDING_METADATA_UPDATE;
            data_len = 8 + mp->data_len;
            data = malloc(data_len);
            if (!data) return false;
            *(int *)data = mp->update_type;
            *(uint32_t *)((char *)data + 4) = mp->data_len;
            if (mp->data_len > 0) {
                memcpy((char *)data + 8,
                       (const char *)payload + sizeof(NcdSubmitMetadataPayload),
                       mp->data_len);
            }
            break;
        }

        case NCD_MSG_REQUEST_RESCAN:
            pending_type = PENDING_RESCAN;
            break;

        case NCD_MSG_REQUEST_FLUSH:
            pending_type = PENDING_FLUSH;
            break;

        default:
            return false;
    }

    bool queued = service_state_enqueue_request(state, pending_type, data, data_len);
    if (data) free(data);
    return queued;
}

/*
 * check_service_ready  --  Check if service is ready for data-dependent operations
 *
 * Returns true if request can proceed, false if busy response was sent.
 *
 * Note: During SCANNING state, we only block if there's no existing database
 * (initial scan). If we have database data, we continue serving requests
 * using the existing data while the rescan happens in background.
 */
static bool check_service_ready(NcdIpcConnection *conn,
                                 uint32_t sequence,
                                 ServiceState *state) {
    /* Check for shutdown first - reject all operations during shutdown */
    if (service_state_is_shutdown_requested(state)) {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_SHUTTING_DOWN,
                              "NCD Not Available - service is shutting down");
        return false;
    }

    ServiceRuntimeState runtime_state = service_state_get_runtime_state(state);

    switch (runtime_state) {
        case SERVICE_STATE_READY:
            return true;

        case SERVICE_STATE_LOADING:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_BUSY_LOADING,
                                  service_state_get_status_message(state));
            return false;

        case SERVICE_STATE_SCANNING:
            /* Only block if this is the initial scan (no database yet) */
            if (!service_state_has_database_data(state)) {
                ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_BUSY_SCANNING,
                                      service_state_get_status_message(state));
                return false;
            }
            /* We have existing data - allow requests to proceed during rescan */
            return true;

        case SERVICE_STATE_STARTING:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_NOT_READY,
                                  "Service starting...");
            return false;

        case SERVICE_STATE_STOPPED:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_NOT_READY,
                                  "NCD Not Available - service is stopped");
            return false;

        default:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC,
                                  "Service in unknown state");
            return false;
    }
}

/*
 * handle_client_connection  --  Handle a single message from a client
 *
 * Returns: 0 on success (message handled), -1 on disconnect/error
 */
static int handle_client_connection(NcdIpcConnection *conn,
                                     ServiceState *state,
                                     SnapshotPublisher *pub) {
    void *payload = NULL;
    size_t payload_len = 0;
    uint32_t sequence = 0;

    int msg_type = ipc_server_receive(conn, &payload, &payload_len, &sequence);
    if (msg_type == 0) {
        DBG_LOG("NCD Service: Failed to receive message or connection closed\n");
        LOG_DETAIL("Client connection closed or receive error");
        return -1;  /* Connection closed or error */
    }

    DBG_LOG("NCD Service: Received message type=%d seq=%u payload_len=%zu\n",
            msg_type, sequence, payload_len);

    LOG_DETAIL("Received message: type=%s, seq=%u, payload_len=%zu",
               msg_type_to_string(msg_type), sequence, payload_len);

    /* PING and GET_STATE_INFO work regardless of service state */
    if (msg_type == NCD_MSG_PING) {
        LOG_DETAIL("PING request received (seq=%u)", sequence);
        ipc_server_send_response(conn, sequence, NULL, 0);
        LOG_RESPONSE("PING response sent (seq=%u)", sequence);
        if (payload) ipc_free_message(payload);
        return 0;
    }

    if (msg_type == NCD_MSG_GET_STATE_INFO) {
        handle_get_state_info(conn, sequence, state, pub);
        if (payload) ipc_free_message(payload);
        return 0;
    }

    if (msg_type == NCD_MSG_GET_DETAILED_STATUS) {
        handle_get_detailed_status(conn, sequence, state, pub);
        if (payload) ipc_free_message(payload);
        return 0;
    }

    if (msg_type == NCD_MSG_GET_VERSION) {
        handle_get_version(conn, sequence);
        if (payload) ipc_free_message(payload);
        return 0;
    }

    if (msg_type == NCD_MSG_REQUEST_SHUTDOWN) {
        handle_request_shutdown(conn, sequence, state);
        if (payload) ipc_free_message(payload);
        return 0;
    }

    /* Check service state - for mutations during LOADING/SCANNING, try to queue */
    ServiceRuntimeState runtime = service_state_get_runtime_state(state);
    if (runtime != SERVICE_STATE_READY) {
        /* Try to queue mutation requests */
        if ((msg_type == NCD_MSG_SUBMIT_HEURISTIC ||
             msg_type == NCD_MSG_SUBMIT_METADATA ||
             msg_type == NCD_MSG_REQUEST_RESCAN ||
             msg_type == NCD_MSG_REQUEST_FLUSH) &&
            try_queue_mutation(state, msg_type, payload, payload_len)) {
            /* Request queued for later processing */
            ipc_server_send_response(conn, sequence, NULL, 0);
            if (payload) ipc_free_message(payload);
            return 0;
        }

        /* Not queued - return busy error */
        if (!check_service_ready(conn, sequence, state)) {
            if (payload) ipc_free_message(payload);
            return 0;
        }
    }

    switch (msg_type) {
        case NCD_MSG_SUBMIT_HEURISTIC:
            if (payload && validate_heuristic_payload((const NcdSubmitHeuristicPayload *)payload, payload_len)) {
                handle_submit_heuristic(conn, sequence, state, pub,
                                        (const NcdSubmitHeuristicPayload *)payload,
                                        (const char *)payload + sizeof(NcdSubmitHeuristicPayload));
            } else {
                ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID,
                                      "Invalid payload");
            }
            break;

        case NCD_MSG_SUBMIT_METADATA:
            if (payload && validate_metadata_payload((const NcdSubmitMetadataPayload *)payload, payload_len)) {
                handle_submit_metadata(conn, sequence, state, pub,
                                       (const NcdSubmitMetadataPayload *)payload,
                                       (const char *)payload + sizeof(NcdSubmitMetadataPayload));
            } else {
                ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID,
                                      "Invalid payload");
            }
            break;

        case NCD_MSG_REQUEST_RESCAN:
            if (payload && payload_len >= sizeof(NcdRequestRescanPayload)) {
                handle_request_rescan(conn, sequence, state, pub,
                                      (const NcdRequestRescanPayload *)payload);
            } else {
                ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID,
                                      "Invalid payload");
            }
            break;

        case NCD_MSG_REQUEST_FLUSH:
            handle_request_flush(conn, sequence, state);
            break;

        default:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID,
                                  "Unknown message type");
            break;
    }

    if (payload) {
        ipc_free_message(payload);
    }

    return 0;
}

/* --------------------------------------------------------- background loader  */

typedef struct {
    ServiceState *state;
    SnapshotPublisher *pub;
} LoaderContext;

static unsigned long loader_thread_func(void *param) {
    LoaderContext *ctx = (LoaderContext *)param;
    if (!ctx) {
        LOG_DETAIL("ERROR - Loader thread received NULL context");
        return 0;
    }

    ServiceState *state = ctx->state;
    SnapshotPublisher *pub = ctx->pub;

    if (!state || !pub) {
        LOG_DETAIL("ERROR - Loader thread received NULL state or pub");
        return 0;
    }

    LOG_DETAIL("Background loader thread started");

    /* Load databases */
    LOG_DETAIL("About to load databases...");
    if (!service_state_load_databases(state)) {
        LOG_DETAIL("Warning - database loading had issues");
    } else {
        LOG_DETAIL("Database loading completed successfully");
    }

    /* Publish database snapshot */
    LOG_DETAIL("Publishing database snapshot...");
    if (!snapshot_publisher_publish_db(pub, state)) {
        LOG_DETAIL("ERROR - Failed to publish database snapshot");
    } else {
        LOG_DETAIL("Database snapshot published successfully");
    }

    /* Transition to READY state */
    LOG_DETAIL("Transitioning to READY state");
    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    service_state_set_status_message(state, "Ready");
    LOG_EVENT("Service state changed to READY");

    /* Process any pending requests that were queued during loading */
    LOG_DETAIL("Processing pending requests...");
    process_all_pending(state, pub);

    free(ctx);

    LOG_DETAIL("Background loader thread exiting");
    return 0;
}

static LoaderContext *g_loader_ctx = NULL;

static void start_background_loader(ServiceState *state, SnapshotPublisher *pub) {
    LOG_DETAIL("Starting background loader thread...");
    g_loader_ctx = (LoaderContext *)malloc(sizeof(LoaderContext));
    if (!g_loader_ctx) {
        fprintf(stderr, "NCD Service: Failed to allocate loader context\n");
        LOG_DETAIL("ERROR - Failed to allocate loader context, continuing synchronously");
        /* Continue synchronously */
        LoaderContext ctx = {state, pub};
        loader_thread_func(&ctx);
        return;
    }

    g_loader_ctx->state = state;
    g_loader_ctx->pub = pub;

    LOG_DETAIL("Background loader context initialized");

    g_loader_thread = platform_thread_create(loader_thread_func, g_loader_ctx);
    if (!g_loader_thread) {
        fprintf(stderr, "NCD Service: Failed to start loader thread\n");
        LOG_DETAIL("ERROR - Failed to start loader thread, continuing synchronously");
        free(g_loader_ctx);
        g_loader_ctx = NULL;
        /* Continue synchronously */
        LoaderContext ctx = {state, pub};
        loader_thread_func(&ctx);
    } else {
        LOG_DETAIL("Background loader thread started successfully");
    }
}

static void wait_for_loader(void) {
    if (g_loader_thread) {
        platform_thread_wait(g_loader_thread, 5000);
        CLOSE_THREAD_HANDLE(g_loader_thread);
    }
    /* Context is freed by loader thread */
    g_loader_ctx = NULL;
}

static void signal_loader_stop(void) {
    /* Loader checks g_running flag */
}

/* --------------------------------------------------------- main loop          */

static void perform_rescan(ServiceState *state, SnapshotPublisher *pub) {
    /* Skip rescan in test mode to prevent scanning user drives */
    const char *test_mode = getenv("NCD_TEST_MODE");
    if (test_mode && test_mode[0]) {
        LOG_EVENT("Rescan skipped: NCD_TEST_MODE is set");
        LOG_DETAIL("Test mode detected, skipping filesystem rescan");
        return;
    }

    LOG_EVENT("Starting filesystem rescan...");
    LOG_DETAIL("Entering perform_rescan");

    ServiceRuntimeState prev_state = service_state_get_runtime_state(state);
    service_state_set_runtime_state(state, SERVICE_STATE_SCANNING);
    service_state_set_status_message(state, "Scanning filesystem...");
    LOG_EVENT("Service state changed to SCANNING");

    /* Scan all drives */
    char drives[26];
    int drive_count = platform_get_available_drives(drives, 26);
    LOG_DETAIL("Found %d drives to scan", drive_count);

    NcdDatabase *new_db = db_create();
    if (!new_db) {
        LOG_DETAIL("ERROR - Failed to create new database for rescan");
        LOG_EVENT("Rescan failed: database creation error");
        service_state_set_runtime_state(state, prev_state);
        return;
    }

    /* Use metadata defaults for scan options */
    const NcdMetadata *meta = service_state_get_metadata(state);
    bool show_hidden = meta ? meta->cfg.default_show_hidden : false;
    bool show_system = meta ? meta->cfg.default_show_system : false;
    const NcdExclusionList *exclusions = meta ? &meta->exclusions : NULL;

    /* Scan each drive */
    int scanned_count = 0;
    for (int i = 0; i < drive_count && ATOMIC_LOAD(g_running); i++) {
        char mount_path[MAX_PATH];
        if (!platform_build_mount_path(drives[i], mount_path, sizeof(mount_path))) {
            LOG_DETAIL("Skipping drive %c: - failed to build mount path", drives[i]);
            continue;
        }

        LOG_DETAIL("Scanning drive %c: (path=%s)", drives[i], mount_path);

        DriveData *drv = db_add_drive(new_db, drives[i]);
        if (!drv) {
            LOG_DETAIL("ERROR - Failed to add drive %c: to database", drives[i]);
            continue;
        }

        /* Scan the drive */
        int dirs = scan_mount(new_db, mount_path, show_hidden, show_system,
                             NULL, NULL, exclusions);
        if (dirs >= 0) {
            scanned_count++;
            LOG_DETAIL("Drive %c: scanned (%d dirs)", drives[i], dirs);
        } else {
            LOG_DETAIL("ERROR - Failed to scan drive %c:", drives[i]);
        }
    }

    /* Update database and publish */
    if (ATOMIC_LOAD(g_running)) {
        if (scanned_count == 0) {
            LOG_DETAIL("Rescan completed but no drives scanned, discarding empty database");
            LOG_EVENT("Rescan complete: 0 drives scanned, database unchanged");
            db_free(new_db);
        } else {
            LOG_DETAIL("Rescan completed, updating database (%d drives scanned)", scanned_count);
            /* Full rescan - not partial */
            service_state_update_database(state, new_db, false);
            service_state_bump_db_generation(state);
            LOG_DETAIL("Publishing updated database snapshot...");
            snapshot_publisher_publish_db(pub, state);
            LOG_EVENT("Database snapshot published after rescan");
        }
    } else {
        LOG_DETAIL("Service stopping, discarding rescan results");
        db_free(new_db);
    }

    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    service_state_set_status_message(state, "Ready");
    LOG_EVENT("Service state changed back to READY after rescan");
    LOG_DETAIL("Exiting perform_rescan");
}

static void service_loop(ServiceState *state, SnapshotPublisher *pub, NcdIpcServer *existing_server) {
    LOG_DETAIL("Entering service_loop");

    NcdIpcServer *server = existing_server;
    if (!server) {
        LOG_DETAIL("ERROR - No IPC server provided, exiting service_loop");
        LOG_EVENT("Service fatal error: IPC server not available");
        return;
    }
    LOG_DETAIL("Using existing IPC server");
    LOG_EVENT("Service main loop started");

    time_t last_flush_check = time(NULL);
    const int FLUSH_INTERVAL_SEC = 15;  /* Flush every 15 seconds if dirty */
    const int DEFERRED_FLUSH_SEC = 10;  /* Deferred flush after 10 seconds of no activity */
    int client_connection_count = 0;

    while (ATOMIC_LOAD(g_running)) {
        /* Accept client connections (non-blocking with timeout) */
        NcdIpcConnection *conn = ipc_server_accept(server, 100);  /* 100ms timeout */

        if (conn) {
            client_connection_count++;
            /* Handle multiple messages on this connection */
            DBG_LOG("NCD Service: Client connected, handling messages...\n");
            LOG_DETAIL("Client #%d connected", client_connection_count);
            int msg_count = 0;
            while (ATOMIC_LOAD(g_running)) {
                int result = handle_client_connection(conn, state, pub);
                if (result < 0) {
                    /* Client disconnected or error */
                    break;
                }
                msg_count++;
                /* Brief pause to prevent busy-waiting */
                platform_sleep_ms(1);
            }
            DBG_LOG("NCD Service: Client disconnected after %d messages\n", msg_count);
            LOG_DETAIL("Client #%d disconnected after %d messages", client_connection_count, msg_count);
            ipc_server_close_connection(conn);
        }

        /* Check for rescan request */
        if (ATOMIC_CLEAR(g_rescan_requested)) {
            perform_rescan(state, pub);
        }

        /* Check for shutdown request */
        if (ATOMIC_LOAD(g_shutdown_requested)) {
            LOG_EVENT("Shutdown requested, exiting main loop");
            ATOMIC_STORE(g_running, 0);
            break;
        }

        /* Check for flush:
         * 1. Explicit flush request from client (immediate only)
         * 2. Deferred flush: ALL database changes (DIRTY_DATABASE and DIRTY_DATABASE_PARTIAL)
         *    are flushed 10 seconds after the last mutation (timer resets on new mutations)
         * 3. Periodic flush for metadata-only changes (15s interval)
         */
        time_t now = time(NULL);

        /* Only explicit flush requests are immediate - all database changes use deferred flush */
        bool needs_immediate = ATOMIC_LOAD(g_flush_requested);

        /* Deferred flush: ALL database changes use deferred flush with reset timer.
         * Both full rescans (DIRTY_DATABASE) and partial updates (DIRTY_DATABASE_PARTIAL)
         * wait 10 seconds after the last mutation before flushing. */
        time_t mutation_age = service_state_get_db_mutation_age(state);
        uint32_t dirty_flags = service_state_get_dirty_flags(state);
        bool needs_deferred = service_state_needs_flush(state) &&
                              (dirty_flags & (DIRTY_DATABASE | DIRTY_DATABASE_PARTIAL)) &&
                              mutation_age >= DEFERRED_FLUSH_SEC;

        /* Periodic flush for metadata-only changes (not database changes) */
        bool needs_periodic = service_state_needs_flush(state) &&
                              !(dirty_flags & (DIRTY_DATABASE | DIRTY_DATABASE_PARTIAL)) &&
                              (now - last_flush_check) > FLUSH_INTERVAL_SEC;

        if (needs_immediate || needs_deferred || needs_periodic) {
            LOG_DETAIL("Flushing state to disk (immediate=%d, deferred=%d, periodic=%d)",
                       needs_immediate, needs_deferred, needs_periodic);
            ATOMIC_STORE(g_flush_requested, 0);
            last_flush_check = now;
            if (service_state_flush(state)) {
                LOG_DETAIL("State flushed successfully");
            } else {
                LOG_DETAIL("ERROR - Failed to flush state");
            }
        }

        /* Small sleep to prevent busy-waiting */
        platform_sleep_ms(10);
    }

    LOG_DETAIL("Cleaning up IPC server (total clients handled: %d)", client_connection_count);
    /* Note: ipc_server_cleanup is deferred to run_service so the pipe
     * stays open until after the mutex is closed, preventing races
     * where ipc_service_exists() sees a live mutex but no pipe. */
    LOG_EVENT("Service main loop ended");
    LOG_DETAIL("Exiting service_loop");
}

/* --------------------------------------------------------- service lifecycle  */

#if NCD_PLATFORM_WINDOWS
/* Find another NCDService.exe process in the process list (exclude self) */
static bool find_existing_ncd_service_process(DWORD *out_pid) {
    DWORD my_pid = GetCurrentProcessId();
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe = {sizeof(pe)};
    bool found = false;
    if (Process32First(hSnap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "NCDService.exe") == 0 &&
                pe.th32ProcessID != my_pid) {
                *out_pid = pe.th32ProcessID;
                found = true;
                break;
            }
        } while (Process32Next(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return found;
}

/* Poll until process exits or timeout */
static bool wait_for_process_exit(DWORD pid, int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        HANDLE hProc = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!hProc) return true;  /* Process gone */
        DWORD r = WaitForSingleObject(hProc, 0);
        CloseHandle(hProc);
        if (r != WAIT_TIMEOUT) return true;  /* Exited */
        platform_sleep_ms(100);
    }
    return false;
}

static void print_stale_process_error(DWORD pid) {
    fprintf(stderr, "NCD Service: Another instance (PID: %lu) is stuck during shutdown.\n", pid);
    fprintf(stderr, "Please terminate it manually with:\n");
    fprintf(stderr, "  taskkill /F /IM NCDService.exe /FI \"PID eq %lu\"\n", pid);
}
#else
/* Check PID file or scan /proc for existing NCDService process */
static bool is_live_ncd_service_process(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) != 0) {
        return false;
    }

    char stat_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(stat_path, "r");
    if (!f) {
        return true;
    }

    int parsed_pid = 0;
    char comm[256];
    char state = '\0';
    bool live = true;
    if (fscanf(f, "%d (%255[^)]) %c", &parsed_pid, comm, &state) == 3) {
        if (strcmp(comm, "NCDService") != 0 || state == 'Z') {
            live = false;
        }
    }
    fclose(f);
    return live;
}

static bool find_existing_ncd_service_process(pid_t *out_pid) {
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    char pid_path[256];
    if (xdg_runtime && *xdg_runtime) {
        snprintf(pid_path, sizeof(pid_path), "%s/ncd_service.pid", xdg_runtime);
    } else {
        snprintf(pid_path, sizeof(pid_path), "/tmp/ncd_service.pid");
    }

    /* Check PID file first */
    FILE *f = fopen(pid_path, "r");
    if (f) {
        pid_t pid = 0;
        if (fscanf(f, "%d", &pid) == 1 && pid > 0) {
            if (is_live_ncd_service_process(pid)) {
                *out_pid = pid;
                fclose(f);
                return true;
            }
        }
        fclose(f);
    }

    /* Fallback: scan /proc for NCDService processes */
    DIR *proc = opendir("/proc");
    if (!proc) return false;

    struct dirent *entry;
    pid_t my_pid = getpid();
    bool found = false;

    while ((entry = readdir(proc)) != NULL) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        pid_t pid = atoi(entry->d_name);
        if (pid == my_pid) continue;

        char comm_path[256];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
        FILE *fcomm = fopen(comm_path, "r");
        if (fcomm) {
            char name[256];
            if (fgets(name, sizeof(name), fcomm)) {
                size_t len = strlen(name);
                if (len > 0 && name[len - 1] == '\n') name[len - 1] = '\0';
                if (strcmp(name, "NCDService") == 0 && is_live_ncd_service_process(pid)) {
                    *out_pid = pid;
                    found = true;
                }
            }
            fclose(fcomm);
            if (found) break;
        }
    }
    closedir(proc);
    return found;
}

static bool wait_for_process_exit(pid_t pid, int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        if (!is_live_ncd_service_process(pid)) return true;
        platform_sleep_ms(100);
    }
    return false;
}

static void print_stale_process_error(pid_t pid) {
    fprintf(stderr, "NCD Service: Another instance (PID: %d) is stuck during shutdown.\n", (int)pid);
    fprintf(stderr, "Please terminate it manually with:\n");
    fprintf(stderr, "  kill -9 %d\n", (int)pid);
}
#endif

/* Check if service is already running */
static bool is_service_running(void) {
#if NCD_PLATFORM_WINDOWS
    /* System-wide mode uses Global\ namespace; user-mode uses session-local mutex */
    const char *mutex_names[] = {
        SERVICE_MUTEX_NAME,
        "Global\\NCDService_Instance_7D3F9A2E",
        NULL
    };
    for (int i = 0; mutex_names[i]; i++) {
        HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, mutex_names[i]);
        if (hMutex) {
            DWORD waitResult = WaitForSingleObject(hMutex, 0);
            CloseHandle(hMutex);
            if (waitResult == WAIT_TIMEOUT) {
                /* Mutex is owned by a live service process */
                return true;
            }
            /* WAIT_ABANDONED_0 (service crashed) or WAIT_OBJECT_0 (unowned) */
        }
    }
    return false;
#else
    return ipc_service_exists();
#endif
}

/* Wait for service to stop with specified timeout */
static int wait_for_service_stop_with_timeout(int timeout_seconds) {
    int wait_count = 0;
    int max_wait = timeout_seconds * 10;  /* 100ms per iteration */

    while (is_service_running() && wait_count < max_wait) {
        platform_sleep_ms(100);
        wait_count++;
    }
    return is_service_running() ? -1 : 0;
}

/* Print service usage information */
static void print_usage(void) {
    printf("NCD Service - Resident state service for NewChangeDirectory\n");
    printf("\n");
    printf("Usage:\n");
    printf("  ncd_service start              Start the service (daemon mode)\n");
    printf("  ncd_service stop               Stop the running service (waits up to 5s)\n");
    printf("  ncd_service stop block <N>     Stop and wait N seconds for shutdown\n");
    printf("                                 Returns -1 if service didn't stop in time\n");
    printf("  ncd_service status             Show service status (running/stopped)\n");
    printf("  ncd_service /agdb              Run service in foreground with debug output\n");
    printf("  ncd_service -conf <path>       Use custom config file\n");
    printf("  ncd_service --daemon           Run service in foreground (internal use)\n");
    printf("  ncd_service -log<n>            Enable logging (n=0-5, see below)\n");
    printf("  ncd_service -init [drives]     Initialize database on startup\n");
    printf("                                 Scans specified drives (e.g. C,D,E)\n");
    printf("                                 or all drives if none specified.\n");
    printf("                                 Blocks until scan is complete.\n");
    printf("\n");
    printf("Logging levels (-log<n>):\n");
    printf("  -log0  Log service start, rescan requests, and client requests\n");
    printf("  -log1  Level 0 + log responses sent to clients\n");
    printf("  -log2  Level 1 + detailed startup/shutdown steps (for crash diagnosis)\n");
    printf("  -log3  Reserved for future debugging\n");
    printf("  -log4  Reserved for future debugging\n");
    printf("  -log5  Reserved for future debugging\n");
    printf("\n");
    printf("With no arguments, prints this help message and shows service status.\n");
}

/* Print detailed service status (used by both Windows and Linux status commands) */
static void print_detailed_status(void) {
    if (!ipc_service_exists()) {
        printf("stopped\n");
        return;
    }

    if (ipc_client_init() != 0) {
        printf("running\n");
        return;
    }

    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        printf("running\n");
        ipc_client_cleanup();
        return;
    }

    NcdIpcDetailedStatus info;
    NcdIpcResult result = ipc_client_get_detailed_status(client, &info);
    ipc_client_disconnect(client);
    ipc_client_cleanup();

    if (result != NCD_IPC_OK) {
        printf("running\n");
        return;
    }

    const char *state_str = "UNKNOWN";
    switch (info.runtime_state) {
        case 0: state_str = "STOPPED"; break;
        case 1: state_str = "STARTING"; break;
        case 2: state_str = "LOADING"; break;
        case 3: state_str = "READY"; break;
        case 4: state_str = "SCANNING"; break;
    }

    printf("Status: %s\n", state_str);
    if (info.status_message[0]) {
        printf("Message: %s\n", info.status_message);
    }
    if (info.app_version[0]) {
        printf("Version: %s\n", info.app_version);
    }
    if (info.build_stamp[0]) {
        printf("Build: %s\n", info.build_stamp);
    }
    if (info.platform[0]) {
        printf("Platform: %s\n", info.platform);
    }
    if (info.arch[0]) {
        printf("Architecture: %s\n", info.arch);
    }
    printf("Protocol: %u\n", info.protocol_version);
    printf("Log level: %d\n", info.log_level);
    if (info.pending_count > 0) {
        printf("Pending requests: %u\n", info.pending_count);
    }
    if (info.dirty_flags != 0) {
        printf("Dirty flags: 0x%08X\n", info.dirty_flags);
    }
    printf("Meta generation: %llu\n", (unsigned long long)info.meta_generation);
    printf("DB generation: %llu\n", (unsigned long long)info.db_generation);

    if (info.meta_path[0]) {
        printf("Metadata file: %s\n", info.meta_path);
    }
    if (info.log_path[0]) {
        printf("Log file: %s\n", info.log_path);
    }

    if (info.drive_count > 0) {
        printf("Cached drives (%u):\n", info.drive_count);
        for (uint32_t i = 0; i < info.drive_count; i++) {
            if (info.drives[i].db_path[0]) {
                printf("  %c: (%u dirs) -> %s\n",
                       info.drives[i].letter,
                       info.drives[i].dir_count,
                       info.drives[i].db_path);
            } else {
                printf("  %c: (%u dirs)\n",
                       info.drives[i].letter,
                       info.drives[i].dir_count);
            }
        }
    } else {
        printf("Cached drives: none\n");
    }
}

/* Parse -log<n> option from argument.
 * Returns:
 *   -1         Not a -log option
 *   -2         Bare "-log" or "/log" — next argv token is the level
 *   0..5       Valid log level from attached form (-log3)
 */
static int parse_log_option(const char *arg) {
    if (!arg) return -1;
    /* Space-separated form: exactly "-log" or "/log" */
    if (strcmp(arg, "-log") == 0 || strcmp(arg, "/log") == 0) {
        return -2;  /* Caller must consume next token as level */
    }
    /* Attached form: "-log3" or "/log3" */
    if (strncmp(arg, "-log", 4) == 0 || strncmp(arg, "/log", 4) == 0) {
        const char *level_str = arg + 4;
        if (*level_str >= '0' && *level_str <= '5' && level_str[1] == '\0') {
            return *level_str - '0';
        }
    }
    return -1;  /* Not a valid -log option */
}

/* --------------------------------------------------------- run service        */

#if NCD_PLATFORM_WINDOWS
/* Spawn detached child process to run as daemon */
static int spawn_daemon(const char *exe_path, const char *extra_args) {
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH * 2];

    if (extra_args && extra_args[0]) {
        snprintf(cmd, sizeof(cmd), "\"%s\" --daemon %s", exe_path, extra_args);
    } else {
        snprintf(cmd, sizeof(cmd), "\"%s\" --daemon", exe_path);
    }

    if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                      CREATE_NO_WINDOW | DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                      NULL, NULL, &si, &pi)) {
        printf("NCD Service started (PID: %lu)\n", pi.dwProcessId);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }
    fprintf(stderr, "Failed to start service: %lu\n", GetLastError());
    return 1;
}
#endif

/* Run the actual service daemon (common to both platforms) */
static int run_service(void) {
    LOG_DETAIL("Entering run_service");

#if NCD_PLATFORM_WINDOWS
    /* Create mutex to indicate we're running.
     * System mode: use Global\ namespace for cross-session visibility. */
    {
        char mutex_name[256];
        if (ncd_is_system_mode()) {
            snprintf(mutex_name, sizeof(mutex_name), "Global\\%s", SERVICE_MUTEX_NAME);
        } else {
            snprintf(mutex_name, sizeof(mutex_name), "%s", SERVICE_MUTEX_NAME);
        }
        g_service_mutex = CreateMutexA(NULL, TRUE, mutex_name);
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "NCD Service: Already running\n");
        LOG_EVENT("Service start failed - already running");
        CLOSE_SERVICE_MUTEX();
        return 1;
    }
    LOG_DETAIL("Service mutex created successfully (%s mode)", 
               ncd_is_system_mode() ? "system" : "per-user");
#endif

    int ret = 1;
    ServiceState *state = NULL;
    SnapshotPublisher *pub = NULL;
    NcdIpcServer *server = NULL;

    setup_signal_handlers();
    LOG_DETAIL("Signal handlers set up");

    /* Initialize service state */
    LOG_DETAIL("Initializing service state...");
    state = service_state_init();
    if (!state) {
        fprintf(stderr, "NCD Service: Failed to initialize state\n");
        LOG_EVENT("Service start failed - state initialization error");
        goto cleanup;
    }
    LOG_DETAIL("Service state initialized successfully");

    /* Initialize snapshot publisher */
    LOG_DETAIL("Initializing snapshot publisher...");
    pub = snapshot_publisher_init();
    if (!pub) {
        fprintf(stderr, "NCD Service: Failed to initialize publisher\n");
        LOG_DETAIL("ERROR - Failed to initialize snapshot publisher");
        goto cleanup;
    }
    LOG_DETAIL("Snapshot publisher initialized successfully");

    /* Publish initial metadata snapshot */
    LOG_DETAIL("Publishing initial metadata snapshot...");
    if (!snapshot_publisher_publish_meta(pub, state)) {
        fprintf(stderr, "NCD Service: Failed to publish metadata snapshot\n");
        LOG_DETAIL("ERROR - Failed to publish initial metadata snapshot");
        goto cleanup;
    }
    LOG_EVENT("Metadata snapshot published");

    /* Initialize IPC server BEFORE starting background loader
     * This allows clients to connect immediately and check service status
     * while the background loader is still initializing */
    LOG_DETAIL("Initializing IPC server...");
    server = ipc_server_init();
    if (!server) {
        fprintf(stderr, "NCD Service: Failed to initialize IPC server\n");
        LOG_DETAIL("ERROR - Failed to initialize IPC server");
        goto cleanup;
    }
    LOG_DETAIL("IPC server initialized successfully");
    LOG_EVENT("IPC server ready - accepting client connections");

    /* Start background loader AFTER IPC is ready
     * Clients can now connect and see STARTING/LOADING state */
    if (g_init_db) {
        LOG_DETAIL("Init-db requested, performing synchronous scan...");
        perform_init_scan(state, pub);
    } else {
        LOG_DETAIL("Starting background loader...");
        start_background_loader(state, pub);
    }

    /* Run service loop - IPC server already initialized */
    LOG_DETAIL("Entering service main loop");
    service_loop(state, pub, server);
    /* server cleanup is deferred to after CLOSE_SERVICE_MUTEX */

    /* Cleanup */
    LOG_DETAIL("Beginning service cleanup...");
    signal_loader_stop();
    wait_for_loader();
    LOG_DETAIL("Background loader stopped");

    if (service_state_needs_flush(state)) {
        LOG_DETAIL("Final state flush before exit...");
        service_state_flush(state);
    }

    ret = 0;

cleanup:
    CLOSE_SERVICE_MUTEX();
    if (server) ipc_server_cleanup(server);
    if (pub) snapshot_publisher_cleanup(pub);
    if (state) service_state_cleanup(state);
    if (ret == 0) {
        LOG_EVENT("Service stopped successfully");
    }
    LOG_DETAIL("Exiting run_service");
    return ret;
}

/* --------------------------------------------------------- main               */

int main(int argc, char *argv[]) {
#if NCD_PLATFORM_WINDOWS
    platform_get_module_path(g_exe_path, sizeof(g_exe_path));
#endif

    /* Parse command line arguments */
    /* First pass: look for logging, init, and user-mode options before other processing */
    for (int i = 1; i < argc; i++) {
        int log_level = parse_log_option(argv[i]);
        if (log_level >= 0) {
            g_log_level = log_level;
        } else if (log_level == -2 && i + 1 < argc) {
            /* Space-separated form: -log 3 */
            const char *next = argv[i + 1];
            if (next[0] >= '0' && next[0] <= '5' && next[1] == '\0') {
                g_log_level = next[0] - '0';
                i++;  /* Consume the level token */
            }
        }
        if (strcmp(argv[i], "-init") == 0 || strcmp(argv[i], "--init-db") == 0) {
            g_init_db = true;
            if (i + 1 < argc && argv[i + 1][0] != '-' &&
                strcmp(argv[i + 1], "start") != 0 &&
                strcmp(argv[i + 1], "stop") != 0 &&
                strcmp(argv[i + 1], "status") != 0 &&
                strcmp(argv[i + 1], "--daemon") != 0) {
                g_init_drive_count = parse_init_drive_list(argv[i + 1], g_init_drives, 26);
                i++;
            }
        }
        if (strcmp(argv[i], "--user-mode") == 0) {
            ncd_set_system_mode(false);
        }
    }

    /* Test mode uses user-mode (per-user) paths for isolated test environments */
    {
        const char *tm = getenv("NCD_TEST_MODE");
        if (tm && tm[0] && strcmp(tm, "0") != 0 && ncd_is_system_mode()) {
            ncd_set_system_mode(false);
        }
    }

    int arg_offset = 0;

    /* Check for debug flag */
    if (argc > 1 && (strcmp(argv[1], "/agdb") == 0 || strcmp(argv[1], "-agdb") == 0)) {
        g_debug_mode = 1;
        if (g_log_level == NCD_LOG_DISABLED) {
            g_log_level = NCD_LOG_LEVEL_2;  /* Default to detailed logging in debug mode */
        }
        arg_offset = 1;
    }

    /* Check for log level option at position 1+offset */
    if (argc > 1 + arg_offset) {
        int log_opt = parse_log_option(argv[1 + arg_offset]);
        if (log_opt >= 0) {
            g_log_level = log_opt;
            arg_offset++;  /* Skip the -log<n> argument */
        } else if (log_opt == -2 && argc > 2 + arg_offset) {
            /* Space-separated form: -log 3 */
            const char *next = argv[2 + arg_offset];
            if (next[0] >= '0' && next[0] <= '5' && next[1] == '\0') {
                g_log_level = next[0] - '0';
                arg_offset += 2;  /* Skip both -log and the level */
            }
        }
    }

    /* Check for config override */
    if (argc > 1 + arg_offset && strcmp(argv[1 + arg_offset], "-conf") == 0) {
        if (argc > 2 + arg_offset) {
            db_metadata_set_override(argv[2 + arg_offset]);
            printf("NCD Service: Using config override: %s\n", argv[2 + arg_offset]);
            LOG_DETAIL("Config override: %s", argv[2 + arg_offset]);
            arg_offset += 2;
        } else {
            fprintf(stderr, "NCD Service: -conf requires a path argument\n");
            return 1;
        }
    }

    /* Skip -init in command position (already parsed in first pass) */
    if (argc > 1 + arg_offset &&
        (strcmp(argv[1 + arg_offset], "-init") == 0 || strcmp(argv[1 + arg_offset], "--init-db") == 0)) {
        arg_offset++;
        /* Skip optional drive list if present */
        if (argc > 1 + arg_offset && argv[1 + arg_offset][0] != '-' &&
            strcmp(argv[1 + arg_offset], "start") != 0 &&
            strcmp(argv[1 + arg_offset], "stop") != 0 &&
            strcmp(argv[1 + arg_offset], "status") != 0 &&
            strcmp(argv[1 + arg_offset], "--daemon") != 0) {
            arg_offset++;
        }
    }

    /* Initialize file logging early */
    log_init();
    LOG_EVENT("=== Service starting ===");

    if (argc > 1 + arg_offset) {
        const char *cmd = argv[1 + arg_offset];

        if (strcmp(cmd, "-?") == 0 ||
            strcmp(cmd, "-h") == 0 ||
            strcmp(cmd, "--help") == 0 ||
            strcmp(cmd, "help") == 0) {
            print_usage();
            log_close();
            return 0;
        }
        else if (strcmp(cmd, "start") == 0) {
            LOG_EVENT("Start command received");
            if (is_service_running()) {
                printf("NCD Service: Already running\n");
                log_close();
                return 0;
            }

            /* Check for a stale process that is still in the process list but
             * has already released its mutex / pipe (shutting down) */
#if NCD_PLATFORM_WINDOWS
            DWORD existing_pid = 0;
            if (find_existing_ncd_service_process(&existing_pid)) {
                printf("NCD Service: Waiting for previous instance (PID: %lu) to finish shutting down...\n", existing_pid);
                if (!wait_for_process_exit(existing_pid, 5)) {
                    print_stale_process_error(existing_pid);
                    log_close();
                    return 1;
                }
                printf("NCD Service: Previous instance exited, proceeding with start\n");
            }
#else
            pid_t existing_pid = 0;
            if (find_existing_ncd_service_process(&existing_pid)) {
                printf("NCD Service: Waiting for previous instance (PID: %d) to finish shutting down...\n", (int)existing_pid);
                if (!wait_for_process_exit(existing_pid, 5)) {
                    print_stale_process_error(existing_pid);
                    log_close();
                    return 1;
                }
                printf("NCD Service: Previous instance exited, proceeding with start\n");
            }
#endif
            /* First-run interactive configuration (skip in system mode) */
            if (!ncd_is_system_mode() && !db_metadata_exists()) {
                const char *test_mode = getenv("NCD_TEST_MODE");
                if (!test_mode || !test_mode[0]) {
                    bool stdin_tty = false;
                    bool stdout_tty = false;
#if NCD_PLATFORM_WINDOWS
                    stdin_tty = _isatty(_fileno(stdin)) != 0;
                    stdout_tty = _isatty(_fileno(stdout)) != 0;
#else
                    stdin_tty = isatty(STDIN_FILENO) != 0;
                    stdout_tty = isatty(STDOUT_FILENO) != 0;
#endif
                    if (stdin_tty && stdout_tty) {
                        printf("Welcome to NCD Service! Let's set up your default options.\n");
                        printf("(Use 'ncd -c' anytime to change these settings)\n\n");
                        NcdMetadata *meta = db_metadata_create();
                        if (ui_edit_config(meta)) {
                            meta->config_dirty = true;
                            if (db_metadata_save(meta)) {
                                printf("Configuration saved.\n\n");
                            } else {
                                printf("Warning: Could not save configuration.\n\n");
                            }
                        } else {
                            printf("Using default settings. (Run 'ncd -c' to configure later)\n\n");
                        }
                        db_metadata_free(meta);
                    }
                }
            }
            /* Build extra args string from remaining arguments after "start" */
            char extra_args[1024] = {0};
            int extra_len = 0;
            for (int i = 2 + arg_offset; i < argc && extra_len < (int)sizeof(extra_args) - 2; i++) {
                int n = snprintf(extra_args + extra_len, sizeof(extra_args) - extra_len,
                                 "%s%s", extra_len > 0 ? " " : "", argv[i]);
                if (n > 0) extra_len += n;
            }
#if NCD_PLATFORM_WINDOWS
            int ret = spawn_daemon(g_exe_path, extra_args);
            log_close();
            return ret;
#else
            /* Daemonize: fork and exit parent */
            LOG_DETAIL("Daemonizing...");
            pid_t pid = fork();
            if (pid < 0) {
                fprintf(stderr, "NCD Service: Failed to fork\n");
                LOG_DETAIL("Fork failed");
                log_close();
                return 1;
            }
            if (pid > 0) {
                /* Parent - wait a moment for child to start */
                usleep(500000); /* 500ms */
                if (ipc_service_exists()) {
                    printf("NCD Service started\n");
                    LOG_DETAIL("Parent: Service started successfully");
                } else {
                    fprintf(stderr, "NCD Service: Failed to start\n");
                    LOG_DETAIL("Parent: Service failed to start");
                }
                log_close();
                return 0;
            }
            /* Child continues to run service below */
            /* Close stdio to daemonize properly */
            FILE *dev_null_r = freopen("/dev/null", "r", stdin);
            FILE *dev_null_w1 = freopen("/dev/null", "w", stdout);
            FILE *dev_null_w2 = freopen("/dev/null", "w", stderr);
            (void)dev_null_r; (void)dev_null_w1; (void)dev_null_w2; /* Suppress unused warnings */
            /* Reopen log file in child to avoid sharing with parent */
            if (g_log_file) {
                fclose(g_log_file);
                g_log_file = NULL;
            }
            log_init();
            LOG_DETAIL("Child process continuing after daemonization...");
            /* Fall through to run_service below */
#endif
        }
        else if (strcmp(cmd, "stop") == 0) {
            LOG_EVENT("Stop command received");

            /* Check for "stop block N" syntax */
            int timeout_seconds = 5;  /* Default timeout */
            if (argc > 2 + arg_offset && strcmp(argv[2 + arg_offset], "block") == 0) {
                if (argc > 3 + arg_offset) {
                    timeout_seconds = atoi(argv[3 + arg_offset]);
                    if (timeout_seconds < 1) timeout_seconds = 1;
                    if (timeout_seconds > 300) timeout_seconds = 300;  /* Max 5 minutes */
                }
                LOG_EVENT("Stop block command received, timeout=%ds", timeout_seconds);
            }

            if (!is_service_running()) {
                printf("NCD Service: Not running\n");
                log_close();
                return 1;
            }

            /* Send shutdown request via IPC */
            if (ipc_client_init() != 0) {
                fprintf(stderr, "NCD Service: Failed to init IPC\n");
                log_close();
                return 1;
            }

            NcdIpcClient *client = ipc_client_connect();
            if (!client) {
                fprintf(stderr, "NCD Service: Failed to connect\n");
                ipc_client_cleanup();
                log_close();
                return 1;
            }

            NcdIpcResult result = ipc_client_request_shutdown(client);
            ipc_client_disconnect(client);
            ipc_client_cleanup();

            if (result == NCD_IPC_OK) {
                int wait_result = wait_for_service_stop_with_timeout(timeout_seconds);
                if (wait_result == -1) {
                    fprintf(stderr, "NCD Service: Stop signal sent but service did not stop within %d seconds\n", timeout_seconds);
                    log_close();
                    return -1;
                }
                printf("NCD Service: Stopped\n");
            } else {
                fprintf(stderr, "NCD Service: Failed to send stop signal\n");
                log_close();
                return 1;
            }
            log_close();
            return 0;
        }
        else if (strcmp(cmd, "status") == 0) {
            print_detailed_status();
            log_close();
            return 0;
        }
        else if (strcmp(cmd, "--daemon") == 0) {
            LOG_EVENT("Daemon mode requested");
            /* Fall through to run_service below */
        }
        else {
            fprintf(stderr, "Unknown command: %s\n", cmd);
            print_usage();
            log_close();
            return 1;
        }
    } else {
        /* No arguments */
#if NCD_PLATFORM_WINDOWS
        print_usage();
        printf("\nService status: %s\n", is_service_running() ? "running" : "stopped");
        log_close();
        return 0;
#else
        /* Linux: run service in foreground when called with no args */
        LOG_EVENT("No command provided, running service in foreground");
        /* Fall through to run_service below */
#endif
    }

    /* Run the service (for --daemon, after Linux fork, or Linux no-args) */
    int ret = run_service();
    log_close();
    return ret;
}
