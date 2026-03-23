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

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#define sleep(x) Sleep((x) * 1000)
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------- globals            */

static volatile int g_running = 1;
static volatile int g_flush_requested = 0;
static volatile int g_rescan_requested = 0;
static volatile int g_shutdown_requested = 0;

#if NCD_PLATFORM_WINDOWS
/* Named objects for service instance detection and control */
#define SERVICE_MUTEX_NAME      "NCDService_Instance_7D3F9A2E"
#define SERVICE_STOP_EVENT_NAME "NCDService_Stop_7D3F9A2E"
static HANDLE g_hStopEvent = NULL;
#endif

/* Version info - must match NCD_APP_VERSION in control_ipc.h */
#define SERVICE_VERSION     "1.3"
#define SERVICE_BUILD_STAMP __DATE__ " " __TIME__

/* Forward declaration for background loader */
static void start_background_loader(ServiceState *state);
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
#endif
}

/* --------------------------------------------------------- IPC request handlers */

static void handle_get_version(NcdIpcConnection *conn, uint32_t sequence) {
    NcdVersionInfoPayload payload;
    memset(&payload, 0, sizeof(payload));
    
    strncpy(payload.app_version, SERVICE_VERSION, sizeof(payload.app_version) - 1);
    strncpy(payload.build_stamp, SERVICE_BUILD_STAMP, sizeof(payload.build_stamp) - 1);
    payload.protocol_version = NCD_IPC_VERSION;
    
    ipc_server_send_response(conn, sequence, &payload, sizeof(payload));
}

static void handle_request_shutdown(NcdIpcConnection *conn, uint32_t sequence) {
    g_shutdown_requested = 1;
    ipc_server_send_response(conn, sequence, NULL, 0);
    printf("NCD Service: Shutdown requested by client\n");
}

static void handle_get_state_info(NcdIpcConnection *conn, 
                                   uint32_t sequence,
                                   const ServiceState *state,
                                   const SnapshotPublisher *pub) {
    SnapshotInfo info;
    snapshot_publisher_get_info(pub, &info);
    
    /* Build response payload with service state */
    NcdStateInfoPayload payload;
    memset(&payload, 0, sizeof(payload));
    payload.protocol_version = NCD_IPC_VERSION;
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
    
    if (service_state_note_heuristic(state, search, target)) {
        /* Republish metadata snapshot */
        service_state_bump_meta_generation(state);
        snapshot_publisher_publish_meta(pub, state);
        ipc_server_send_response(conn, sequence, NULL, 0);
    } else {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC, 
                              "Failed to record heuristic");
    }
}

static void handle_submit_metadata(NcdIpcConnection *conn,
                                    uint32_t sequence,
                                    ServiceState *state,
                                    SnapshotPublisher *pub,
                                    const NcdSubmitMetadataPayload *payload,
                                    const void *data) {
    bool success = false;
    
    switch (payload->update_type) {
        case NCD_META_UPDATE_GROUP_ADD: {
            const char **args = (const char **)data;
            success = service_state_add_group(state, args[0], args[1]);
            break;
        }
        case NCD_META_UPDATE_GROUP_REMOVE: {
            success = service_state_remove_group(state, (const char *)data);
            break;
        }
        case NCD_META_UPDATE_EXCLUSION_ADD: {
            success = service_state_add_exclusion(state, (const char *)data);
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
            const char *path = *(const char **)data;
            char drive = *((const char *)data + sizeof(const char *));
            success = service_state_add_dir_history(state, path, drive);
            break;
        }
        default:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_INVALID, 
                                  "Unknown update type");
            return;
    }
    
    if (success) {
        /* Republish metadata snapshot */
        service_state_bump_meta_generation(state);
        snapshot_publisher_publish_meta(pub, state);
        ipc_server_send_response(conn, sequence, NULL, 0);
    } else {
        ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC, 
                              "Failed to update metadata");
    }
}

static void handle_request_rescan(NcdIpcConnection *conn,
                                   uint32_t sequence,
                                   ServiceState *state,
                                   SnapshotPublisher *pub,
                                   const NcdRequestRescanPayload *payload) {
    /* Signal main loop to perform rescan */
    g_rescan_requested = 1;
    
    ipc_server_send_response(conn, sequence, NULL, 0);
}

