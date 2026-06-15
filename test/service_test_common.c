/* service_test_common.c -- Shared service test utilities (implementations) */

#include "service_test_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ==================================================================
 * Service executable path resolution
 * ================================================================== */

const char *service_get_executable_path(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA("NCDService.exe");
    if (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY))
        return "NCDService.exe";
    return "..\\NCDService.exe";
#else
    /* build.sh produces NCDService; manual builds may use ncd_service */
    if (access("NCDService", X_OK) == 0) return "./NCDService";
    if (access("ncd_service", X_OK) == 0) return "./ncd_service";
    if (access("../NCDService", X_OK) == 0) return "../NCDService";
    return "../ncd_service";
#endif
}

bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(service_get_executable_path());
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    return (access(service_get_executable_path(), X_OK) == 0);
#endif
}

/* ==================================================================
 * Service command execution
 * ================================================================== */

static int run_command_raw(const char *full_cmd, char *output, size_t output_size) {
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

    if (!CreateProcessA(NULL, (char *)full_cmd, NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return -1;
    }

    CloseHandle(hWrite);

    /* Read output */
    if (output && output_size > 0) {
        DWORD bytesRead = 0;
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

    /* Read output - always drain the pipe to avoid SIGPIPE in child */
    if (output && output_size > 0) {
        size_t total = 0;
        while (total < output_size - 1 && !feof(pipe)) {
            size_t n = fread(output + total, 1, output_size - 1 - total, pipe);
            if (n == 0) break;
            total += n;
        }
        output[total] = '\0';
    } else {
        /* Drain the pipe even if caller doesn't want output, to prevent
         * the child process from receiving SIGPIPE when pclose closes the fd. */
        char drain[256];
        while (fread(drain, 1, sizeof(drain), pipe) > 0) {}
    }

    int status = pclose(pipe);
    return safe_exit_code(status);
#endif
}

int run_service_command(const char *cmd, char *output, size_t output_size) {
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", service_get_executable_path(), cmd);
    return run_command_raw(full_cmd, output, output_size);
}

int run_service_command_ex(const char *cmd, const char *extra_args,
                           char *output, size_t output_size) {
    char full_cmd[512];
    const char *exe = service_get_executable_path();
#if NCD_PLATFORM_WINDOWS
    if (extra_args && extra_args[0]) {
        snprintf(full_cmd, sizeof(full_cmd), "%s %s %s", exe, cmd, extra_args);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s %s", exe, cmd);
    }
#else
    if (extra_args && extra_args[0]) {
        snprintf(full_cmd, sizeof(full_cmd), "%s %s %s 2>&1", exe, cmd, extra_args);
    } else {
        snprintf(full_cmd, sizeof(full_cmd), "%s %s 2>&1", exe, cmd);
    }
#endif
    return run_command_raw(full_cmd, output, output_size);
}

/* ==================================================================
 * Service state polling
 * ================================================================== */

bool wait_for_service_state(bool expected_running, int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        bool currently_running = ipc_service_exists();
        if (currently_running == expected_running) {
            return true;
        }
        platform_sleep_ms(100);
    }
    return false;
}

/* ---- Linux process-liveness helpers (used by wait/process helpers) ---- */

#if !NCD_PLATFORM_WINDOWS
static bool is_live_ncd_service_process(pid_t pid) {
    if (pid <= 0) return false;
    if (kill(pid, 0) != 0) return false;

    char stat_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", (int)pid);
    FILE *f = fopen(stat_path, "r");
    if (!f) return true; /* process exists but /proc entry missing (rare) */

    int parsed_pid = 0;
    char comm[256];
    char state = '\0';
    bool live = true;
    if (fscanf(f, "%d (%255[^)]) %c", &parsed_pid, comm, &state) == 3) {
        if ((strcmp(comm, "NCDService") != 0 && strcmp(comm, "ncd_service") != 0) || state == 'Z') {
            live = false;
        }
    }
    fclose(f);
    return live;
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

    /* Try PID file first */
    FILE *f = fopen(pid_file, "r");
    if (f) {
        pid_t pid = 0;
        if (fscanf(f, "%d", &pid) == 1 && is_live_ncd_service_process(pid)) {
            fclose(f);
            if (out_pid) *out_pid = pid;
            return true;
        }
        fclose(f);
    }

    /* Fall back to /proc scan */
    DIR *proc = opendir("/proc");
    if (!proc) return false;

    struct dirent *entry;
    pid_t my_pid = getpid();
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0 || pid == my_pid) continue;

        char comm_path[256];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", (int)pid);
        FILE *fcomm = fopen(comm_path, "r");
        if (!fcomm) continue;

        char name[256];
        if (!fgets(name, sizeof(name), fcomm)) {
            fclose(fcomm);
            continue;
        }
        fclose(fcomm);

        size_t len = strlen(name);
        if (len > 0 && name[len - 1] == '\n') name[len - 1] = '\0';

        if ((strcmp(name, "NCDService") == 0 || strcmp(name, "ncd_service") == 0) && is_live_ncd_service_process(pid)) {
            if (out_pid) *out_pid = pid;
            closedir(proc);
            return true;
        }
    }
    closedir(proc);
    return false;
}
#endif /* !NCD_PLATFORM_WINDOWS */

