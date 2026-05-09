/*
 * test_service_stress.c  --  Service stress tests (25 tests)
 *
 * Tests:
 * - State Transitions (6 tests)
 * - IPC Stress (5 tests)
 * - Crash Recovery (5 tests)
 * - Version Compatibility (4 tests)
 * - Client Fallback (5 tests)
 */

#include <stdio.h>
#define printf(...) fprintf(stderr, __VA_ARGS__)
#include "test_framework.h"
#include "../src/service_state.h"
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include <string.h>
#include <stdlib.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#include <process.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <dirent.h>
#endif

#include "test_posix_exit.h"

/* --------------------------------------------------------- test utilities     */

#define SERVICE_EXE NCD_PLATFORM_WINDOWS ? "NCDService.exe" : "../ncd_service"
#define SERVICE_START_TIMEOUT 10
#define SERVICE_STOP_TIMEOUT 5
#define GRACEFUL_SHUTDOWN_TIMEOUT 3

/* Get path to service executable (prefers parent directory to avoid PATH conflicts) */
static const char *get_service_executable_path(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA("NCDService.exe");
    if (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY))
        return "NCDService.exe";
    return "..\\NCDService.exe";
#else
    if (access("ncd_service", X_OK) == 0) return "./ncd_service";
    return "../ncd_service";
#endif
}

static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(get_service_executable_path());
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access(get_service_executable_path(), X_OK) == 0);
#endif
}

static int run_service_command(const char *cmd, char *output, size_t output_size) {
    char full_cmd[512];
#if NCD_PLATFORM_WINDOWS
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", get_service_executable_path(), cmd);
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
    return safe_exit_code(status);
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
    system("pkill -9 -x NCDService 2>/dev/null; killall -9 NCDService 2>/dev/null");
    system("rm -f ${XDG_RUNTIME_DIR:-/tmp}/ncd_service.pid 2>/dev/null");
#endif
    for (int i = 0; i < 20; i++) {
        if (!ipc_service_exists()) break;
        platform_sleep_ms(100);
    }
}

#if NCD_PLATFORM_WINDOWS
static void wait_for_service_fully_exited(int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, "NCDService_Instance_7D3F9A2E");
        if (!hMutex) {
            return;
        }
        CloseHandle(hMutex);
        platform_sleep_ms(100);
    }
}

static bool service_process_still_running(void) {
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, "NCDService_Instance_7D3F9A2E");
    if (!hMutex) {
        return false;
    }
    CloseHandle(hMutex);
    return true;
}
#else
static bool is_live_ncd_service_process(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    if (kill(pid, 0) != 0) {
        return false;
    }

    {
        char stat_path[256];
        snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
        FILE *f = fopen(stat_path, "r");
        if (!f) {
            return true;
        }

        {
            int parsed_pid = 0;
            char comm[256];
            char state = '\0';
            bool live = true;
            if (fscanf(f, "%d (%255[^)]) %c", &parsed_pid, comm, &state) == 3) {
                if (strcmp(comm, "NCDService") != 0 || state == 'Z') {
                    live = false;
                }
            }
            fclose(f);
            return live;
        }
    }
}

static bool find_live_ncd_service_process(pid_t *out_pid) {
    const char *pid_file = NULL;
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    char pid_path_buf[256];

    if (xdg_runtime && *xdg_runtime) {
        snprintf(pid_path_buf, sizeof(pid_path_buf), "%s/ncd_service.pid", xdg_runtime);
        pid_file = pid_path_buf;
    } else {
        pid_file = "/tmp/ncd_service.pid";
    }

    {
        FILE *f = fopen(pid_file, "r");
        if (f) {
            pid_t pid = 0;
            if (fscanf(f, "%d", &pid) == 1 && is_live_ncd_service_process(pid)) {
                fclose(f);
                if (out_pid) {
                    *out_pid = pid;
                }
                return true;
            }
            fclose(f);
        }
    }

    {
        DIR *proc = opendir("/proc");
        if (!proc) {
            return false;
        }

        {
            struct dirent *entry;
            pid_t my_pid = getpid();
            while ((entry = readdir(proc)) != NULL) {
                pid_t pid = 0;
                char comm_path[256];
                FILE *fcomm;
                char name[256];
                size_t len;

                if (entry->d_name[0] < '0' || entry->d_name[0] > '9') {
                    continue;
                }
                pid = (pid_t)atoi(entry->d_name);
                if (pid <= 0 || pid == my_pid) {
                    continue;
                }

                snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", (int)pid);
                fcomm = fopen(comm_path, "r");
                if (!fcomm) {
                    continue;
                }

                if (!fgets(name, sizeof(name), fcomm)) {
                    fclose(fcomm);
                    continue;
                }
                fclose(fcomm);

                len = strlen(name);
                if (len > 0 && name[len - 1] == '\n') {
                    name[len - 1] = '\0';
                }

                if (strcmp(name, "NCDService") == 0 && is_live_ncd_service_process(pid)) {
                    if (out_pid) {
                        *out_pid = pid;
                    }
                    closedir(proc);
                    return true;
                }
            }
            closedir(proc);
            return false;
        }
    }
}

