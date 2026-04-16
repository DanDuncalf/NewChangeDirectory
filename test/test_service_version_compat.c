/*
 * test_service_version_compat.c  --  Service version compatibility tests
 *
 * Tests:
 * - Version check between client and service
 * - Version mismatch detection
 * - Auto-shutdown of mismatched service
 * - Cross-version compatibility scenarios
 *
 * These tests verify that the NCD client and service handle version
 * mismatches gracefully.
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
#endif

/* --------------------------------------------------------- test utilities     */

/* Service executable name */
#if NCD_PLATFORM_WINDOWS
#define SERVICE_EXE "..\\NCDService.exe"
#else
#define SERVICE_EXE "../ncd_service"
#endif

#define SERVICE_TIMEOUT 10

/* Check if service executable exists */
static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(SERVICE_EXE);
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access(SERVICE_EXE, X_OK) == 0);
#endif
}

/* Run service command */
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

/* Ensure service is stopped */
static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        return;
    }
    
    run_service_command("stop");
    wait_for_service_state(false, 5);
    
    /* Force kill if still running */
#if NCD_PLATFORM_WINDOWS
    if (ipc_service_exists()) {
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
    }
#endif
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

/* --------------------------------------------------------- version check tests */

TEST(version_check_when_service_running) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    /* Start service */
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    /* Connect to service */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Check version */
    NcdIpcVersionCheckResult result;
    NcdIpcResult ipc_result = ipc_client_check_version(client, NCD_APP_VERSION, 
                                                        __DATE__ " " __TIME__, &result);
    
    /* Should succeed and versions should match */
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_result);
    ASSERT_TRUE(result.versions_match);
    ASSERT_FALSE(result.service_was_stopped);
    ASSERT_STR_CONTAINS(result.message, "match");
    
    ipc_client_disconnect(client);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

TEST(version_check_gets_service_version_info) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get version info directly */
    NcdIpcVersionInfo info;
    NcdIpcResult ipc_result = ipc_client_get_version(client, &info);
    
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_result);
    ASSERT_TRUE(strlen(info.app_version) > 0);
    ASSERT_TRUE(strlen(info.build_stamp) > 0);
    ASSERT_EQ_INT(NCD_IPC_VERSION, info.protocol_version);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

TEST(version_check_mismatch_triggers_shutdown) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Use a fake version that doesn't match */
    NcdIpcVersionCheckResult result;
    NcdIpcResult ipc_result = ipc_client_check_version(client, "0.0.0", 
                                                        "Jan 01 2020 00:00:00", &result);
    
    /* Should report mismatch */
    ASSERT_FALSE(result.versions_match);
    /* Service should have been stopped or stop was attempted */
    ASSERT_TRUE(result.service_was_stopped || !ipc_service_exists());
    ASSERT_STR_CONTAINS(result.message, "mismatch");
    
    ipc_client_disconnect(client);
    
    /* Give service time to shut down */
    platform_sleep_ms(500);
    ensure_service_stopped();
    return 0;
}

TEST(version_check_result_populated_correctly) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    const char *test_client_ver = "1.2.3";
    const char *test_client_build = "Test Build 123";
    
    NcdIpcVersionCheckResult result;
    memset(&result, 0, sizeof(result));
    
    ipc_client_check_version(client, test_client_ver, test_client_build, &result);
    
    /* Result should contain client version info */
    ASSERT_EQ_STR(test_client_ver, result.client_version);
    ASSERT_EQ_STR(test_client_build, result.client_build);
    
    /* Result should contain service version info */
    ASSERT_TRUE(strlen(result.service_version) > 0);
    ASSERT_TRUE(strlen(result.service_build) > 0);
    
    /* Message should be populated */
    ASSERT_TRUE(strlen(result.message) > 0);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

TEST(version_check_invalid_params_fails) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult result;
    
    /* NULL client_version should fail */
    NcdIpcResult ipc_result = ipc_client_check_version(client, NULL, "build", &result);
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, ipc_result);
    
    /* NULL client_build should fail */
    ipc_result = ipc_client_check_version(client, "version", NULL, &result);
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, ipc_result);
    
    /* NULL result should fail */
    ipc_result = ipc_client_check_version(client, "version", "build", NULL);
    ASSERT_EQ_INT(NCD_IPC_ERROR_INVALID, ipc_result);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

TEST(protocol_version_matches_expected) {
    ensure_service_stopped();
    
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }
    
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionInfo info;
    NcdIpcResult ipc_result = ipc_client_get_version(client, &info);
    
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_result);
    /* Protocol version should be 3 (current version) */
    ASSERT_EQ_INT(3, info.protocol_version);
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_version_compat(void) {
    RUN_TEST(version_check_when_service_running);
    RUN_TEST(version_check_gets_service_version_info);
    RUN_TEST(version_check_mismatch_triggers_shutdown);
    RUN_TEST(version_check_result_populated_correctly);
    RUN_TEST(version_check_invalid_params_fails);
    RUN_TEST(protocol_version_matches_expected);
}

TEST_MAIN(
    suite_version_compat();
)
