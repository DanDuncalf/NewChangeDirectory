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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <stdarg.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#define sleep(x) Sleep((x) * 1000)
#else
#include <unistd.h>
#include <pthread.h>
#endif

/* --------------------------------------------------------- globals            */

static volatile int g_running = 1;
static volatile int g_flush_requested = 0;
static volatile int g_rescan_requested = 0;
static volatile int g_shutdown_requested = 0;
static int g_debug_mode = 0;  /* Set to 1 for verbose IPC logging */

#if NCD_PLATFORM_WINDOWS
/* Named objects for service instance detection and control */
#define SERVICE_MUTEX_NAME      "NCDService_Instance_7D3F9A2E"
#define SERVICE_STOP_EVENT_NAME "NCDService_Stop_7D3F9A2E"
static HANDLE g_hStopEvent = NULL;
#endif

/* Version info - must match NCD_APP_VERSION in control_ipc.h */
#define SERVICE_VERSION     "1.3"
#define SERVICE_BUILD_STAMP __DATE__ " " __TIME__

/* Debug logging macro */
#define DBG_LOG(...) do { if (g_debug_mode) printf(__VA_ARGS__); } while(0)

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
        /* Fallback to temp directory */
#if NCD_PLATFORM_WINDOWS
        const char *temp = getenv("TEMP");
        if (!temp) temp = getenv("TMP");
        if (temp) {
            snprintf(logs_dir, sizeof(logs_dir), "%s", temp);
        } else {
            snprintf(logs_dir, sizeof(logs_dir), "C:" NCD_PATH_SEP "Windows" NCD_PATH_SEP "Temp");
        }
#else
        const char *tmp = getenv("TMPDIR");
        if (tmp) {
            snprintf(logs_dir, sizeof(logs_dir), "%s", tmp);
        } else {
            snprintf(logs_dir, sizeof(logs_dir), "/tmp");
        }
#endif
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
    fprintf(g_log_file, "[%s] [%lu] [L%d] ", timebuf, (unsigned long)tid, level);
#else
    pthread_t tid = pthread_self();
    fprintf(g_log_file, "[%s] [%lu] [L%d] ", timebuf, (unsigned long)tid, level);
#endif
    
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

/* Thread handle for background loader */
#if NCD_PLATFORM_WINDOWS
static HANDLE g_loader_thread = NULL;
#else
static pthread_t g_loader_thread = 0;
#endif

/* --------------------------------------------------------- signal handling    */

#if NCD_PLATFORM_WINDOWS
static BOOL WINAPI signal_handler(DWORD sig) {
    if (sig == CTRL_C_EVENT || sig == CTRL_BREAK_EVENT) {
        g_running = 0;
        return TRUE;
    }
    return FALSE;
}
#else
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
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
    g_shutdown_requested = 1;
    
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
#if NCD_PLATFORM_WINDOWS
        snprintf(log_path, sizeof(log_path), "%s\\ncd_service.log", log_dir);
#else
        snprintf(log_path, sizeof(log_path), "%s/ncd_service.log", log_dir);