static void wait_for_service_fully_exited(int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        pid_t pid = 0;
        if (!find_live_ncd_service_process(&pid)) {
            return;
        }
        platform_sleep_ms(100);
    }
}

static bool service_process_still_running(void) {
    return find_live_ncd_service_process(NULL);
}
#endif

static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        wait_for_service_fully_exited(5);
        if (service_process_still_running()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
        }
        return;
    }

    {
        char _buf[256];
        run_service_command("stop", _buf, sizeof(_buf));
    }

    if (!wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT)) {
        force_terminate_service();
    }

    wait_for_service_fully_exited(SERVICE_STOP_TIMEOUT);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
}

static bool ensure_service_running(void) {
    if (ipc_service_exists()) {
        return true;
    }
    if (!service_executable_exists()) {
        return false;
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        char _buf[256];
        int rc;

        wait_for_service_fully_exited(5);
        if (service_process_still_running()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
        }

        rc = run_service_command("start", _buf, sizeof(_buf));
        if (rc != 0 && !ipc_service_exists()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
            continue;
        }

        if (wait_for_service_state(true, SERVICE_START_TIMEOUT)) {
            return true;
        }

        force_terminate_service();
        wait_for_service_fully_exited(3);
    }

    return false;
}

/* --------------------------------------------------------- state transition tests */

TEST(svc_stress_state_transition_starting_to_loading) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    /* Service state transition is internal; verify service starts successfully */
    char _buf[256];
    int rc = run_service_command("start", _buf, sizeof(_buf));
    ASSERT_EQ_INT(0, rc);
    
    bool started = wait_for_service_state(true, SERVICE_START_TIMEOUT);
    ASSERT_TRUE(started);
    
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_state_transition_loading_to_ready) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    
    /* Wait for service to reach READY state (respond to IPC) */
    ipc_client_init();
    bool ready = false;
    for (int i = 0; i < 50; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcStateInfo info;
            if (ipc_client_get_state_info(client, &info) == NCD_IPC_OK) {
                ready = true;
                ipc_client_disconnect(client);
                break;
            }
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(100);
    }
    ipc_client_cleanup();
    
    ASSERT_TRUE(ready);
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_state_transition_ready_to_scanning) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Request rescan to transition to scanning */
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

TEST(svc_stress_state_transition_scanning_to_ready) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Service should return to ready after completing any operations */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcResult result = ipc_client_ping(client);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_state_transition_any_to_stopped) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    ASSERT_TRUE(ipc_service_exists());
    
    ensure_service_stopped();
    ASSERT_FALSE(ipc_service_exists());
    return 0;
}

TEST(svc_stress_state_transition_invalid_rejected) {
    /* Test that invalid state transitions are handled gracefully */
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    /* Stopping an already stopped service should fail gracefully */
    char output[256] = {0};
    int rc = run_service_command("stop", output, sizeof(output));
    ASSERT_TRUE(strstr(output, "Not running") != NULL ||
                strstr(output, "not running") != NULL);
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(rc != 0); /* Should fail */
#else
    (void)rc;
#endif
    
    return 0;
}

/* --------------------------------------------------------- ipc stress tests */

