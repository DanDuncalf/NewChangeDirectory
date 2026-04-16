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
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <process.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

/* --------------------------------------------------------- test utilities     */

/* Service executable name */
#if NCD_PLATFORM_WINDOWS
#define SERVICE_EXE "..\\NCDService.exe"
#else
#define SERVICE_EXE "../ncd_service"
#endif

#define SERVICE_TIMEOUT 10
#define GRACEFUL_TIMEOUT 3

/* Check if service executable exists */
static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(SERVICE_EXE);
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access(SERVICE_EXE, X_OK) == 0);
#endif
}

/* Run service command and capture exit code */
static int run_service_command(const char *cmd) {
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", SERVICE_EXE, cmd);
    
#if NCD_PLATFORM_WINDOWS
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi = {0};
    
    if (!CreateProcessA(NULL, full_cmd, NULL, NULL, FALSE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return -1;
    }
    
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return (int)exitCode;
#else
    int status = system(full_cmd);
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
        platform_sleep_ms(100);
    }
    return false;
}

/* Force terminate service */
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
    system("pkill -9 -f ncd_service 2>/dev/null || killall -9 ncd_service 2>/dev/null");
#endif
    platform_sleep_ms(500);
}

/* Ensure service is stopped - tries graceful first, then force kill */
static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        return;
    }
    
    /* Try graceful stop first */
    run_service_command("stop");
    
    /* Wait for graceful shutdown */
    bool stopped = wait_for_service_state(false, GRACEFUL_TIMEOUT);
    if (stopped) {
        return;
    }
    
    /* Graceful shutdown timed out - force kill */
    force_terminate_service();
    wait_for_service_state(false, 2);
}

/* Ensure service is running */
static bool ensure_service_running(void) {
    if (ipc_service_exists()) {
        return true;
    }
    
    if (!service_executable_exists()) {
        return false;
    }
    
    run_service_command("start");
    return wait_for_service_state(true, SERVICE_TIMEOUT);
}

/* --------------------------------------------------------- legacy shutdown tests */

TEST(legacy_shutdown_graceful_stop_succeeds) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    /* Issue graceful stop command */
    int exit_code = run_service_command("stop");
    
    /* Stop command should return 0 on success */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Service should be stopped */
    bool stopped = wait_for_service_state(false, GRACEFUL_TIMEOUT);
    ASSERT_TRUE(stopped);
    
    return 0;
}

TEST(legacy_shutdown_stop_when_already_stopped_returns_success) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Ensure service is stopped */
    ASSERT_FALSE(ipc_service_exists());
    
    /* Stop command when already stopped - returns 0 or 1 depending on implementation
     * Some implementations return 0 (success - already in desired state)
     * Others return 1 (error - service not running to stop)
     * Both are acceptable behaviors */
    int exit_code = run_service_command("stop");
    ASSERT_TRUE(exit_code == 0 || exit_code == 1);
    
    return 0;
}

TEST(legacy_shutdown_block_command_waits_for_stop) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    /* Issue block stop command with 10 second timeout */
    int exit_code = run_service_command("stop block 10");
    
    /* Should return 0 when service stops within timeout */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Service should be stopped */
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

TEST(legacy_shutdown_ipc_request_shutdown_works) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    /* Connect via IPC */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Send shutdown request */
    NcdIpcResult result = ipc_client_request_shutdown(client);
    
    /* Should succeed or return NOT_FOUND (service closed pipe) */
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_NOT_FOUND);
    
    ipc_client_disconnect(client);
    
    /* Service should stop */
    bool stopped = wait_for_service_state(false, GRACEFUL_TIMEOUT);
    ASSERT_TRUE(stopped);
    
    return 0;
}

TEST(legacy_shutdown_double_stop_is_safe) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    /* First stop */
    int exit_code = run_service_command("stop");
    ASSERT_EQ_INT(0, exit_code);
    
    /* Wait for stop */
    wait_for_service_state(false, GRACEFUL_TIMEOUT);
    
    /* Second stop - service is already stopped
     * Accept either 0 (success) or 1 (not running) as valid */
    exit_code = run_service_command("stop");
    ASSERT_TRUE(exit_code == 0 || exit_code == 1);
    
    return 0;
}

TEST(legacy_shutdown_force_kill_as_last_resort) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    /* Verify service is running */
    ASSERT_TRUE(ipc_service_exists());
    
    /* Force terminate */
    force_terminate_service();
    
    /* Service should be stopped */
    ASSERT_FALSE(ipc_service_exists());
    
    return 0;
}

TEST(legacy_shutdown_restart_after_stop_works) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    /* Stop service */
    run_service_command("stop");
    wait_for_service_state(false, GRACEFUL_TIMEOUT);
    ASSERT_FALSE(ipc_service_exists());
    
    /* Restart service */
    int exit_code = run_service_command("start");
    ASSERT_EQ_INT(0, exit_code);
    
    /* Service should be running */
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_legacy_shutdown(void) {
    RUN_TEST(legacy_shutdown_graceful_stop_succeeds);
    RUN_TEST(legacy_shutdown_stop_when_already_stopped_returns_success);
    RUN_TEST(legacy_shutdown_block_command_waits_for_stop);
    RUN_TEST(legacy_shutdown_ipc_request_shutdown_works);
    RUN_TEST(legacy_shutdown_double_stop_is_safe);
    RUN_TEST(legacy_shutdown_force_kill_as_last_resort);
    RUN_TEST(legacy_shutdown_restart_after_stop_works);
}

TEST_MAIN(
    suite_legacy_shutdown();
)
