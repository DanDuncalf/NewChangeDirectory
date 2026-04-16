/*
 * test_service_ipc.c  --  Tier 5: Service IPC integration tests
 *
 * Tests the full client-service IPC communication:
 * - Service starts and responds to ping
 * - Service provides database via shared memory
 * - Service accepts heuristic update
 * - Service accepts metadata update
 * - Service handles rescan request
 * - Service handles flush request
 * - Service version check compatibility
 * - Service graceful shutdown
 * - Client falls back to local on service down
 * - Snapshot publisher produces valid snapshots
 */

#include "test_framework.h"
#include "../src/control_ipc.h"
#include "../src/state_backend.h"
#include "../src/service_state.h"
#include "../src/service_publish.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------- test utilities     */

/* Maximum time to wait for service operations */
#define SERVICE_TIMEOUT_MS 5000

/* Check if service executable exists */
static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA("NCDService.exe");
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access("ncd_service", X_OK) == 0);
#endif
}

/* Start service process */
static bool start_service(void) {
    if (ipc_service_exists()) {
        return true; /* Already running */
    }
    
#if NCD_PLATFORM_WINDOWS
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA("NCDService.exe", NULL, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
#else
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        /* Child */
        execl("./ncd_service", "ncd_service", (char *)NULL);
        exit(1);
    }
#endif
    
    /* Wait for service to be ready */
    for (int i = 0; i < 50; i++) {
        if (ipc_service_exists()) {
            return true;
        }
        platform_sleep_ms(100);
    }
    return false;
}

/* Stop service */
static bool stop_service(void) {
    if (!ipc_service_exists()) {
        return true; /* Already stopped */
    }
    
    NcdIpcClient *client = ipc_client_connect();
    if (client) {
        ipc_client_request_shutdown(client);
        ipc_client_disconnect(client);
    }
    
    /* Wait for service to stop */
    for (int i = 0; i < 50; i++) {
        if (!ipc_service_exists()) {
            return true;
        }
        platform_sleep_ms(100);
    }
    return false;
}

/* Ensure service is stopped before/after tests */
static void ensure_service_stopped(void) {
    stop_service();
}

static bool metadata_has_group_path(const NcdMetadata *meta,
                                    const char *group_name,
                                    const char *group_path) {
    if (!meta || !group_name || !group_path) {
        return false;
    }
    for (int i = 0; i < meta->groups.count; i++) {
        const NcdGroupEntry *entry = &meta->groups.groups[i];
        if (strcmp(entry->name, group_name) == 0 &&
            strcmp(entry->path, group_path) == 0) {
            return true;
        }
    }
    return false;
}

/* --------------------------------------------------------- Tier 5 Tests       */

/* Test 1: Service starts and responds to ping */
TEST(service_starts_and_responds_to_ping) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    
    /* Start service */
    ASSERT_TRUE(start_service());
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Ping should succeed */
    NcdIpcResult result = ipc_client_ping(client);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 2: Service provides database via shared memory */