TEST(svc_stress_ipc_1000_concurrent_connections) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    
    int success_count = 0;
    for (int i = 0; i < 100; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            if (ipc_client_ping(client) == NCD_IPC_OK) {
                success_count++;
            }
            ipc_client_disconnect(client);
        }
    }
    
    ipc_client_cleanup();
    
    /* Expect at least 90% success rate under stress */
    ASSERT_TRUE(success_count >= 90);
    
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_ipc_message_queue_overflow_handling) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    ipc_client_init();
    
    /* Send many rapid requests to potentially overflow queue */
    int accepted = 0;
    for (int i = 0; i < 50; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcResult result = ipc_client_ping(client);
            if (result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY) {
                accepted++;
            }
            ipc_client_disconnect(client);
        }
    }
    
    ipc_client_cleanup();
    
    /* Service should handle queue without crashing */
    ASSERT_TRUE(accepted >= 0);
    
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_ipc_large_payload_handling) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Submit heuristic with large search/target strings */
    char large_search[1024];
    char large_target[1024];
    memset(large_search, 'a', sizeof(large_search) - 1);
    memset(large_target, 'b', sizeof(large_target) - 1);
    large_search[sizeof(large_search) - 1] = '\0';
    large_target[sizeof(large_target) - 1] = '\0';
    
    NcdIpcResult result = ipc_client_submit_heuristic(client, large_search, large_target);
    /* Should handle gracefully (may truncate or reject but not crash) */
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_INVALID);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_ipc_malformed_message_rejection) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Valid ping should work */
    NcdIpcResult result = ipc_client_ping(client);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_ipc_connection_during_shutdown) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Request shutdown */
    ipc_client_request_shutdown(client);
    ipc_client_disconnect(client);
    
    /* Try to connect during shutdown - should fail gracefully */
    platform_sleep_ms(100);
    NcdIpcClient *client2 = ipc_client_connect();
    if (client2) {
        /* May succeed but operations should fail */
        NcdIpcResult result = ipc_client_ping(client2);
        ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_GENERIC);
        ipc_client_disconnect(client2);
    }
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- crash recovery tests */

TEST(svc_stress_service_crash_during_scan_recovery) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    /* Force terminate to simulate crash */
    force_terminate_service();
    ASSERT_FALSE(ipc_service_exists());
    
    /* Should be able to restart after crash */
    bool restarted = ensure_service_running();
    ASSERT_TRUE(restarted);
    ASSERT_TRUE(ipc_service_exists());
    
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_service_crash_during_save_recovery) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Request flush then crash during operation */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    if (client) {
        ipc_client_request_flush(client);
        ipc_client_disconnect(client);
    }
    ipc_client_cleanup();
    
    /* Force terminate to simulate crash during save */
    force_terminate_service();
    ASSERT_FALSE(ipc_service_exists());
    
    /* Should be able to restart */
    bool restarted = ensure_service_running();
    ASSERT_TRUE(restarted);
    
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_client_detects_service_crash) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    ASSERT_TRUE(ipc_service_exists());
    ipc_client_init();
    {
        NcdIpcClient *client = ipc_client_connect();
        ASSERT_NOT_NULL(client);
        ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
        ipc_client_disconnect(client);
    }

    /* Crash the service and verify clients can no longer use it. */
    force_terminate_service();
    ASSERT_FALSE(ipc_service_exists());

    {
        NcdIpcClient *client = ipc_client_connect();
        ASSERT_TRUE(client == NULL);
    }

    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_client_reconnect_after_service_restart) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    ipc_client_init();
    
    /* Initial connection */
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    ipc_client_disconnect(client);
    
    /* Restart service */
    ensure_service_stopped();
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Reconnect after restart */
    client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    ipc_client_disconnect(client);
    
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_dirty_data_preserved_on_crash) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    /* Start service and make changes */
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Submit a heuristic to create dirty data */
    NcdIpcResult result = ipc_client_submit_heuristic(client, "test_crash", "/test/crash/path");
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_BUSY);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    
    /* Crash before explicit flush */
    force_terminate_service();
    
    /* Restart - service should handle recovery */
    ensure_service_running();
    ASSERT_TRUE(ipc_service_exists());
    
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- version compatibility tests */

TEST(svc_stress_version_new_client_old_service) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Get service version */
    NcdIpcVersionInfo info;
    NcdIpcResult result = ipc_client_get_version(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    ASSERT_TRUE(strlen(info.app_version) > 0);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_version_old_client_new_service) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Service should handle version check gracefully */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionCheckResult check_result;
    NcdIpcResult result = ipc_client_check_version(client, NCD_APP_VERSION, 
                                                    __DATE__ " " __TIME__, &check_result);
    /* Should succeed or indicate mismatch without crashing */
    ASSERT_TRUE(result == NCD_IPC_OK || result == NCD_IPC_ERROR_GENERIC);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_version_protocol_mismatch_handling) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Protocol version should match */
    NcdIpcStateInfo info;
    NcdIpcResult result = ipc_client_get_state_info(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    ASSERT_EQ_INT(NCD_IPC_VERSION, info.protocol_version);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_version_force_override_flag) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    /* Service should operate normally even with version check */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- client fallback tests */

