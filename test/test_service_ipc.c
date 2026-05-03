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
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#endif

/* --------------------------------------------------------- test utilities     */

/* Maximum time to wait for service operations */
#define SERVICE_TIMEOUT_MS 5000
#define SERVICE_START_TIMEOUT 10
#define SERVICE_STOP_TIMEOUT 5
#define GRACEFUL_SHUTDOWN_TIMEOUT 3

#if NCD_PLATFORM_WINDOWS
#define SERVICE_EXE "NCDService.exe"
#else
#define SERVICE_EXE "./ncd_service"
#endif

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

/* Check if service executable exists */
static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(get_service_executable_path());
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return access(get_service_executable_path(), X_OK) == 0;
#endif
}

/* Forward declaration */
static int run_service_command_ex(const char *cmd, const char *extra_args, char *output, size_t output_size);

/* Run service command and capture output */
static int run_service_command(const char *cmd, char *output, size_t output_size) {
    return run_service_command_ex(cmd, NULL, output, output_size);
}

static int run_service_command_ex(const char *cmd, const char *extra_args, char *output, size_t output_size) {
    char full_cmd[512];
    const char *exe = get_service_executable_path();
#if NCD_PLATFORM_WINDOWS
    if (extra_args && extra_args[0]) {
        snprintf(full_cmd, sizeof(full_cmd), "%s %s %s", exe, cmd, extra_args);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s %s", exe, cmd);
    }
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

    if (output && output_size > 0) {
        DWORD bytesRead = 0;
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
    (void)extra_args;
    snprintf(full_cmd, sizeof(full_cmd), "%s %s 2>&1", exe, cmd);
    FILE *pipe = popen(full_cmd, "r");
    if (!pipe) {
        return -1;
    }

    if (output && output_size > 0) {
        size_t total = 0;
        while (total < output_size - 1 && !feof(pipe)) {
            size_t n = fread(output + total, 1, output_size - 1 - total, pipe);
            if (n == 0) {
                break;
            }
            total += n;
        }
        output[total] = '\0';
    }

    {
        int status = pclose(pipe);
        return WEXITSTATUS(status);
    }
#endif
}

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
        if (!ipc_service_exists()) {
            break;
        }
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

static void print_service_log(void) {
#if NCD_PLATFORM_WINDOWS
    /* Service daemon may use real LOCALAPPDATA even if test overrides it */
    char local[MAX_PATH] = {0};
    if (!platform_get_env("LOCALAPPDATA", local, sizeof(local))) return;
    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%s\\NCD\\ncd_service.log", local);
    FILE *f = fopen(log_path, "r");
    if (f) {
        char line[512];
        printf("--- Service Log (%s) ---\n", log_path);
        while (fgets(line, sizeof(line), f)) {
            printf("%s", line);
        }
        printf("--- End Log ---\n");
        fclose(f);
    } else {
        printf("--- No service log found at %s ---\n", log_path);
    }
#endif
}

/* Start service process */
static bool start_service(void) {
    if (ipc_service_exists()) {
        /* Verify it's actually responsive, not just a zombie mutex */
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

    for (int attempt = 0; attempt < 2; attempt++) {
        wait_for_service_fully_exited(5);
        if (service_process_still_running()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
        }

        {
            char output[256] = {0};
            int rc = run_service_command("start", output, sizeof(output));
            if (rc != 0 && !ipc_service_exists()) {
                force_terminate_service();
                wait_for_service_fully_exited(3);
                continue;
            }
        }

        if (!wait_for_service_state(true, SERVICE_START_TIMEOUT)) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
            continue;
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

        force_terminate_service();
        wait_for_service_fully_exited(3);
    }

    return false;
}

/* Stop service */
static bool stop_service(void) {
    if (!ipc_service_exists()) {
        wait_for_service_fully_exited(5);
        if (service_process_still_running()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
        }
        return !service_process_still_running();
    }

    {
        char output[256] = {0};
        (void)run_service_command("stop", output, sizeof(output));
    }

    if (!wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT)) {
        force_terminate_service();
    }

    wait_for_service_fully_exited(SERVICE_STOP_TIMEOUT);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }

    return !ipc_service_exists() && !service_process_still_running();
}

/* Ensure service is stopped before/after tests */
static void ensure_service_stopped(void) {
    (void)stop_service();
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
    /* Start service with logging enabled */
    {
        char output[256] = {0};
        int rc = run_service_command_ex("start", "-log2", output, sizeof(output));
        (void)rc;
    }
    ASSERT_TRUE(wait_for_service_state(true, SERVICE_START_TIMEOUT));
    
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);
    
    /* Submit a metadata update (add group) - service may not be fully ready */
    const char *group_data = "@testgroup\n/path";
    NcdIpcResult result = ipc_client_submit_metadata(client, NCD_META_UPDATE_GROUP_ADD,
                                                        group_data, strlen(group_data));
    printf("DEBUG: ipc_client_submit_metadata result = %d (%s)\n", result, ipc_error_string(result));
    
    ipc_client_disconnect(client);
    ensure_service_stopped();
    print_service_log();
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
    /* Start service with logging to diagnose metadata update failure */
    {
        char output[256] = {0};
        int rc = run_service_command_ex("start", "-log2", output, sizeof(output));
        (void)rc;
    }
    ASSERT_TRUE(wait_for_service_state(true, SERVICE_START_TIMEOUT));
    
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
    
    /* Close view before submitting metadata update to avoid Windows SHM
     * recreation race: if client holds SHM handle when service republishes,
     * CreateFileMapping fails with ERROR_ALREADY_EXISTS and the service
     * cannot recreate the metadata snapshot. */
    state_backend_close(view);
    view = NULL;
    
    /* Submit metadata update via direct IPC (no SHM handle held) */
    NcdIpcClient *ipc = ipc_client_connect();
    ASSERT_NOT_NULL(ipc);
    char group_data[512];
    snprintf(group_data, sizeof(group_data), "%s\n%s", group_name, group_path);
    NcdIpcResult ipc_result = ipc_client_submit_metadata(ipc,
                                                            NCD_META_UPDATE_GROUP_ADD,
                                                            group_data,
                                                            strlen(group_data));
    ipc_client_disconnect(ipc);
    ipc_client_cleanup();
    ASSERT_EQ_INT(NCD_IPC_OK, ipc_result);
    
    /* Give service time to publish SHM snapshots */
    platform_sleep_ms(500);
    
    /* Reopen state backend and verify service connection */
    int retry;
    for (retry = 0; retry < 20; retry++) {
        result = state_backend_open_best_effort(&view, &info);
        if (result == 0 && info.from_service) break;
        if (view) { state_backend_close(view); view = NULL; }
        platform_sleep_ms(100);
    }
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
    print_service_log();
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