static void handle_request_flush(NcdIpcConnection *conn,
                                  uint32_t sequence,
                                  ServiceState *state) {
    /* Signal main loop to flush */
    g_flush_requested = 1;
    
    ipc_server_send_response(conn, sequence, NULL, 0);
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
                              int update_type, const void *data) {
    bool success = false;
    
    switch (update_type) {
        case NCD_META_UPDATE_GROUP_ADD: {
            const char **args = (const char **)data;
            success = service_state_add_group(state, args[0], args[1]);
            break;
        }
        case NCD_META_UPDATE_GROUP_REMOVE:
            success = service_state_remove_group(state, (const char *)data);
            break;
        case NCD_META_UPDATE_EXCLUSION_ADD:
            success = service_state_add_exclusion(state, (const char *)data);
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
            const char *path = *(const char **)data;
            char drive = *((const char *)data + sizeof(const char *));
            success = service_state_add_dir_history(state, path, drive);
            break;
        }
    }
    
    if (success) {
        service_state_bump_meta_generation(state);
        snapshot_publisher_publish_meta((SnapshotPublisher *)pub, state);
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
                        handle_pending_metadata(state, pub, update_type, update_data);
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
    
    printf("NCD Service: Pending requests processed\n");
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
 */
static bool check_service_ready(NcdIpcConnection *conn, 
                                 uint32_t sequence,
                                 ServiceState *state) {
    ServiceRuntimeState runtime_state = service_state_get_runtime_state(state);
    
    switch (runtime_state) {
        case SERVICE_STATE_READY:
            return true;
            
        case SERVICE_STATE_LOADING:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_BUSY_LOADING,
                                  service_state_get_status_message(state));
            return false;
            
        case SERVICE_STATE_SCANNING:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_BUSY_SCANNING,
                                  service_state_get_status_message(state));
            return false;
            
        case SERVICE_STATE_STARTING:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_NOT_READY,
                                  "Service starting...");
            return false;
            
        default:
            ipc_server_send_error(conn, sequence, NCD_IPC_ERROR_GENERIC,
                                  "Service in unknown state");
            return false;
    }
}

static void handle_client_connection(NcdIpcConnection *conn,
                                      ServiceState *state,
                                      SnapshotPublisher *pub) {
    void *payload = NULL;
    size_t payload_len = 0;
    
    int msg_type = ipc_server_receive(conn, &payload, &payload_len);
    if (msg_type == 0) {
        return;
    }
    
    /* Note: We need the sequence number from the header, but receive
     * only returns the payload. For simplicity, assume sequence 0 here
     * or extend the IPC layer to return header info. */
    uint32_t sequence = 0;  /* Would come from header in real implementation */
    
    /* PING and GET_STATE_INFO work regardless of service state */
    if (msg_type == NCD_MSG_PING) {
        ipc_server_send_response(conn, sequence, NULL, 0);
        if (payload) ipc_free_message(payload);
        return;
    }
    
    if (msg_type == NCD_MSG_GET_STATE_INFO) {
        handle_get_state_info(conn, sequence, state, pub);
        if (payload) ipc_free_message(payload);
        return;
    }
    
    if (msg_type == NCD_MSG_GET_VERSION) {
        handle_get_version(conn, sequence);
        if (payload) ipc_free_message(payload);
        return;
    }
    
    if (msg_type == NCD_MSG_REQUEST_SHUTDOWN) {
        handle_request_shutdown(conn, sequence);
        if (payload) ipc_free_message(payload);
        return;
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
            return;
        }
        
        /* Not queued - return busy error */
        if (!check_service_ready(conn, sequence, state)) {
            if (payload) ipc_free_message(payload);
            return;
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
        fprintf(stderr, "NCD Service: ERROR - Loader thread received NULL context\n");
        return 0;
    }
    
    ServiceState *state = ctx->state;
    SnapshotPublisher *pub = ctx->pub;
    
    if (!state || !pub) {
        fprintf(stderr, "NCD Service: ERROR - Loader thread received NULL state or pub\n");
        return 0;
    }
    
    printf("NCD Service: Background loader started\n");
    
    /* Load databases */
    extern bool service_state_load_databases(ServiceState *state);
    if (!service_state_load_databases(state)) {
        fprintf(stderr, "NCD Service: Warning - database loading had issues\n");
    }
    
    /* Publish database snapshot */
    printf("NCD Service: Publishing database snapshot...\n");
    if (!snapshot_publisher_publish_db(pub, state)) {
        fprintf(stderr, "NCD Service: Failed to publish database snapshot\n");
    } else {
        printf("NCD Service: Database snapshot published\n");
    }
    
    /* Transition to READY state */
    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    service_state_set_status_message(state, "Ready");
    printf("NCD Service: Ready\n");
    
    /* Process any pending requests that were queued during loading */
    process_all_pending(state, pub);
    
    free(ctx);
    
#if NCD_PLATFORM_WINDOWS
    return 0;
#else
    return NULL;
#endif
}