TEST(service_provides_database_via_shared_memory) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    /* Connect and get state info */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcStateInfo info;
    NcdIpcResult result = ipc_client_get_state_info(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    /* Verify shared memory names are provided */
    ASSERT_TRUE(strlen(info.meta_name) > 0);
    ASSERT_TRUE(strlen(info.db_name) > 0);
    
    /* Generations should be non-zero when data is available */
    ASSERT_TRUE(info.meta_generation > 0);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 3: Service accepts heuristic update */
TEST(service_accepts_heuristic_update) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Submit a heuristic update */
    NcdIpcResult result = ipc_client_submit_heuristic(client, "downloads", "/home/user/Downloads");
    
    /* Should succeed (or be queued if service is still loading) */
    ASSERT_TRUE(result == NCD_IPC_OK || 
                result == NCD_IPC_ERROR_BUSY_LOADING ||
                result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 4: Service accepts metadata update */
TEST(service_accepts_metadata_update) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Submit a metadata update (add group) */
    const char *group_data = "@testgroup\n/test/path";
    NcdIpcResult result = ipc_client_submit_metadata(client, NCD_META_UPDATE_GROUP_ADD,
                                                      group_data, strlen(group_data));
    
    /* Should succeed or be busy */
    ASSERT_TRUE(result == NCD_IPC_OK || 
                result == NCD_IPC_ERROR_BUSY_LOADING ||
                result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 5: Service handles rescan request */
TEST(service_handles_rescan_request) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request rescan (empty mask = all drives) */
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    
    /* Should be accepted (will be queued if busy) */
    ASSERT_TRUE(result == NCD_IPC_OK || 
                result == NCD_IPC_ERROR_BUSY_LOADING ||
                result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 6: Service handles flush request */
TEST(service_handles_flush_request) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request flush */
    NcdIpcResult result = ipc_client_request_flush(client);
    
    /* Should succeed or indicate busy state */
    ASSERT_TRUE(result == NCD_IPC_OK || 
                result == NCD_IPC_ERROR_BUSY_LOADING ||
                result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 7: Service version check compatibility */
TEST(service_version_check_compatibility) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get version info */
    NcdIpcVersionInfo info;
    NcdIpcResult result = ipc_client_get_version(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    /* Verify version info is populated */
    ASSERT_TRUE(strlen(info.app_version) > 0);
    ASSERT_TRUE(info.protocol_version > 0);
    
    /* Check version compatibility */
    NcdIpcVersionCheckResult check_result;
    result = ipc_client_check_version(client, NCD_APP_VERSION, __DATE__ " " __TIME__, 
                                       &check_result);
    
    /* Should succeed or indicate mismatch */
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_GENERIC);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 8: Service graceful shutdown */
TEST(service_graceful_shutdown) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    /* Verify service is running */
    ASSERT_TRUE(ipc_service_exists());
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request shutdown */
    NcdIpcResult result = ipc_client_request_shutdown(client);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    ipc_client_disconnect(client);
    
    /* Wait for service to stop */
    bool stopped = false;
    for (int i = 0; i < 50; i++) {
        if (!ipc_service_exists()) {
            stopped = true;
            break;
        }
        platform_sleep_ms(100);
    }
    
    ASSERT_TRUE(stopped);
    return 0;
}

/* Test 9: Client falls back to local on service down */
TEST(client_falls_back_to_local_on_service_down) {
    ensure_service_stopped();
    
    /* Verify service is not running */
    ASSERT_FALSE(ipc_service_exists());
    
    /* Try to open state via best effort - should fallback to local */
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_best_effort(&view, &info);
    
    /* Should succeed via local fallback */
    ASSERT_EQ_INT(0, result);
    ASSERT_NOT_NULL(view);
    ASSERT_FALSE(info.from_service); /* Should be from local disk */
    
    state_backend_close(view);
    return 0;
}

/* Test 10: Snapshot publisher produces valid snapshots */
TEST(snapshot_publisher_produces_valid_snapshots) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    /* Give service time to publish initial snapshots */
    platform_sleep_ms(1000);
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get state info to find shared memory names */
    NcdIpcStateInfo info;
    NcdIpcResult result = ipc_client_get_state_info(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    /* Verify snapshot sizes are reasonable (non-zero) */
    ASSERT_TRUE(info.meta_size > 0);
    ASSERT_TRUE(info.meta_size < 100 * 1024 * 1024); /* Less than 100MB */
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* Test 11: IPC error strings are meaningful */
TEST(ipc_error_strings_meaningful) {
    const char *str;
    
    str = ipc_error_string(NCD_IPC_OK);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_GENERIC);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strlen(str) > 0);
    
    str = ipc_error_string(NCD_IPC_ERROR_BUSY);
    ASSERT_NOT_NULL(str);
    ASSERT_TRUE(strstr(str, "busy") != NULL || strstr(str, "Busy") != NULL);
    
    str = ipc_error_string(NCD_IPC_ERROR_NOT_FOUND);
    ASSERT_NOT_NULL(str);
    
    return 0;
}

/* Test 12: State backend connects to service when available */
TEST(state_backend_connects_to_service_when_available) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    /* Give service time to initialize */
    platform_sleep_ms(1000);
    
    /* Open state via best effort - should connect to service */
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    
    int result = state_backend_open_best_effort(&view, &info);
    ASSERT_EQ_INT(0, result);
    ASSERT_NOT_NULL(view);
    ASSERT_TRUE(info.from_service);
    ASSERT_TRUE(info.generation > 0);
    
    const NcdMetadata *meta = state_view_metadata(view);
    const NcdDatabase *db = state_view_database(view);
    ASSERT_NOT_NULL(meta);
    ASSERT_NOT_NULL(db);
    
    state_backend_close(view);
    
    ensure_service_stopped();
    return 0;
}

/* Test 13: Metadata updates through state_backend in service mode persist */
TEST(state_backend_group_update_roundtrip_when_service_running) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    ensure_service_stopped();
    ASSERT_TRUE(start_service());
    
    platform_sleep_ms(1000);
    
    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    int result = state_backend_open_best_effort(&view, &info);
    ASSERT_EQ_INT(0, result);
    ASSERT_NOT_NULL(view);
    ASSERT_TRUE(info.from_service);
    
    const char *group_name = "svc_roundtrip_group";
#if NCD_PLATFORM_WINDOWS
    const char *group_path = "C:\\NCD_Service_Roundtrip_Path";
#else
    const char *group_path = "/tmp/ncd_service_roundtrip_path";
#endif
    
    const char *args[2] = { group_name, group_path };
    result = state_backend_submit_metadata_update(view,
                                                  NCD_META_UPDATE_GROUP_ADD,
                                                  args,
                                                  sizeof(args));
    ASSERT_EQ_INT(0, result);
    state_backend_close(view);
    
    view = NULL;
    result = state_backend_open_best_effort(&view, &info);
    ASSERT_EQ_INT(0, result);
    ASSERT_NOT_NULL(view);
    ASSERT_TRUE(info.from_service);
    
    const NcdMetadata *meta = state_view_metadata(view);
    ASSERT_NOT_NULL(meta);
    ASSERT_TRUE(metadata_has_group_path(meta, group_name, group_path));
    
    result = state_backend_submit_metadata_update(view,
                                                  NCD_META_UPDATE_GROUP_REMOVE,
                                                  group_name,
                                                  strlen(group_name) + 1);
    ASSERT_EQ_INT(0, result);
    
    state_backend_close(view);
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_ipc(void) {
    printf("\n=== Tier 5: Service IPC Integration Tests ===\n\n");
    
    RUN_TEST(service_starts_and_responds_to_ping);
    RUN_TEST(service_provides_database_via_shared_memory);
    RUN_TEST(service_accepts_heuristic_update);
    RUN_TEST(service_accepts_metadata_update);
    RUN_TEST(service_handles_rescan_request);
    RUN_TEST(service_handles_flush_request);
    RUN_TEST(service_version_check_compatibility);
    RUN_TEST(service_graceful_shutdown);
    RUN_TEST(client_falls_back_to_local_on_service_down);
    RUN_TEST(snapshot_publisher_produces_valid_snapshots);
    RUN_TEST(ipc_error_strings_meaningful);
    RUN_TEST(state_backend_connects_to_service_when_available);
    RUN_TEST(state_backend_group_update_roundtrip_when_service_running);
}

TEST_MAIN(
    suite_service_ipc();
)