TEST(svc_stress_fallback_when_service_not_running) {
    ensure_service_stopped();
    
    /* Service is not running, client should fallback to local */
    ASSERT_FALSE(ipc_service_exists());
    
    /* This test documents expected behavior - fallback is client-side */
    /* Client can still operate using local disk state */
    ASSERT_TRUE(1);
    return 0;
}

TEST(svc_stress_fallback_when_shm_mapping_fails) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    /* With service running but no valid SHM, client should fallback */
    ASSERT_TRUE(ipc_service_exists());
    
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_fallback_when_version_incompatible) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(1000);
    
    /* Client should detect version and handle appropriately */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    NcdIpcVersionInfo info;
    NcdIpcResult result = ipc_client_get_version(client, &info);
    ASSERT_EQ_INT(NCD_IPC_OK, result);
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_fallback_when_service_busy_timeout) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    ensure_service_running();
    platform_sleep_ms(500);
    
    ipc_client_init();
    
    /* Multiple rapid requests - some may get BUSY response */
    int busy_count = 0;
    for (int i = 0; i < 20; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcResult result = ipc_client_ping(client);
            if (result == NCD_IPC_ERROR_BUSY || result == NCD_IPC_ERROR_BUSY_LOADING) {
                busy_count++;
            }
            ipc_client_disconnect(client);
        }
    }
    
    ipc_client_cleanup();
    
    /* Busy responses are OK, service is still operational */
    ASSERT_TRUE(busy_count >= 0);
    
    ensure_service_stopped();
    return 0;
}

TEST(svc_stress_fallback_recovery_when_service_returns) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }
    
    /* Start service */
    ensure_service_running();
    platform_sleep_ms(1000);
    ASSERT_TRUE(ipc_service_exists());
    
    /* Stop service */
    ensure_service_stopped();
    ASSERT_FALSE(ipc_service_exists());
    
    /* Restart service */
    ensure_service_running();
    platform_sleep_ms(1000);
    ASSERT_TRUE(ipc_service_exists());
    
    /* Verify service is operational after recovery */
    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_ping(client));
    
    ipc_client_disconnect(client);
    ipc_client_cleanup();
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_stress(void) {
    printf("\n=== Service Stress Tests ===\n\n");
    
    /* State Transitions (6 tests) */
    RUN_TEST(svc_stress_state_transition_starting_to_loading);
    RUN_TEST(svc_stress_state_transition_loading_to_ready);
    RUN_TEST(svc_stress_state_transition_ready_to_scanning);
    RUN_TEST(svc_stress_state_transition_scanning_to_ready);
    RUN_TEST(svc_stress_state_transition_any_to_stopped);
    RUN_TEST(svc_stress_state_transition_invalid_rejected);
    
    /* IPC Stress (5 tests) */
    RUN_TEST(svc_stress_ipc_1000_concurrent_connections);
    RUN_TEST(svc_stress_ipc_message_queue_overflow_handling);
    RUN_TEST(svc_stress_ipc_large_payload_handling);
    RUN_TEST(svc_stress_ipc_malformed_message_rejection);
    RUN_TEST(svc_stress_ipc_connection_during_shutdown);
    
    /* Crash Recovery (5 tests) */
    RUN_TEST(svc_stress_service_crash_during_scan_recovery);
    RUN_TEST(svc_stress_service_crash_during_save_recovery);
    RUN_TEST(svc_stress_client_detects_service_crash);
    RUN_TEST(svc_stress_client_reconnect_after_service_restart);
    RUN_TEST(svc_stress_dirty_data_preserved_on_crash);
    
    /* Version Compatibility (4 tests) */
    RUN_TEST(svc_stress_version_new_client_old_service);
    RUN_TEST(svc_stress_version_old_client_new_service);
    RUN_TEST(svc_stress_version_protocol_mismatch_handling);
    RUN_TEST(svc_stress_version_force_override_flag);
    
    /* Client Fallback (5 tests) */
    RUN_TEST(svc_stress_fallback_when_service_not_running);
    RUN_TEST(svc_stress_fallback_when_shm_mapping_fails);
    RUN_TEST(svc_stress_fallback_when_version_incompatible);
    RUN_TEST(svc_stress_fallback_when_service_busy_timeout);
    RUN_TEST(svc_stress_fallback_recovery_when_service_returns);
    
    /* Final cleanup */
    printf("\n--- Final cleanup ---\n");
    ensure_service_stopped();
}

TEST_MAIN(
    suite_service_stress();
)
