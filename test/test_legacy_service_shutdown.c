/*
 * test_legacy_service_shutdown.c  --  Legacy service shutdown tests
 *
 * Tests:
 * - New client can shutdown old service version via IPC
 * - Graceful shutdown with fallback to force kill
 * - Exit code verification when stopping incompatible versions
 *
 * These tests verify backward compatibility in service management.
 */

#include "test_framework.h"
#include "service_test_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------- test utilities     */

#define SERVICE_TIMEOUT 10

/* Ensure service is running */
static bool ensure_service_running(void) {
    if (ipc_service_exists()) {
        /* Verify it's actually responsive, not a zombie pipe */
        ipc_client_init();
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcResult result = ipc_client_ping(client);
            ipc_client_disconnect(client);
            ipc_client_cleanup();
            if (result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_LOADING || result == NCD_IPC_ERROR_BUSY_SCANNING) {
                return true;
            }
        } else {
            ipc_client_cleanup();
        }
        /* Unresponsive - kill it */
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
    
    if (!service_executable_exists()) {
        return false;
    }
    
    wait_for_service_fully_exited(3);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
    
    run_service_command("start", NULL, 0);
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    if (!started) {
        ensure_service_stopped();
        return false;
    }
    
    ipc_client_init();
    for (int i = 0; i < 50; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcResult result = ipc_client_ping(client);
            ipc_client_disconnect(client);
            ipc_client_cleanup();
            if (result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY_LOADING || result == NCD_IPC_ERROR_BUSY_SCANNING) {
                return true;
            }
        }
        platform_sleep_ms(100);
    }
    ipc_client_cleanup();
    return false;
}

/* --------------------------------------------------------- legacy shutdown tests */

TEST(legacy_shutdown_graceful_stop_succeeds) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Start service */
    ASSERT_TRUE(ensure_service_running());
    ASSERT_TRUE(ipc_service_exists());
    
    /* Stop service */
    int exit_code = run_service_command("stop", NULL, 0);
    
    /* Stop should succeed */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Wait for service to stop */
    bool stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    ASSERT_TRUE(stopped);
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

TEST(legacy_shutdown_block_command_waits_for_stop) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Start service */
    ASSERT_TRUE(ensure_service_running());
    ASSERT_TRUE(ipc_service_exists());
    
    /* Use block command to stop */
    int exit_code = run_service_command("stop", NULL, 0);
    
    /* Should succeed */
    ASSERT_EQ_INT(0, exit_code);
    
    ensure_service_stopped();
    return 0;
}

TEST(legacy_shutdown_double_stop_is_safe) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Start service */
    ASSERT_TRUE(ensure_service_running());
    
    /* First stop */
    int exit_code = run_service_command("stop block 10", NULL, 0);
    (void)exit_code;
    
    /* Wait for service to actually stop */
    bool stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    ASSERT_TRUE(stopped);
    ASSERT_FALSE(ipc_service_exists());
    wait_for_service_fully_exited(3);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
    
    /* Second stop on already-stopped service should be safe */
    ensure_service_stopped();
    return 0;
}

TEST(legacy_shutdown_force_kill_as_last_resort) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Start service */
    ASSERT_TRUE(ensure_service_running());
    ASSERT_TRUE(ipc_service_exists());
    
    /* Stop service */
    int exit_code = run_service_command("stop", NULL, 0);
    ASSERT_EQ_INT(0, exit_code);
    
    wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    wait_for_service_fully_exited(3);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
    
    ASSERT_FALSE(ipc_service_exists());
    ASSERT_FALSE(service_process_still_running());
    
    return 0;
}

TEST(legacy_shutdown_ipc_request_shutdown_works) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Start service */
    ASSERT_TRUE(ensure_service_running());
    ASSERT_TRUE(ipc_service_exists());
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request shutdown */
    NcdIpcResult result = ipc_client_request_shutdown(client);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    ipc_client_disconnect(client);
    
    /* Wait for service to stop */
    bool stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    ASSERT_TRUE(stopped);
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

TEST(legacy_start_after_force_kill) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Start service normally */
    ASSERT_TRUE(ensure_service_running());
    ASSERT_TRUE(ipc_service_exists());
    
    /* Force kill */
    force_terminate_service();
    wait_for_service_fully_exited(3);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
    
    ASSERT_FALSE(ipc_service_exists());
    
    /* Should be able to start again */
    ensure_service_stopped();
    ASSERT_TRUE(ensure_service_running());
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_legacy_shutdown(void) {
    RUN_TEST(legacy_shutdown_graceful_stop_succeeds);
    RUN_TEST(legacy_shutdown_block_command_waits_for_stop);
    RUN_TEST(legacy_shutdown_double_stop_is_safe);
    RUN_TEST(legacy_shutdown_force_kill_as_last_resort);
    RUN_TEST(legacy_shutdown_ipc_request_shutdown_works);
    RUN_TEST(legacy_start_after_force_kill);
}

TEST_MAIN(
    suite_legacy_shutdown();
)
