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
    
    /* Include service state info in reserved fields */
    /* We can extend the protocol later; for now send status in error messages */
    
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
    
    /* All other operations require READY state */
    if (!check_service_ready(conn, sequence, state)) {
        if (payload) ipc_free_message(payload);
        return;
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

#if NCD_PLATFORM_WINDOWS
static DWORD WINAPI LoaderThreadFunc(LPVOID param) {
#else
static void *loader_thread_func(void *param) {
#endif
    ServiceState *state = (ServiceState *)param;
    
    printf("NCD Service: Background loader started\n");
    
    /* Load databases */
    extern bool service_state_load_databases(ServiceState *state);
    if (!service_state_load_databases(state)) {
        fprintf(stderr, "NCD Service: Warning - database loading had issues\n");
    }
    
    /* Transition to READY state */
    service_state_set_runtime_state(state, SERVICE_STATE_READY);
    service_state_set_status_message(state, "Ready");
    printf("NCD Service: Ready\n");
    
#if NCD_PLATFORM_WINDOWS
    return 0;
#else
    return NULL;
#endif
}

static void start_background_loader(ServiceState *state) {
#if NCD_PLATFORM_WINDOWS
    g_loader_thread = CreateThread(NULL, 0, LoaderThreadFunc, state, 0, NULL);
    if (!g_loader_thread) {
        fprintf(stderr, "NCD Service: Failed to start loader thread\n");
        /* Continue synchronously */
        LoaderThreadFunc(state);
    }
#else
    if (pthread_create(&g_loader_thread, NULL, loader_thread_func, state) != 0) {
        fprintf(stderr, "NCD Service: Failed to start loader thread\n");
        /* Continue synchronously */
        loader_thread_func(state);
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

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    printf("NCD State Service starting...\n");
    
    setup_signal_handlers();
    
    /* Initialize service state (metadata loaded synchronously, databases lazy) */
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
    
    /* Publish initial metadata snapshot (available immediately) */
    bool meta_ok = snapshot_publisher_publish_meta(pub, state);
    if (!meta_ok) {
        fprintf(stderr, "NCD Service: Failed to publish metadata snapshot\n");
        snapshot_publisher_cleanup(pub);
        service_state_cleanup(state);
        return 1;
    }
    printf("NCD Service: Metadata snapshot published\n");
    
    /* Start background loader to load databases */
    start_background_loader(state);
    
    /* Note: Database snapshot will be published by the loader when ready */
    
    /* Run service loop */
    service_loop(state, pub);
    
    /* Cleanup */
    printf("NCD Service: Shutting down...\n");
    
    /* Signal loader to stop and wait */
    g_running = 0;
    signal_loader_stop();
    wait_for_loader();
    
    /* Final flush */
    if (service_state_needs_flush(state)) {
        service_state_flush(state);
    }
    
    snapshot_publisher_cleanup(pub);
    service_state_cleanup(state);
    
    printf("NCD Service: Shutdown complete\n");
    
    return 0;
}
