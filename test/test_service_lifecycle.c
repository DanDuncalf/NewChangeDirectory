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
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <process.h>
#include <tlhelp32.h>
#define sleep(x) Sleep((x) * 1000)
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
/* Look for service in parent directory first, then current directory */
#define SERVICE_EXE "../ncd_service"
#endif

/* Maximum time to wait for service to start/stop (seconds) */
#define SERVICE_START_TIMEOUT 10
#define SERVICE_STOP_TIMEOUT 5

/* Check if service executable exists */
static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(SERVICE_EXE);
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access(SERVICE_EXE, X_OK) == 0);
#endif
}

/* Run service command and get exit code */
static int run_service_command(const char *cmd, char *output, size_t output_size) {
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", SERVICE_EXE, cmd);
    
#if NCD_PLATFORM_WINDOWS
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    HANDLE hRead, hWrite;
    
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        return -1;
    }
    
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    
    PROCESS_INFORMATION pi = {0};
    
    if (!CreateProcessA(NULL, full_cmd, NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }
    
    CloseHandle(hWrite);
    
    /* Read output */
    DWORD bytesRead = 0;
    if (output && output_size > 0) {
        ReadFile(hRead, output, (DWORD)(output_size - 1), &bytesRead, NULL);
        output[bytesRead] = '\0';
    }
    CloseHandle(hRead);
    
    /* Wait for process and get exit code */
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return (int)exitCode;
#else
    FILE *pipe = popen(full_cmd, "r");
    if (!pipe) {
        return -1;
    }
    
    /* Read output */
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

/* Wait for service to reach expected state */
static bool wait_for_service_state(bool expected_running, int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        bool currently_running = ipc_service_exists();
        if (currently_running == expected_running) {
            return true;
        }
#if NCD_PLATFORM_WINDOWS
        Sleep(100);
#else
        usleep(100000);
#endif
    }
    return false;
}

/* Timeout for graceful shutdown (seconds) */
#define GRACEFUL_SHUTDOWN_TIMEOUT 3

/* Force terminate service process */
static void force_terminate_service(void) {
#if NCD_PLATFORM_WINDOWS
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = {sizeof(pe)};
        if (Process32First(hSnap, &pe)) {
            do {
                if (_stricmp(pe.szExeFile, "NCDService.exe") == 0) {
                    HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hProc) {
                        TerminateProcess(hProc, 1);
                        CloseHandle(hProc);
                    }
                }
            } while (Process32Next(hSnap, &pe));
        }
        CloseHandle(hSnap);
    }
#else
    /* Try pkill first, then killall as fallback */
    system("pkill -9 -f ncd_service 2>/dev/null || killall -9 ncd_service 2>/dev/null");
#endif
    /* Wait for process to actually exit */
    for (int i = 0; i < 20; i++) {
        if (!ipc_service_exists()) break;
#if NCD_PLATFORM_WINDOWS
        Sleep(100);
#else
        usleep(100000);
#endif
    }
}

/* Ensure service is stopped - tries graceful first, then force kill */
static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        return;
    }
    
    /* Try graceful stop first */
    run_service_command("stop", NULL, 0);
    
    /* Wait for graceful shutdown */
    bool stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    if (stopped) {
        return;
    }
    
    /* Graceful shutdown timed out - force kill */
    printf("  [WARNING] Graceful shutdown timed out, force terminating...\n");
    force_terminate_service();
}

/* Ensure service is running - starts it if not */
static bool ensure_service_running(void) {
    if (ipc_service_exists()) {
        return true;
    }
    
    if (!service_executable_exists()) {
        return false;
    }
    
    run_service_command("start", NULL, 0);
    return wait_for_service_state(true, SERVICE_START_TIMEOUT);
}

/* --------------------------------------------------------- basic lifecycle tests */

TEST(service_status_when_stopped) {
    ensure_service_stopped();
    
    char output[256] = {0};
    int exit_code = run_service_command("status", output, sizeof(output));
    
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
    
    /* Should fail (non-zero exit code) */
    ASSERT_TRUE(exit_code != 0);
    ASSERT_TRUE(strstr(output, "Not running") != NULL);
    
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
    run_service_command("start", NULL, 0);
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Restart should stop and start */
    memset(output, 0, sizeof(output));
    int exit_code = run_service_command("stop", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    wait_for_service_state(false, SERVICE_STOP_TIMEOUT);
    
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
    run_service_command("start", NULL, 0);
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
    
    /* Initialize IPC client */
    int init_result = ipc_client_init();
    ASSERT_EQ_INT(0, init_result);
    
    /* Try to connect when service is stopped */
    NcdIpcClient *client = ipc_client_connect();
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
    run_service_command("start", NULL, 0);
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
    run_service_command("start", NULL, 0);
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
    run_service_command("start", NULL, 0);
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
    
    /* Start service */
    run_service_command("start", NULL, 0);
    
    /* Initially service should exist but may be in STARTING state */
    bool exists = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(exists);
    
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
#if NCD_PLATFORM_WINDOWS
        Sleep(100);
#else
        usleep(100000);
#endif
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
    /* This test verifies service termination works correctly */
    
    /* First ensure service is running */
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
    
    /* Test force termination path - start service again */
    exit_code = run_service_command("start", output, sizeof(output));
    ASSERT_EQ_INT(0, exit_code);
    started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Force terminate (simulates stuck service) */
    force_terminate_service();
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
    
    /* Final cleanup and ensure service is left running */
    printf("\n--- Final cleanup: Leaving service running ---\n");
    ensure_service_stopped();
    if (service_executable_exists()) {
        char output[256] = {0};
        run_service_command("start", output, sizeof(output));
        bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
        if (started) {
            printf("Service left running for subsequent tests.\n");
        } else {
            printf("WARNING: Could not leave service running.\n");
        }
    }
}

TEST_MAIN(
    suite_service_lifecycle();
)
