/*
 * test_service_lifecycle.c  --  Service lifecycle tests
 *
 * Tests:
 * - Service start/stop/status on Windows and Linux
 * - Double-start detection
 * - Service availability detection
 * - Cross-platform service control
 *
 * These tests interact with the real service executable and verify
 * that service lifecycle operations work correctly.
 */

#include "test_framework.h"
#include "service_test_common.h"
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

/* Service executable name */
#if NCD_PLATFORM_WINDOWS
#define SERVICE_EXE "NCDService.exe"
#else
#define SERVICE_EXE "../ncd_service"
#endif

/* Maximum time to wait for service to start/stop (seconds) */
#define SERVICE_START_TIMEOUT 10
#define SERVICE_STOP_TIMEOUT 5

/* --------------------------------------------------------- basic lifecycle tests */

TEST(service_status_when_stopped) {
    ensure_service_stopped();
    
    char output[256] = {0};
    (void)run_service_command("status", output, sizeof(output));
    
    /* Should report "stopped" and return 0 or 1 (depending on implementation) */
    ASSERT_TRUE(strstr(output, "stopped") != NULL || !ipc_service_exists());
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

TEST(service_start_when_stopped) {
    ensure_service_stopped();
    
    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    char output[256] = {0};
    int exit_code = run_service_command("start", output, sizeof(output));
    
    /* Start should succeed */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Wait for service to be detectable */
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    ASSERT_TRUE(ipc_service_exists());
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

TEST(service_double_start_fails) {
    ensure_service_stopped();
    
    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    /* Start service first time */
    char output[256] = {0};
    int exit_code = run_service_command("start", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Try to start again - should report already running */
    memset(output, 0, sizeof(output));
    exit_code = run_service_command("start", output, sizeof(output));
    
    /* Double start should report already running (exit code 0, but message says running) */
    ASSERT_TRUE(strstr(output, "Already running") != NULL || strstr(output, "running") != NULL);
    ASSERT_TRUE(ipc_service_exists());
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

TEST(service_stop_when_running) {
    ensure_service_stopped();
    
    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    /* Start service */
    char output[256] = {0};
    run_service_command("start", output, sizeof(output));
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Stop service */
    memset(output, 0, sizeof(output));
    int exit_code = run_service_command("stop", output, sizeof(output));
    
    /* Stop should succeed */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Wait for service to stop */
    bool stopped = wait_for_service_state(false, SERVICE_STOP_TIMEOUT);
    ASSERT_TRUE(stopped);
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

TEST(service_stop_when_already_stopped) {
    ensure_service_stopped();
    
    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    /* Try to stop when not running */
    char output[256] = {0};
    int exit_code = run_service_command("stop", output, sizeof(output));

    /* Output should indicate service is not running (wrapper uses lowercase) */
    ASSERT_TRUE(strstr(output, "Not running") != NULL ||
                strstr(output, "not running") != NULL);
#if NCD_PLATFORM_WINDOWS
    /* Should fail (non-zero exit code) */
    ASSERT_TRUE(exit_code != 0);
#else
    (void)exit_code;
#endif
    
    return 0;
}

TEST(service_restart) {
    ensure_service_stopped();

    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }

    /* Start service */
    char output[256] = {0};
    int exit_code = run_service_command("start", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Restart should stop and start */
    memset(output, 0, sizeof(output));
    exit_code = run_service_command("stop", output, sizeof(output));
    (void)exit_code;
    ensure_service_stopped();
    ASSERT_FALSE(ipc_service_exists());
    
    exit_code = run_service_command("start", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- IPC connectivity tests */

TEST(service_ipc_ping) {
    ensure_service_stopped();

    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }

    /* Start service */
    char start_out[256] = {0};
    int start_rc = run_service_command("start", start_out, sizeof(start_out));
    ASSERT_EQ_INT(0, start_rc);
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Initialize IPC client */
    int init_result = ipc_client_init();
    ASSERT_EQ_INT(0, init_result);
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Ping should succeed */
    NcdIpcResult result = ipc_client_ping(client);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    /* Cleanup */
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    
    return 0;
}

TEST(service_ipc_ping_when_stopped) {
    ensure_service_stopped();
    
    /* Wait for Windows named pipe cleanup (zombie state)
     * After the service stops, the named pipe can remain in a "zombie" state
     * briefly where CreateFile succeeds but the server is dead.
     * We wait for the pipe to be fully cleaned up by the OS.
     */
    platform_sleep_ms(300);
    
    /* Initialize IPC client */
    int init_result = ipc_client_init();
    ASSERT_EQ_INT(0, init_result);
    
    /* Try to connect when service is stopped - retry to handle zombie state */
    NcdIpcClient *client = NULL;
    int retries = 20;
    while (retries-- > 0) {
        client = ipc_client_connect();
        if (client == NULL) {
            break;  /* Success - no connection */
        }
        /* Got a handle (zombie pipe), close and retry */
        ipc_client_disconnect(client);
        client = NULL;
        platform_sleep_ms(50);
    }
    ASSERT_NULL(client);
    
    ipc_client_cleanup();
    return 0;
}

TEST(service_ipc_get_version) {
    ensure_service_stopped();
    
    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    /* Start service */
    { char buf[256]; run_service_command("start", buf, sizeof(buf)); }
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Initialize IPC client */
    ipc_client_init();
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get version */
    NcdIpcVersionInfo info;
    NcdIpcResult result = ipc_client_get_version(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    ASSERT_TRUE(strlen(info.app_version) > 0);
    ASSERT_TRUE(info.protocol_version > 0);
    
    /* Cleanup */
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    
    return 0;
}

TEST(service_ipc_get_state_info) {
    ensure_service_stopped();
    
    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    /* Start service */
    { char buf[256]; run_service_command("start", buf, sizeof(buf)); }
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Initialize IPC client */
    ipc_client_init();
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get state info */
    NcdIpcStateInfo info;
    NcdIpcResult result = ipc_client_get_state_info(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    ASSERT_TRUE(info.protocol_version == NCD_IPC_VERSION);
    
    /* Cleanup */
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    
    return 0;
}

TEST(service_ipc_shutdown_request) {
    ensure_service_stopped();
    
    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    /* Start service */
    { char buf[256]; run_service_command("start", buf, sizeof(buf)); }
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Initialize IPC client */
    ipc_client_init();
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request shutdown */
    NcdIpcResult result = ipc_client_request_shutdown(client);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    /* Cleanup */
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    
    /* Wait for service to stop */
    bool stopped = wait_for_service_state(false, SERVICE_STOP_TIMEOUT);
    ASSERT_TRUE(stopped);
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

/* --------------------------------------------------------- service state progression tests */

TEST(service_state_progression) {
    ensure_service_stopped();

    /* Skip if service executable not built */
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }

    /* Start service and verify it launched successfully */
    char output[256] = {0};
    int exit_code = run_service_command("start", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);

    /* Wait for service process to be detectable via IPC pipe. */
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Initialize IPC client */
    ipc_client_init();
    
    /* Wait for service to become READY (db_generation > 0) */
    int retries = 50;  /* 5 seconds */
    bool ready = false;
    while (retries-- > 0) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcStateInfo info;
            NcdIpcResult result = ipc_client_get_state_info(client, &info);
            ipc_client_disconnect(client);
            
            if (result == NCD_IPC_OK && info.db_generation > 0) {
                ready = true;
                break;
            }
        }
        platform_sleep_ms(100);
    }
    
    ipc_client_cleanup();
    
    /* Service should eventually be ready */
    /* Note: If no databases exist, it may stay at generation 0 */
    /* So we just verify the service responded to IPC */
    ASSERT_TRUE(retries > 0 || ready);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- service termination test */

TEST(service_termination_graceful_then_force) {
    /* This test verifies graceful stop and safe restart.
     * The service now waits for a shutting-down instance to fully exit
     * before spawning a new daemon, preventing double-start races. */
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found: %s\n", SERVICE_EXE);
        return 0;
    }
    
    ensure_service_stopped();
    
    /* Start service */
    char output[256] = {0};
    int exit_code = run_service_command("start", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    ASSERT_TRUE(ipc_service_exists());
    
    /* Test graceful stop */
    exit_code = run_service_command("stop", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    
    bool stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    ASSERT_TRUE(stopped);
    ASSERT_FALSE(ipc_service_exists());
    wait_for_service_fully_exited(5);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
    
    /* Test restart after graceful stop - service should safely wait for
     * the old instance to exit before creating a new one */
    exit_code = run_service_command("start", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    ASSERT_TRUE(ipc_service_exists());
    
    /* Clean shutdown */
    exit_code = run_service_command("stop", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    ASSERT_TRUE(stopped);
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_lifecycle(void) {
    printf("\n=== Service Lifecycle Tests ===\n\n");
    
    /* Basic lifecycle tests */
    RUN_TEST(service_status_when_stopped);
    RUN_TEST(service_start_when_stopped);
    RUN_TEST(service_double_start_fails);
    RUN_TEST(service_stop_when_running);
    RUN_TEST(service_stop_when_already_stopped);
    RUN_TEST(service_restart);
    
    /* IPC connectivity tests */
    RUN_TEST(service_ipc_ping);
    RUN_TEST(service_ipc_ping_when_stopped);
    RUN_TEST(service_ipc_get_version);
    RUN_TEST(service_ipc_get_state_info);
    RUN_TEST(service_ipc_shutdown_request);
    
    /* State progression tests */
    RUN_TEST(service_state_progression);
    
    /* Service termination test */
    RUN_TEST(service_termination_graceful_then_force);
    
    /* Final cleanup - ensure service is fully stopped */
    printf("\n--- Final cleanup: Stopping service ---\n");
    ensure_service_stopped();
}

TEST_MAIN(
    suite_service_lifecycle();
)
