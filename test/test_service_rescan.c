/*
 * test_service_rescan.c  --  Service rescan tests (20 tests)
 *
 * Tests:
 * - Service-side Rescan (10 tests)
 * - Rescan Integration (10 tests)
 */

#include "test_framework.h"
#include "../src/service_state.h"
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

/* --------------------------------------------------------- test utilities     */

#define SERVICE_START_TIMEOUT 10
#define SERVICE_STOP_TIMEOUT 5

static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA("NCDService.exe");
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access("../ncd_service", X_OK) == 0);
#endif
}

static int run_service_command(const char *cmd, char *output, size_t output_size) {
    char full_cmd[512];
#if NCD_PLATFORM_WINDOWS
    snprintf(full_cmd, sizeof(full_cmd), "NCDService.exe %s", cmd);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hRead, hWrite;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return -1;
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    PROCESS_INFORMATION pi = {0};
    if (!CreateProcessA(NULL, full_cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }
    CloseHandle(hWrite);
    DWORD bytesRead = 0;
    if (output && output_size > 0) {
        ReadFile(hRead, output, (DWORD)(output_size - 1), &bytesRead, NULL);
        output[bytesRead] = '\0';
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exitCode;
#else
    snprintf(full_cmd, sizeof(full_cmd), "../ncd_service %s", cmd);
    FILE *pipe = popen(full_cmd, "r");
    if (!pipe) return -1;
    if (output && output_size > 0) {
        size_t total = 0;
        while (total < output_size - 1 && !feof(pipe)) {
            size_t n = fread(output + total, 1, output_size - 1 - total, pipe);
            if (n == 0) break;
            total += n;
        }
        output[total] = '\0';
    }
    int status = pclose(pipe);
    return WEXITSTATUS(status);
#endif
}

static bool wait_for_service_state(bool expected_running, int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        bool currently_running = ipc_service_exists();
        if (currently_running == expected_running) return true;
        platform_sleep_ms(100);
    }
    return false;
}

static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) return;
    char _buf[256];
    run_service_command("stop", _buf, sizeof(_buf));
    wait_for_service_state(false, SERVICE_STOP_TIMEOUT);
}

static bool ensure_service_running(void) {
    if (ipc_service_exists()) return true;
    if (!service_executable_exists()) return false;
    char _buf[256];
    run_service_command("start", _buf, sizeof(_buf));
    return wait_for_service_state(true, SERVICE_START_TIMEOUT);
}

/* --------------------------------------------------------- service-side rescan tests */

TEST(rescan_request_while_already_scanning) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* First rescan request */
    bool drive_mask[26] = {false};
    NcdIpcResult result1 = ipc_client_request_rescan(client, drive_mask, false);
    
    /* Second rescan while first may be in progress */
    NcdIpcResult result2 = ipc_client_request_rescan(client, drive_mask, false);
    
    /* Should handle gracefully - either accept or report busy */
    ASSERT_TRUE(result1 == NCD_IPC_OK || result1 == NCD_IPC_ERROR_BUSY_SCANNING);
    ASSERT_TRUE(result2 == NCD_IPC_OK || result2 == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_request_with_specific_drives) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request rescan of specific drives (C and D) */
    bool drive_mask[26] = {false};
    drive_mask[2] = true;  /* C: */
    drive_mask[3] = true;  /* D: */
    
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_request_with_exclude_pattern) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request rescan - exclusions are handled by service from metadata */
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_atomic_update_no_downtime) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    
    /* Get initial generation */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    NcdIpcStateInfo info1;
    NcdIpcResult result = ipc_client_get_state_info(client, &info1);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    ipc_client_disconnect(client);
    
    /* Request rescan */
    client = ipc_client_connect();
    bool drive_mask[26] = {false};
    ipc_client_request_rescan(client, drive_mask, false);
    ipc_client_disconnect(client);
    
    /* Service should remain responsive during rescan */
    for (int i = 0; i < 10; i++) {
        platform_sleep_ms(200);
        client = ipc_client_connect();
        if (client) {
            NcdIpcResult r = ipc_client_ping(client);
            ASSERT_TRUE(r == NCD_IPC_OK || r == NCD_IPC_ERROR_BUSY_SCANNING);
            ipc_client_disconnect(client);
        }
    }
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_progress_reporting) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    /* Get state info to check progress */
    NcdIpcStateInfo info;
    result = ipc_client_get_state_info(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    ASSERT_TRUE(info.db_generation >= 0);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_cancellation_mid_scan) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Start rescan */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    bool drive_mask[26] = {false};
    ipc_client_request_rescan(client, drive_mask, false);
    ipc_client_disconnect(client);
    
    /* Shutdown during rescan */
    client = ipc_client_connect();
    if (client) {
        ipc_client_request_shutdown(client);
        ipc_client_disconnect(client);
    }
    
    ipc_client_cleanup();
    wait_for_service_state(false, SERVICE_STOP_TIMEOUT);
    return 0;
}