bool service_process_still_running(void) {
#if NCD_PLATFORM_WINDOWS
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, "NCDService_Instance_7D3F9A2E");
    if (!hMutex) return false;
    CloseHandle(hMutex);
    return true;
#else
    return find_live_ncd_service_process(NULL);
#endif
}

void wait_for_service_fully_exited(int timeout_seconds) {
#if NCD_PLATFORM_WINDOWS
    for (int i = 0; i < timeout_seconds * 10; i++) {
        HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, "NCDService_Instance_7D3F9A2E");
        if (!hMutex) return;
        CloseHandle(hMutex);
        platform_sleep_ms(100);
    }
#else
    for (int i = 0; i < timeout_seconds * 10; i++) {
        pid_t pid = 0;
        if (!find_live_ncd_service_process(&pid)) return;
        platform_sleep_ms(100);
    }
#endif
}

/* ==================================================================
 * Service lifecycle helpers
 * ================================================================== */

void force_terminate_service(void) {
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
    system("pkill -9 -x NCDService 2>/dev/null; pkill -9 -x ncd_service 2>/dev/null; killall -9 NCDService 2>/dev/null; killall -9 ncd_service 2>/dev/null");
    system("rm -f ${XDG_RUNTIME_DIR:-/tmp}/ncd_service.pid 2>/dev/null; rm -f ${XDG_RUNTIME_DIR:-/tmp}/NCDService.pid 2>/dev/null");
#endif
    /* Wait for process to actually exit */
    for (int i = 0; i < 30; i++) {
        if (!ipc_service_exists()) break;
        platform_sleep_ms(100);
    }
}

void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        /* Pipe gone but process may still hold the mutex during cleanup */
        wait_for_service_fully_exited(5);
        if (service_process_still_running()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
        }
        return;
    }

    /* Try graceful stop first */
    { char buf[256]; run_service_command("stop", buf, sizeof(buf)); }

    /* Wait for graceful shutdown */
    bool stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    if (stopped) {
        wait_for_service_fully_exited(5);
        if (service_process_still_running()) {
            force_terminate_service();
            wait_for_service_fully_exited(3);
        }
        return;
    }

    /* Graceful shutdown timed out - force kill */
    printf("  [WARNING] Graceful shutdown timed out, force terminating...\n");
    force_terminate_service();
    wait_for_service_fully_exited(3);
}

/* ==================================================================
 * Bounded condition polling
 * ================================================================== */

bool wait_until(wait_condition_fn cond, int timeout_ms, int interval_ms) {
    if (!cond) return false;
    if (interval_ms <= 0) interval_ms = 50;
    if (timeout_ms <= 0) timeout_ms = 5000;

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (cond()) return true;
        platform_sleep_ms(interval_ms);
        elapsed += interval_ms;
    }
    return cond(); /* One final check */
}