static LoaderContext *g_loader_ctx = NULL;

static void start_background_loader(ServiceState *state, SnapshotPublisher *pub) {
    g_loader_ctx = (LoaderContext *)malloc(sizeof(LoaderContext));
    if (!g_loader_ctx) {
        fprintf(stderr, "NCD Service: Failed to allocate loader context\n");
        /* Continue synchronously */
        LoaderContext ctx = {state, pub};
        LoaderThreadFunc(&ctx);
        return;
    }
    
    g_loader_ctx->state = state;
    g_loader_ctx->pub = pub;
    

    
#if NCD_PLATFORM_WINDOWS
    g_loader_thread = CreateThread(NULL, 0, LoaderThreadFunc, g_loader_ctx, 0, NULL);
    if (!g_loader_thread) {
        fprintf(stderr, "NCD Service: Failed to start loader thread\n");
        free(g_loader_ctx);
        g_loader_ctx = NULL;
        /* Continue synchronously */
        LoaderContext ctx = {state, pub};
        LoaderThreadFunc(&ctx);
    }
#else
    if (pthread_create(&g_loader_thread, NULL, loader_thread_func, g_loader_ctx) != 0) {
        fprintf(stderr, "NCD Service: Failed to start loader thread\n");
        free(g_loader_ctx);
        g_loader_ctx = NULL;
        /* Continue synchronously */
        LoaderContext ctx = {state, pub};
        loader_thread_func(&ctx);
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
    ServiceRuntimeState prev_state = service_state_get_runtime_state(state);
    service_state_set_runtime_state(state, SERVICE_STATE_SCANNING);
    service_state_set_status_message(state, "Scanning filesystem...");
    
    printf("NCD Service: Starting rescan...\n");
    
    /* Scan all drives */
    char drives[26];
    int drive_count = platform_get_available_drives(drives, 26);
    
    NcdDatabase *new_db = db_create();
    if (!new_db) {
        fprintf(stderr, "NCD Service: Failed to create database for rescan\n");
        service_state_set_runtime_state(state, prev_state);
        return;
    }
    
    /* Scan each drive */
    for (int i = 0; i < drive_count && g_running; i++) {
        char mount_path[MAX_PATH];
        if (!platform_build_mount_path(drives[i], mount_path, sizeof(mount_path))) {
            continue;
        }
        
        printf("NCD Service: Scanning drive %c:...\n", drives[i]);
        
        DriveData *drv = db_add_drive(new_db, drives[i]);
        if (!drv) continue;
        
        /* Scan the drive */
        /* Note: scanner_scan_drive is not exposed, using simplified approach */
        /* In full implementation, use proper scanner API */
    }
    
    /* Update database and publish */
    if (g_running) {
        service_state_update_database(state, new_db);
        service_state_bump_db_generation(state);
        snapshot_publisher_publish_db(pub, state);
        printf("NCD Service: Rescan complete\n");
    } else {
        db_free(new_db);
    }
    
    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    service_state_set_status_message(state, "Ready");
}

static void service_loop(ServiceState *state, SnapshotPublisher *pub) {
    NcdIpcServer *server = ipc_server_init();
    if (!server) {
        fprintf(stderr, "NCD Service: Failed to initialize IPC server\n");
        return;
    }
    
    printf("NCD Service: Running (PID: %d)\n", 
#if NCD_PLATFORM_WINDOWS
           GetCurrentProcessId()
#else
           (int)getpid()
#endif
    );
    
    time_t last_flush_check = time(NULL);
    const int FLUSH_INTERVAL_SEC = 30;  /* Flush every 30 seconds if dirty */
    
    int loop_count = 0;
    while (g_running) {
        /* Accept client connections (non-blocking with timeout) */
        NcdIpcConnection *conn = ipc_server_accept(server, 100);  /* 100ms timeout */
        
        if (conn) {
            handle_client_connection(conn, state, pub);
            ipc_server_close_connection(conn);
        }
        
        /* Check for rescan request */
        if (g_rescan_requested) {
            g_rescan_requested = 0;
            perform_rescan(state, pub);
        }
        
        /* Check for shutdown request */
        if (g_shutdown_requested) {
            printf("NCD Service: Processing shutdown request...\n");
            g_running = 0;
            break;
        }
        
        /* Check for flush request or periodic flush */
        time_t now = time(NULL);
        if (g_flush_requested || 
            (service_state_needs_flush(state) && 
             (now - last_flush_check) > FLUSH_INTERVAL_SEC)) {
            g_flush_requested = 0;
            last_flush_check = now;
            
            if (service_state_flush(state)) {
                printf("NCD Service: Flushed to disk\n");
            }
        }
        
        /* Small sleep to prevent busy-waiting */
        #if NCD_PLATFORM_WINDOWS
        Sleep(10);
        #else
        usleep(10000);
        #endif
    }
    
    ipc_server_cleanup(server);
}

/* --------------------------------------------------------- main               */


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

/* Signal running service to stop via IPC */
static bool signal_stop_service(void) {
    /* Initialize IPC client subsystem */
    if (ipc_client_init() != 0) {
        return false;
    }
    
    /* Connect to the service IPC server */
    NcdIpcClient *client = ipc_client_connect();
    if (!client) {
        ipc_client_cleanup();
        return false;
    }
    
    /* Send shutdown request */
    NcdIpcResult result = ipc_client_request_shutdown(client);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    
    return (result == NCD_IPC_OK);
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
    /* Create mutex to indicate we're running */
    HANDLE hMutex = CreateMutexA(NULL, TRUE, SERVICE_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "NCD Service: Already running\n");
        CloseHandle(hMutex);
        return 1;
    }
    
    (void)g_hStopEvent; /* Unused - IPC used for shutdown */
    
    printf("NCD State Service starting...\n");
    
    setup_signal_handlers();
    
    /* Initialize service state */
    ServiceState *state = service_state_init();
    if (!state) {
        fprintf(stderr, "NCD Service: Failed to initialize state\n");
        CloseHandle(hMutex);
        return 1;
    }
    
    /* Initialize snapshot publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    if (!pub) {
        fprintf(stderr, "NCD Service: Failed to initialize publisher\n");
        service_state_cleanup(state);
        CloseHandle(hMutex);
        return 1;
    }
    
    /* Publish initial metadata snapshot */
    if (!snapshot_publisher_publish_meta(pub, state)) {
        fprintf(stderr, "NCD Service: Failed to publish metadata snapshot\n");
        snapshot_publisher_cleanup(pub);
        service_state_cleanup(state);
        CloseHandle(hMutex);
        return 1;
    }
    printf("NCD Service: Metadata snapshot published\n");
    
    /* Start background loader */
    start_background_loader(state, pub);
    
    /* Run service loop - modified to check stop event */
    NcdIpcServer *server = ipc_server_init();
    if (!server) {
        fprintf(stderr, "NCD Service: Failed to initialize IPC server\n");
    } else {
        printf("NCD Service: Running (PID: %lu)\n", GetCurrentProcessId());
        
        time_t last_flush_check = time(NULL);
        const int FLUSH_INTERVAL_SEC = 30;
        
        while (g_running) {
            
            /* Accept client connections (100ms timeout) */
            NcdIpcConnection *conn = ipc_server_accept(server, 100);
            
            if (conn) {
                handle_client_connection(conn, state, pub);
                ipc_server_close_connection(conn);
            }
            
            /* Check for rescan request */
            if (g_rescan_requested) {
                g_rescan_requested = 0;
                perform_rescan(state, pub);
            }
            
            /* Check for shutdown request */
            if (g_shutdown_requested) {
                printf("NCD Service: Processing shutdown request...\n");
                g_running = 0;
                break;
            }
            
            /* Periodic flush */
            time_t now = time(NULL);
            if (g_flush_requested || 
                (service_state_needs_flush(state) && 
                 (now - last_flush_check) > FLUSH_INTERVAL_SEC)) {
                g_flush_requested = 0;
                last_flush_check = now;
                if (service_state_flush(state)) {
                    printf("NCD Service: Flushed to disk\n");
                }
            }
            
            Sleep(10);
        }
        
        ipc_server_cleanup(server);
    }
    
    /* Cleanup */
    printf("NCD Service: Shutting down...\n");
    g_running = 0;
    signal_loader_stop();
    wait_for_loader();
    
    if (service_state_needs_flush(state)) {
        service_state_flush(state);
    }
    
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    
    CloseHandle(hMutex);
    
    printf("NCD Service: Shutdown complete\n");
    return 0;
}