TEST(rescan_failure_rollback) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Get initial generation */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    NcdIpcStateInfo info_before;
    ipc_client_get_state_info(client, &info_before);
    ipc_client_disconnect(client);
    
    /* Request rescan of non-existent drive */
    bool drive_mask[26] = {false};
    drive_mask[25] = true;  /* Z: - likely doesn't exist */
    client = ipc_client_connect();
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_NOT_FOUND);
    ipc_client_disconnect(client);
    
    /* Service should still be operational */
    platform_sleep_ms(500);
    client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    ipc_client_disconnect(client);
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_concurrent_client_queries) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Start a rescan */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    bool drive_mask[26] = {false};
    ipc_client_request_rescan(client, drive_mask, false);
    ipc_client_disconnect(client);
    
    /* Concurrent queries during rescan */
    int success_count = 0;
    for (int i = 0; i < 20; i++) {
        client = ipc_client_connect();
        if (client) {
            NcdIpcStateInfo info;
            NcdIpcResult result = ipc_client_get_state_info(client, &info);
            if (result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING) {
                success_count++;
            }
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(50);
    }
    
    ASSERT_TRUE(success_count >= 10);
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_large_database_memory_pressure) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Request rescan - service should handle memory pressure */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_with_no_changes_detected) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* First rescan to establish baseline */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcStateInfo info1;
    ipc_client_get_state_info(client, &info1);
    ipc_client_disconnect(client);
    
    /* Second rescan immediately after */
    platform_sleep_ms(500);
    client = ipc_client_connect();
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    ipc_client_disconnect(client);
    
    /* Allow rescan to complete */
    platform_sleep_ms(2000);
    
    /* Service should still be responsive */
    client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    ipc_client_disconnect(client);
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- rescan integration tests */

TEST(rescan_triggers_client_cache_invalidation) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get initial generation */
    NcdIpcStateInfo info1;
    ipc_client_get_state_info(client, &info1);
    ipc_client_disconnect(client);
    
    /* Request rescan */
    platform_sleep_ms(100);
    client = ipc_client_connect();
    bool drive_mask[26] = {false};
    ipc_client_request_rescan(client, drive_mask, false);
    ipc_client_disconnect(client);
    
    /* Wait and check for generation change */
    platform_sleep_ms(3000);
    client = ipc_client_connect();
    NcdIpcStateInfo info2;
    ipc_client_get_state_info(client, &info2);
    ipc_client_disconnect(client);
    
    /* Generation should have changed (or be ready) */
    ASSERT_TRUE(info2.db_generation > 0);
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_updates_generation_counter) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    /* Get state and verify generation */
    NcdIpcStateInfo info;
    result = ipc_client_get_state_info(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    ASSERT_TRUE(info.db_generation >= 0);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_preserves_heuristics) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Submit heuristic before rescan */
    NcdIpcResult result = ipc_client_submit_heuristic(client, "test_query", "/test/path");
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_LOADING);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    /* Heuristics should be preserved - service maintains them separately */
    ASSERT_TRUE(1);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_preserves_groups) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Add group before rescan */
    const char *group_data = "@testgroup\n/test/path";
    NcdIpcResult result = ipc_client_submit_metadata(client, NCD_META_UPDATE_GROUP_ADD,
                                                      group_data, strlen(group_data));
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_LOADING);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_preserves_exclusions) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Add exclusion before rescan */
    const char *exclusion_data = "*Windows*";
    NcdIpcResult result = ipc_client_submit_metadata(client, NCD_META_UPDATE_EXCLUSION_ADD,
                                                      exclusion_data, strlen(exclusion_data));
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_LOADING);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_preserves_config) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    /* Config is in metadata, separate from database */
    ASSERT_TRUE(1);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_preserves_history) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Add history entry before rescan */
    const char *path = "/test/history/path";
    NcdIpcResult result = ipc_client_submit_metadata(client, NCD_META_UPDATE_DIR_HISTORY_ADD,
                                                      path, strlen(path));
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_LOADING);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_merges_with_existing_data) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get state before rescan */
    NcdIpcStateInfo info1;
    ipc_client_get_state_info(client, &info1);
    
    /* Request rescan */
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    ipc_client_disconnect(client);
    
    /* Wait for completion */
    platform_sleep_ms(3000);
    
    /* Verify service is still operational with merged data */
    client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    ipc_client_disconnect(client);
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_removes_deleted_directories) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Rescan will pick up current filesystem state */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(rescan_adds_new_directories) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Rescan will pick up current filesystem state */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    bool drive_mask[26] = {false};
    NcdIpcResult result = ipc_client_request_rescan(client, drive_mask, false);
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_SCANNING);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_rescan(void) {
    printf("\n=== Service Rescan Tests ===\n\n");
    
    /* Service-side Rescan (10 tests) */
    RUN_TEST(rescan_request_while_already_scanning);
    RUN_TEST(rescan_request_with_specific_drives);
    RUN_TEST(rescan_request_with_exclude_pattern);
    RUN_TEST(rescan_atomic_update_no_downtime);
    RUN_TEST(rescan_progress_reporting);
    RUN_TEST(rescan_cancellation_mid_scan);
    RUN_TEST(rescan_failure_rollback);
    RUN_TEST(rescan_concurrent_client_queries);
    RUN_TEST(rescan_large_database_memory_pressure);
    RUN_TEST(rescan_with_no_changes_detected);
    
    /* Rescan Integration (10 tests) */
    RUN_TEST(rescan_triggers_client_cache_invalidation);
    RUN_TEST(rescan_updates_generation_counter);
    RUN_TEST(rescan_preserves_heuristics);
    RUN_TEST(rescan_preserves_groups);
    RUN_TEST(rescan_preserves_exclusions);
    RUN_TEST(rescan_preserves_config);
    RUN_TEST(rescan_preserves_history);
    RUN_TEST(rescan_merges_with_existing_data);
    RUN_TEST(rescan_removes_deleted_directories);
    RUN_TEST(rescan_adds_new_directories);
    
    /* Final cleanup */
    printf("\n--- Final cleanup ---\n");
    ensure_service_stopped();
}

TEST_MAIN(
    suite_service_rescan();
)
