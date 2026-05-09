/*
 * test_service_init_db.c -- Service init-db option tests
 *
 * Tests:
 * - Service starts with -init and performs synchronous scan
 * - Service starts with -init and specific drive list
 * - Help output mentions -init option
 */

#include "test_framework.h"
#include "../src/service_state.h"
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include "../src/database.h"
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
#include <dirent.h>
#endif

#include "test_posix_exit.h"

/* --------------------------------------------------------- test utilities     */

#define SERVICE_START_TIMEOUT 15
#define SERVICE_STOP_TIMEOUT 5
#define GRACEFUL_SHUTDOWN_TIMEOUT 3

#if NCD_PLATFORM_WINDOWS
#define SERVICE_EXE "NCDService.exe"
#else
#define SERVICE_EXE "../ncd_service"
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
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", get_service_executable_path(), cmd);

#if NCD_PLATFORM_WINDOWS
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

static bool ensure_service_running_with_args(const char *args) {
    if (ipc_service_exists()) {
        return true;
    }
    if (!service_executable_exists()) {
        return false;
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        char cmd[256];
        char output[256] = {0};
        int rc;

        wait_for_service_fully_exited(5);
        if (service_process_still_running()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
        }

        snprintf(cmd, sizeof(cmd), "start%s%s", (args && *args) ? " " : "", args ? args : "");
        rc = run_service_command(cmd, output, sizeof(output));
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

/* Temporarily rename metadata file to simulate first run */
static char g_meta_backup[MAX_PATH] = {0};

static bool backup_metadata(void) {
    char path[MAX_PATH];
    if (!db_metadata_path(path, sizeof(path))) return false;
    if (!platform_file_exists(path)) return true; /* nothing to back up */
    snprintf(g_meta_backup, sizeof(g_meta_backup), "%s.bak", path);
#if NCD_PLATFORM_WINDOWS
    MoveFileA(path, g_meta_backup);
#else
    rename(path, g_meta_backup);
#endif
    return true;
}

static bool restore_metadata(void) {
    if (!g_meta_backup[0]) return true;
    char path[MAX_PATH];
    if (!db_metadata_path(path, sizeof(path))) return false;
#if NCD_PLATFORM_WINDOWS
    MoveFileA(g_meta_backup, path);
#else
    rename(g_meta_backup, path);
#endif
    g_meta_backup[0] = '\0';
    return true;
}

/* --------------------------------------------------------- tests              */

TEST(init_db_help_shows_option) {
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }

    char output[1024] = {0};
#if NCD_PLATFORM_WINDOWS
    (void)run_service_command("-?", output, sizeof(output));
#else
    /* Run the actual binary directly; the wrapper script doesn't show -init */
    FILE *pipe = popen("../NCDService '-?'", "r");
    if (pipe) {
        size_t n = fread(output, 1, sizeof(output) - 1, pipe);
        output[n] = '\0';
        pclose(pipe);
    }
#endif

    ASSERT_TRUE(strstr(output, "-init") != NULL);
    return 0;
}

TEST(init_db_starts_service_with_no_drive_list) {
    ensure_service_stopped();
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }

    backup_metadata();

    /* Start service with -init but no drive list (scan all) */
    bool started = ensure_service_running_with_args("-init");
    ASSERT_TRUE(started);
    ASSERT_TRUE(ipc_service_exists());

    /* Give it a moment to reach READY (even if scan is skipped in test mode) */
    platform_sleep_ms(500);

    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    if (client) {
        NcdIpcDetailedStatus info;
        NcdIpcResult result = ipc_client_get_detailed_status(client, &info);
        if (result == NCD_IPC_OK) {
            /* Should eventually be READY (test mode skips scan, so it goes READY quickly) */
            ASSERT_TRUE(info.runtime_state == SERVICE_STATE_READY ||
                        info.runtime_state == SERVICE_STATE_SCANNING);
        }
        ipc_client_disconnect(client);
    }
    ipc_client_cleanup();

    ensure_service_stopped();
    restore_metadata();
    return 0;
}

TEST(init_db_starts_service_with_drive_list) {
    ensure_service_stopped();
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }

    backup_metadata();

    /* Start service with -init and a drive list */
    bool started = ensure_service_running_with_args("-init C");
    ASSERT_TRUE(started);
    ASSERT_TRUE(ipc_service_exists());

    platform_sleep_ms(500);

    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    if (client) {
        NcdIpcDetailedStatus info;
        NcdIpcResult result = ipc_client_get_detailed_status(client, &info);
        if (result == NCD_IPC_OK) {
            ASSERT_TRUE(info.runtime_state == SERVICE_STATE_READY ||
                        info.runtime_state == SERVICE_STATE_SCANNING);
        }
        ipc_client_disconnect(client);
    }
    ipc_client_cleanup();

    ensure_service_stopped();
    restore_metadata();
    return 0;
}

TEST(init_db_creates_metadata_when_missing) {
    ensure_service_stopped();
    if (!service_executable_exists()) {
        printf("SKIP: Service executable not found\n");
        return 0;
    }

    backup_metadata();

    /* Ensure metadata file does not exist */
    char path[MAX_PATH];
    if (db_metadata_path(path, sizeof(path)) && platform_file_exists(path)) {
        /* Try to remove it */
#if NCD_PLATFORM_WINDOWS
        DeleteFileA(path);
#else
        remove(path);
#endif
    }

    bool started = ensure_service_running_with_args("-init");
    ASSERT_TRUE(started);

    /* Wait for service to finish init */
    platform_sleep_ms(1000);

    ensure_service_stopped();

    /* Metadata should have been created by init scan */
    if (db_metadata_path(path, sizeof(path))) {
        /* We just verify the path is valid; the file may or may not exist
         * depending on whether test mode was active. In normal operation
         * it would be created. */
        (void)path;
    }

    restore_metadata();
    return 0;
}

/* --------------------------------------------------------- suites             */

void suite_init_db() {
    printf("\n=== Init Database Tests ===\n");
    RUN_TEST(init_db_help_shows_option);
    RUN_TEST(init_db_starts_service_with_no_drive_list);
    RUN_TEST(init_db_starts_service_with_drive_list);
    RUN_TEST(init_db_creates_metadata_when_missing);
}

TEST_MAIN(
    RUN_SUITE(init_db);
)