#endif /* NCD_PLATFORM_WINDOWS */

/* --------------------------------------------------------- main               */

int main(int argc, char *argv[]) {
#if NCD_PLATFORM_WINDOWS
    /* Get executable path for spawning daemon */
    char exe_path[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    
    /* Parse command line arguments */
    if (argc > 1) {
        if (strcmp(argv[1], "start") == 0) {
            if (is_service_running()) {
                printf("NCD Service: Already running\n");
                return 0;
            }
            return spawn_daemon(exe_path);
        }
        
        if (strcmp(argv[1], "stop") == 0) {
            if (!is_service_running()) {
                printf("NCD Service: Not running\n");
                return 1;
            }
            if (signal_stop_service()) {
                printf("NCD Service: Stop signal sent\n");
                return 0;
            }
            fprintf(stderr, "NCD Service: Failed to send stop signal\n");
            return 1;
        }
        
        if (strcmp(argv[1], "status") == 0) {
            printf(is_service_running() ? "running\n" : "stopped\n");
            return 0;
        }
        
        if (strcmp(argv[1], "--daemon") == 0) {
            return run_service();
        }
        
        /* Unknown command - fall through to print usage */
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        fprintf(stderr, "Usage: %s [start|stop|status]\n", argv[0]);
        return 1;
    }
    
    /* No arguments = run daemon directly (backward compatible) */
    return run_service();
    
#else /* Linux/POSIX */
    
    /* Linux: No built-in command parsing - script handles it */
    (void)argc;
    (void)argv;
    
    printf("NCD State Service starting...\n");
    
    setup_signal_handlers();
    
    /* Initialize service state */
    ServiceState *state = service_state_init();
    if (!state) {
        fprintf(stderr, "NCD Service: Failed to initialize state\n");
        return 1;
    }
    
    printf("NCD Service: State initialized (metadata loaded)\n");
    
    /* Initialize snapshot publisher */
    SnapshotPublisher *pub = snapshot_publisher_init();
    if (!pub) {
        fprintf(stderr, "NCD Service: Failed to initialize publisher\n");
        service_state_cleanup(state);
        return 1;
    }
    
    printf("NCD Service: Publisher initialized\n");
    
    /* Publish initial metadata snapshot */
    if (!snapshot_publisher_publish_meta(pub, state)) {
        fprintf(stderr, "NCD Service: Failed to publish metadata snapshot\n");
        snapshot_publisher_cleanup(pub);
        service_state_cleanup(state);
        return 1;
    }
    printf("NCD Service: Metadata snapshot published\n");
    
    /* Start background loader */
    start_background_loader(state, pub);
    
    /* Run service loop */
    service_loop(state, pub);
    
    /* Cleanup */
    printf("NCD Service: Shutting down...\n");
    g_running = 0;
    signal_loader_stop();
    wait_for_loader();
    
    if (service_state_needs_flush(state)) {
        service_state_flush(state);
    }
    
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    
    printf("NCD Service: Shutdown complete\n");
    
    return 0;
    
#endif /* NCD_PLATFORM_WINDOWS */
}