#endif
    } else {
        const char *temp = getenv("TEMP");
        if (!temp) temp = getenv("TMP");
        if (temp) {
#if NCD_PLATFORM_WINDOWS
            snprintf(log_path, sizeof(log_path), "%s\\ncd_service.log", temp);
#else
            snprintf(log_path, sizeof(log_path), "%s/ncd_service.log", temp);
#endif
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

static void handle_submit_metadata(NcdIpcConnection *conn,
                                    uint32_t sequence,
                                    ServiceState *state,
                                    SnapshotPublisher *pub,
                                    const NcdSubmitMetadataPayload *payload,
                                    const void *data) {
    bool success = false;
    bool publish_db = false;
    
    LOG_EVENT("SUBMIT_METADATA request received (seq=%u, type=%s)", 
              sequence, metadata_update_type_to_string(payload->update_type));
    
    switch (payload->update_type) {
        case NCD_META_UPDATE_GROUP_ADD: {
            /* Data format: [name_len (4 bytes)][path_len (4 bytes)][name string][path string] */
            if (data && payload->data_len >= 8) {
                uint32_t name_len = *(uint32_t *)data;
                uint32_t path_len = *(uint32_t *)((char *)data + 4);
                if (name_len > 0 && path_len > 0 && payload->data_len >= 8 + name_len + path_len) {
                    const char *name = (const char *)data + 8;
                    const char *path = (const char *)data + 8 + name_len;
                    success = service_state_add_group(state, name, path);
                }
            }
            break;
        }
        case NCD_META_UPDATE_GROUP_REMOVE: {
            success = service_state_remove_group(state, (const char *)data);
            break;
        }
        case NCD_META_UPDATE_EXCLUSION_ADD: {
            success = service_state_add_exclusion(state, (const char *)data);
            publish_db = success;
            break;
        }
        case NCD_META_UPDATE_EXCLUSION_REMOVE: {
            success = service_state_remove_exclusion(state, (const char *)data);
            break;
        }
        case NCD_META_UPDATE_CONFIG: {
            success = service_state_update_config(state, (const NcdConfig *)data);
            break;
        }
        case NCD_META_UPDATE_CLEAR_HISTORY: {
            success = service_state_clear_history(state);
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_ADD: {
            /* Data format: [path string (null-terminated)][drive byte] */
            if (data && payload->data_len >= 2) {
                const char *path = (const char *)data;
                size_t path_len = strlen(path);
                if (payload->data_len >= path_len + 2) {  /* path + null + drive */
                    char drive = ((const char *)data)[path_len + 1];
                    success = service_state_add_dir_history(state, path, drive);
                }
            }
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_REMOVE: {
            /* data is the 0-based index */
            int idx = *(const int *)data;
            success = service_state_remove_dir_history(state, idx);
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_SWAP: {
            /* No data needed - just swap first two */
            success = service_state_swap_dir_history(state);
            break;
        }
        default:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID, 
                                  "Unknown update type");
            LOG_RESPONSE("SUBMIT_METADATA error sent (seq=%u, unknown type=%d)", 
                         sequence, payload->update_type);
            return;
    }
    
    if (success) {
        /* Republish metadata snapshot */
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
    g_rescan_requested = 1;
    
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
    g_flush_requested = 1;
    
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
    bool success = false;
    bool publish_db = false;
    
    switch (update_type) {
        case NCD_META_UPDATE_GROUP_ADD: {
            /* Data format: [name_len (4 bytes)][path_len (4 bytes)][name string][path string] */
            if (data && data_len >= 8) {
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
            success = service_state_remove_group(state, (const char *)data);
            break;
        case NCD_META_UPDATE_EXCLUSION_ADD:
            success = service_state_add_exclusion(state, (const char *)data);
            publish_db = success;
            break;
        case NCD_META_UPDATE_EXCLUSION_REMOVE:
            success = service_state_remove_exclusion(state, (const char *)data);
            break;
        case NCD_META_UPDATE_CONFIG:
            success = service_state_update_config(state, (const NcdConfig *)data);
            break;
        case NCD_META_UPDATE_CLEAR_HISTORY:
            success = service_state_clear_history(state);
            break;
        case NCD_META_UPDATE_DIR_HISTORY_ADD: {
            /* Data format: [path string (null-terminated)][drive byte] */
            if (data && data_len >= 2) {
                const char *path = (const char *)data;
                size_t path_len = strlen(path);
                if (data_len >= path_len + 2) {  /* path + null + drive */
                    char drive = ((const char *)data)[path_len + 1];
                    success = service_state_add_dir_history(state, path, drive);
                }
            }
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_REMOVE: {
            int idx = *(const int *)data;
            success = service_state_remove_dir_history(state, idx);
            break;
        }
        case NCD_META_UPDATE_DIR_HISTORY_SWAP: {
            success = service_state_swap_dir_history(state);
            break;
        }
    }
    
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
    g_rescan_requested = 1;
}

void handle_pending_flush(ServiceState *state) {
    (void)state;
    g_flush_requested = 1;
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
            if (!payload || payload_len < sizeof(NcdSubmitHeuristicPayload)) {
                return false;
            }
            NcdSubmitHeuristicPayload *hp = (NcdSubmitHeuristicPayload *)payload;
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
            if (!payload || payload_len < sizeof(NcdSubmitMetadataPayload)) {
                return false;
            }
            NcdSubmitMetadataPayload *mp = (NcdSubmitMetadataPayload *)payload;
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
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_NOT_READY,
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
            if (payload && payload_len >= sizeof(NcdSubmitHeuristicPayload)) {
                handle_submit_heuristic(conn, sequence, state, pub,
                                        (const NcdSubmitHeuristicPayload *)payload,
                                        (const char *)payload + sizeof(NcdSubmitHeuristicPayload));
            } else {
                ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID, 
                                      "Invalid payload");
            }
            break;
            
        case NCD_MSG_SUBMIT_METADATA:
            if (payload && payload_len >= sizeof(NcdSubmitMetadataPayload)) {
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

#if NCD_PLATFORM_WINDOWS
static DWORD WINAPI LoaderThreadFunc(LPVOID param) {
#else
static void *loader_thread_func(void *param) {
#endif
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
    extern bool service_state_load_databases(ServiceState *state);
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
    
#if NCD_PLATFORM_WINDOWS
    return 0;
#else
    return NULL;
#endif
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
#if NCD_PLATFORM_WINDOWS
        LoaderThreadFunc(&ctx);
#else
        loader_thread_func(&ctx);
#endif
        return;
    }
    
    g_loader_ctx->state = state;
    g_loader_ctx->pub = pub;
    
    LOG_DETAIL("Background loader context initialized");
    
#if NCD_PLATFORM_WINDOWS
    g_loader_thread = CreateThread(NULL, 0, LoaderThreadFunc, g_loader_ctx, 0, NULL);
    if (!g_loader_thread) {
        fprintf(stderr, "NCD Service: Failed to start loader thread\n");
        LOG_DETAIL("ERROR - Failed to start loader thread (Win32 error=%lu), continuing synchronously", GetLastError());
        free(g_loader_ctx);
        g_loader_ctx = NULL;
        /* Continue synchronously */
        LoaderContext ctx = {state, pub};
        LoaderThreadFunc(&ctx);
    } else {
        LOG_DETAIL("Background loader thread started successfully (Windows)");
    }
#else
    if (pthread_create(&g_loader_thread, NULL, loader_thread_func, g_loader_ctx) != 0) {
        fprintf(stderr, "NCD Service: Failed to start loader thread\n");
        LOG_DETAIL("ERROR - Failed to start loader thread (pthread), continuing synchronously");
        free(g_loader_ctx);
        g_loader_ctx = NULL;
        /* Continue synchronously */
        LoaderContext ctx = {state, pub};
        loader_thread_func(&ctx);
    } else {
        LOG_DETAIL("Background loader thread started successfully (POSIX)");
    }
#endif
}

static void wait_for_loader(void) {
#if NCD_PLATFORM_WINDOWS
    if (g_loader_thread) {
        WaitForSingleObject(g_loader_thread, 5000);
        CloseHandle(g_loader_thread);
        g_loader_thread = NULL;
    }
#else
    if (g_loader_thread) {
        pthread_join(g_loader_thread, NULL);
        g_loader_thread = 0;
    }
#endif
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
    
    /* Scan each drive */
    int scanned_count = 0;
    for (int i = 0; i < drive_count && g_running; i++) {
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
        /* Note: scanner_scan_drive is not exposed, using simplified approach */
        /* In full implementation, use proper scanner API */
        scanned_count++;
        LOG_DETAIL("Drive %c: scanned (%d dirs)", drives[i], drv->dir_count);
    }
    
    /* Update database and publish */
    if (g_running) {
        LOG_DETAIL("Rescan completed, updating database (%d drives scanned)", scanned_count);
        /* Full rescan - not partial */
        service_state_update_database(state, new_db, false);
        service_state_bump_db_generation(state);
        LOG_DETAIL("Publishing updated database snapshot...");
        snapshot_publisher_publish_db(pub, state);
        LOG_EVENT("Database snapshot published after rescan");
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
    
    while (g_running) {
        /* Accept client connections (non-blocking with timeout) */
        NcdIpcConnection *conn = ipc_server_accept(server, 100);  /* 100ms timeout */
        
        if (conn) {
            client_connection_count++;
            /* Handle multiple messages on this connection */
            DBG_LOG("NCD Service: Client connected, handling messages...\n");
            LOG_DETAIL("Client #%d connected", client_connection_count);
            int msg_count = 0;
            while (g_running) {
                int result = handle_client_connection(conn, state, pub);
                if (result < 0) {
                    /* Client disconnected or error */
                    break;
                }
                msg_count++;
                /* Brief pause to prevent busy-waiting */
                #if NCD_PLATFORM_WINDOWS
                Sleep(1);
                #else
                usleep(1000);
                #endif
            }
            DBG_LOG("NCD Service: Client disconnected after %d messages\n", msg_count);
            LOG_DETAIL("Client #%d disconnected after %d messages", client_connection_count, msg_count);
            ipc_server_close_connection(conn);
        }
        
        /* Check for rescan request */
        if (g_rescan_requested) {
            g_rescan_requested = 0;
            perform_rescan(state, pub);
        }
        
        /* Check for shutdown request */
        if (g_shutdown_requested) {
            LOG_EVENT("Shutdown requested, exiting main loop");
            g_running = 0;
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
        bool needs_immediate = g_flush_requested;
        
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
            g_flush_requested = 0;
            last_flush_check = now;
            if (service_state_flush(state)) {
                LOG_DETAIL("State flushed successfully");
            } else {
                LOG_DETAIL("ERROR - Failed to flush state");
            }
        }
        
        /* Small sleep to prevent busy-waiting */
        #if NCD_PLATFORM_WINDOWS
        Sleep(10);
        #else
        usleep(10000);
        #endif
    }
    
    LOG_DETAIL("Cleaning up IPC server (total clients handled: %d)", client_connection_count);
    ipc_server_cleanup(server);
    LOG_EVENT("Service main loop ended");
    LOG_DETAIL("Exiting service_loop");
}

/* --------------------------------------------------------- main               */

/* Forward declaration */
#if NCD_PLATFORM_WINDOWS
static bool is_service_running(void);
#endif

/* Wait for service to stop with specified timeout */
static int wait_for_service_stop_with_timeout(int timeout_seconds) {
    int wait_count = 0;
    int max_wait = timeout_seconds * 10;  /* 100ms per iteration */
    
#if NCD_PLATFORM_WINDOWS
    while (is_service_running() && wait_count < max_wait) {
        Sleep(100);
        wait_count++;
    }
    return is_service_running() ? -1 : 0;
#else
    while (ipc_service_exists() && wait_count < max_wait) {
        usleep(100000);
        wait_count++;
    }
    return ipc_service_exists() ? -1 : 0;
#endif
}

/* --------------------------------------------------------- Windows service control */

#if NCD_PLATFORM_WINDOWS

/* Check if service is already running */
static bool is_service_running(void) {
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, SERVICE_MUTEX_NAME);
    if (hMutex) {
        CloseHandle(hMutex);
        return true;
    }
    return false;
}

/* Print service usage information */
static void print_usage(void) {
    printf("NCD Service - Resident state service for NewChangeDirectory\n");
    printf("\n");
    printf("Usage:\n");
    printf("  ncdservice start              Start the service (daemon mode)\n");
    printf("  ncdservice stop               Stop the running service (waits up to 5s)\n");
    printf("  ncdservice stop block <N>     Stop and wait N seconds for shutdown\n");
    printf("                                Returns -1 if service didn't stop in time\n");
    printf("  ncdservice status             Show service status (running/stopped)\n");
    printf("  ncdservice /agdb              Run service in foreground with debug output\n");
    printf("  ncdservice -conf <path>       Use custom config file\n");
    printf("  ncdservice --daemon           Run service in foreground (internal use)\n");
    printf("  ncdservice -log<n>            Enable logging (n=0-5, see below)\n");
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

/* Spawn detached child process to run as daemon */
static int spawn_daemon(const char *exe_path) {
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH * 2];
    
    snprintf(cmd, sizeof(cmd), "\"%s\" --daemon", exe_path);
    
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

/* Run the actual service (daemon mode) */
static int run_service(void) {
    LOG_DETAIL("Entering run_service (Windows)");
    
    /* Create mutex to indicate we're running */
    HANDLE hMutex = CreateMutexA(NULL, TRUE, SERVICE_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "NCD Service: Already running\n");
        LOG_EVENT("Service start failed - already running");
        CloseHandle(hMutex);
        return 1;
    }
    LOG_DETAIL("Service mutex created successfully");
    
    (void)g_hStopEvent; /* Unused - IPC used for shutdown */
    
    setup_signal_handlers();
    LOG_DETAIL("Signal handlers set up");
    
    /* Initialize service state */
    LOG_DETAIL("Initializing service state...");
    ServiceState *state = service_state_init();
    if (!state) {
        fprintf(stderr, "NCD Service: Failed to initialize state\n");
        LOG_EVENT("Service start failed - state initialization error");
        CloseHandle(hMutex);
        return 1;
    }
    LOG_DETAIL("Service state initialized successfully");
    
    /* Initialize snapshot publisher */
    LOG_DETAIL("Initializing snapshot publisher...");
    SnapshotPublisher *pub = snapshot_publisher_init();
    if (!pub) {
        fprintf(stderr, "NCD Service: Failed to initialize publisher\n");
        LOG_DETAIL("ERROR - Failed to initialize snapshot publisher");
        service_state_cleanup(state);
        CloseHandle(hMutex);
        return 1;
    }
    LOG_DETAIL("Snapshot publisher initialized successfully");
    
    /* Publish initial metadata snapshot */
    LOG_DETAIL("Publishing initial metadata snapshot...");
    if (!snapshot_publisher_publish_meta(pub, state)) {
        fprintf(stderr, "NCD Service: Failed to publish metadata snapshot\n");
        LOG_DETAIL("ERROR - Failed to publish initial metadata snapshot");
        snapshot_publisher_cleanup(pub);
        service_state_cleanup(state);
        CloseHandle(hMutex);
        return 1;
    }
    LOG_EVENT("Metadata snapshot published");
    
    /* Initialize IPC server BEFORE starting background loader
     * This allows clients to connect immediately and check service status
     * while the background loader is still initializing */
    LOG_DETAIL("Initializing IPC server...");
    NcdIpcServer *server = ipc_server_init();
    if (!server) {
        fprintf(stderr, "NCD Service: Failed to initialize IPC server\n");
        LOG_DETAIL("ERROR - Failed to initialize IPC server");
        snapshot_publisher_cleanup(pub);
        service_state_cleanup(state);
        CloseHandle(hMutex);
        return 1;
    }
    LOG_DETAIL("IPC server initialized successfully");
    LOG_EVENT("IPC server ready - accepting client connections");
    
    /* Start background loader AFTER IPC is ready
     * Clients can now connect and see STARTING/LOADING state */
    LOG_DETAIL("Starting background loader...");
    start_background_loader(state, pub);
    
    /* Run service loop - IPC server already initialized */
    LOG_DETAIL("Entering service main loop");
    
    time_t last_flush_check = time(NULL);
    const int FLUSH_INTERVAL_SEC = 30;
    int client_connection_count = 0;
    
    while (g_running) {
            
            /* Accept client connections (100ms timeout) */
            NcdIpcConnection *conn = ipc_server_accept(server, 100);
            
            if (conn) {
                client_connection_count++;
                /* Handle multiple messages on this connection */
                DBG_LOG("NCD Service: Client connected, handling messages...\n");
                LOG_DETAIL("Client #%d connected", client_connection_count);
                int msg_count = 0;
                while (g_running) {
                    int result = handle_client_connection(conn, state, pub);
                    if (result < 0) {
                        /* Client disconnected or error */
                        break;
                    }
                    msg_count++;
                    /* Brief pause to prevent busy-waiting */
                    Sleep(1);
                }
                DBG_LOG("NCD Service: Client disconnected after %d messages\n", msg_count);
                LOG_DETAIL("Client #%d disconnected after %d messages", client_connection_count, msg_count);
                ipc_server_close_connection(conn);
            }
            
            /* Check for rescan request */
            if (g_rescan_requested) {
                g_rescan_requested = 0;
                perform_rescan(state, pub);
            }
            
            /* Check for shutdown request */
            if (g_shutdown_requested) {
                LOG_EVENT("Shutdown requested, exiting service loop");
                g_running = 0;
                break;
            }
            
            /* Check for flush */
            time_t now = time(NULL);
            bool needs_immediate = g_flush_requested || service_state_needs_immediate_flush(state);
            bool needs_periodic = service_state_needs_flush(state) && 
                                  (now - last_flush_check) > FLUSH_INTERVAL_SEC;
            
            if (needs_immediate || needs_periodic) {
                LOG_DETAIL("Flushing state (immediate=%d, periodic=%d)", needs_immediate, needs_periodic);
                g_flush_requested = 0;
                last_flush_check = now;
                service_state_flush(state);
            }
            
            Sleep(10);
        }
        
    LOG_DETAIL("Cleaning up IPC server (total clients: %d)", client_connection_count);
    ipc_server_cleanup(server);
    
    /* Cleanup */
    LOG_DETAIL("Beginning service cleanup...");
    g_running = 0;
    signal_loader_stop();
    wait_for_loader();
    LOG_DETAIL("Background loader stopped");
    
    if (service_state_needs_flush(state)) {
        LOG_DETAIL("Final state flush before exit...");
        service_state_flush(state);
    }
    
    LOG_DETAIL("Cleaning up snapshot publisher...");
    snapshot_publisher_cleanup(pub);
    LOG_DETAIL("Cleaning up service state...");
    service_state_cleanup(state);
    
    LOG_DETAIL("Closing service mutex");
    CloseHandle(hMutex);
    
    LOG_EVENT("Service stopped successfully");
    LOG_DETAIL("Exiting run_service");
    
    return 0;
}

#endif /* NCD_PLATFORM_WINDOWS */

/* --------------------------------------------------------- main               */

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

/* Parse -log<n> option from argument */
static int parse_log_option(const char *arg) {
    if (strncmp(arg, "-log", 4) == 0 || strncmp(arg, "/log", 4) == 0) {
        const char *level_str = arg + 4;
        if (*level_str >= '0' && *level_str <= '5' && level_str[1] == '\0') {
            return *level_str - '0';
        }
    }
    return -1;  /* Not a valid -log option */
}

int main(int argc, char *argv[]) {
#if NCD_PLATFORM_WINDOWS
    /* Get executable path for spawning daemon */
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    
    /* Parse command line arguments */
    /* First pass: look for logging options before other processing */
    for (int i = 1; i < argc; i++) {
        int log_level = parse_log_option(argv[i]);
        if (log_level >= 0) {
            g_log_level = log_level;
            /* Initialize logging early */
            log_init();
            LOG_EVENT("Logging enabled at level %d", g_log_level);
        }
    }
    
    if (argc > 1) {
        /* Check for debug flag */
        if (strcmp(argv[1], "/agdb") == 0 || strcmp(argv[1], "-agdb") == 0) {
            g_debug_mode = 1;
            if (g_log_level == NCD_LOG_DISABLED) {
                g_log_level = NCD_LOG_LEVEL_2;  /* Default to detailed logging in debug mode */
                log_init();
            }
            fprintf(stderr, "NCD Service: Debug mode enabled\n");
            LOG_EVENT("Debug mode enabled (/agdb)");
            fflush(stderr);
            /* Shift arguments and continue parsing */
            if (argc > 2) {
                argv[1] = argv[2];
                argc--;
            } else {
                /* Only /agdb was passed, run service */
                fprintf(stderr, "NCD Service: Calling run_service()...\n");
                fflush(stderr);
                return run_service();
            }
        }
        
        /* Check for config override */
        if (strcmp(argv[1], "-conf") == 0) {
            if (argc > 2) {
                db_metadata_set_override(argv[2]);
                printf("NCD Service: Using config override: %s\n", argv[2]);
                LOG_DETAIL("Config override set: %s", argv[2]);
                /* Shift arguments and continue parsing */
                if (argc > 3) {
                    argv[1] = argv[3];
                    argc -= 2;
                } else {
                    /* Only -conf was passed, run service */
                    return run_service();
                }
            } else {
                fprintf(stderr, "NCD Service: -conf requires a path argument\n");
                return 1;
            }
        }
        
        /* Check for log level option at position 1 */
        int log_level = parse_log_option(argv[1]);
        if (log_level >= 0) {
            /* Already handled in first pass, just shift arguments */
            if (argc > 2) {
                argv[1] = argv[2];
                argc--;
            } else {
                /* Only -log<n> was passed, run service */
                return run_service();
            }
        }
        
        if (strcmp(argv[1], "start") == 0) {
            LOG_EVENT("Start command received");
            if (is_service_running()) {
                printf("NCD Service: Already running\n");
                return 0;
            }
            return spawn_daemon(exe_path);
        }
        
        if (strcmp(argv[1], "stop") == 0) {
            LOG_EVENT("Stop command received");
            
            /* Check for "stop block N" syntax */
            int timeout_seconds = 5;  /* Default timeout */
            if (argc > 2 && strcmp(argv[2], "block") == 0) {
                if (argc > 3) {
                    timeout_seconds = atoi(argv[3]);
                    if (timeout_seconds < 1) timeout_seconds = 1;
                    if (timeout_seconds > 300) timeout_seconds = 300;  /* Max 5 minutes */
                }
                LOG_EVENT("Stop block command received, timeout=%ds", timeout_seconds);
            }
            
            if (!is_service_running()) {
                printf("NCD Service: Not running\n");
                return 1;
            }
            
            /* Send shutdown request via IPC */
            if (ipc_client_init() != 0) {
                fprintf(stderr, "NCD Service: Failed to init IPC\n");
                return 1;
            }
            
            NcdIpcClient *client = ipc_client_connect();
            if (!client) {
                fprintf(stderr, "NCD Service: Failed to connect\n");
                ipc_client_cleanup();
                return 1;
            }
            
            NcdIpcResult result = ipc_client_request_shutdown(client);
            ipc_client_disconnect(client);
            ipc_client_cleanup();
            
            if (result == NCD_IPC_OK) {
                int wait_result = wait_for_service_stop_with_timeout(timeout_seconds);
                if (wait_result == -1) {
                    fprintf(stderr, "NCD Service: Stop signal sent but service did not stop within %d seconds\n", timeout_seconds);
                    return -1;
                }
                printf("NCD Service: Stopped\n");
                return 0;
            }
            fprintf(stderr, "NCD Service: Failed to send stop signal\n");
            return 1;
        }
        
        if (strcmp(argv[1], "status") == 0) {
            print_detailed_status();
            return 0;
        }
        
        if (strcmp(argv[1], "--daemon") == 0) {
            LOG_EVENT("Daemon mode requested");
            return run_service();
        }
        
        /* Unknown command - fall through to print usage */
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        /* fall through to print_usage */
    }
    
    /* No arguments = print usage and status */
    print_usage();
    printf("\nService status: %s\n", is_service_running() ? "running" : "stopped");
    return 0;
    
#else /* Linux/POSIX */
    
    /* Parse command line arguments - first pass for logging options */
    for (int i = 1; i < argc; i++) {
        int log_level = parse_log_option(argv[i]);
        if (log_level >= 0) {
            g_log_level = log_level;
        }
    }
    
    /* Initialize file logging early */
    log_init();
    LOG_EVENT("=== Service starting (Linux/POSIX) ===");
    
    /* Parse command line arguments */
    if (argc > 1) {
        /* Check for debug flag first */
        int arg_offset = 0;
        if (strcmp(argv[1], "/agdb") == 0 || strcmp(argv[1], "-agdb") == 0) {
            g_debug_mode = 1;
            if (g_log_level == NCD_LOG_DISABLED) {
                g_log_level = NCD_LOG_LEVEL_2;
                log_init();
            }
            printf("NCD Service: Debug mode enabled\n");
            LOG_EVENT("Debug mode enabled via /agdb");
            arg_offset = 1;
        }
        
        /* Check for log level option */
        int log_opt = parse_log_option(argv[1 + arg_offset]);
        if (log_opt >= 0) {
            arg_offset++;  /* Skip the -log<n> argument */
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
                LOG_DETAIL("Error: -conf without path argument");
                log_close();
                return 1;
            }
        }
        
        /* Handle commands */
        if (argc > 1 + arg_offset) {
            const char *cmd = argv[1 + arg_offset];
            
            if (strcmp(cmd, "start") == 0) {
                LOG_EVENT("Start command received");
                /* Check if already running */
                if (ipc_service_exists()) {
                    printf("NCD Service: Already running\n");
                    LOG_DETAIL("Start requested but already running");
                    log_close();
                    return 0;
                }
                
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
                
                if (!ipc_service_exists()) {
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
                LOG_DETAIL("Status check: detailed status printed");
                log_close();
                return 0;
            }
            else if (strcmp(cmd, "--daemon") == 0) {
                /* Run directly (already daemonized) */
                LOG_EVENT("Daemon mode requested");
            }
            else {
                fprintf(stderr, "Unknown command: %s\n", cmd);
                fprintf(stderr, "Usage: %s [/agdb] [-conf <path>] [start|stop|status]\n", argv[0]);
                LOG_DETAIL("Unknown command: %s", cmd);
                log_close();
                return 1;
            }
        }
    }
    
    setup_signal_handlers();
    LOG_DETAIL("Signal handlers set up");
    
    /* Initialize service state */
    LOG_DETAIL("Initializing service state...");
    ServiceState *state = service_state_init();
    if (!state) {
        fprintf(stderr, "NCD Service: Failed to initialize state\n");
        LOG_DETAIL("ERROR - Failed to initialize state");
        log_close();
        return 1;
    }
    LOG_DETAIL("Service state initialized");
    
    /* Initialize snapshot publisher */
    LOG_DETAIL("Initializing snapshot publisher...");
    SnapshotPublisher *pub = snapshot_publisher_init();
    if (!pub) {
        fprintf(stderr, "NCD Service: Failed to initialize publisher\n");
        LOG_DETAIL("ERROR - Failed to initialize publisher");
        service_state_cleanup(state);
        log_close();
        return 1;
    }
    LOG_DETAIL("Snapshot publisher initialized");
    
    /* Publish initial metadata snapshot */
    LOG_DETAIL("Publishing initial metadata snapshot...");
    if (!snapshot_publisher_publish_meta(pub, state)) {
        fprintf(stderr, "NCD Service: Failed to publish metadata snapshot\n");
        LOG_DETAIL("ERROR - Failed to publish metadata snapshot");
        snapshot_publisher_cleanup(pub);
        service_state_cleanup(state);
        log_close();
        return 1;
    }
    LOG_EVENT("Metadata snapshot published");
    
    /* Initialize IPC server BEFORE starting background loader
     * This allows clients to connect immediately and check service status
     * while the background loader is still initializing */
    LOG_DETAIL("Initializing IPC server...");
    NcdIpcServer *server = ipc_server_init();
    if (!server) {
        fprintf(stderr, "NCD Service: Failed to initialize IPC server\n");
        LOG_DETAIL("ERROR - Failed to initialize IPC server");
        snapshot_publisher_cleanup(pub);
        service_state_cleanup(state);
        log_close();
        return 1;
    }
    LOG_DETAIL("IPC server initialized successfully");
    LOG_EVENT("IPC server ready - accepting client connections");
    
    /* Start background loader AFTER IPC is ready
     * Clients can now connect and see STARTING/LOADING state */
    LOG_DETAIL("Starting background loader...");
    start_background_loader(state, pub);
    
    /* Run service loop with existing IPC server */
    LOG_DETAIL("Entering service loop...");
    service_loop(state, pub, server);
    
    /* Cleanup */
    LOG_DETAIL("Beginning service cleanup...");
    g_running = 0;
    signal_loader_stop();
    wait_for_loader();
    LOG_DETAIL("Background loader stopped");
    
    if (service_state_needs_flush(state)) {
        LOG_DETAIL("Final state flush...");
        service_state_flush(state);
    }
    
    LOG_DETAIL("Cleaning up snapshot publisher...");
    snapshot_publisher_cleanup(pub);
    LOG_DETAIL("Cleaning up service state...");
    service_state_cleanup(state);
    
    LOG_EVENT("Service stopped successfully");
    log_close();
    
    return 0;
    
#endif /* NCD_PLATFORM_WINDOWS */
}
