/*
 * service_race_tester.c -- Standalone threaded race-condition tester for NCD service
 *
 * PURPOSE
 *   Launches 8 worker threads that concurrently hammer the NCD service through
 *   both agent CLI commands and direct IPC calls.  A watchdog thread monitors
 *   service health and detects crashes / hangs.
 *
 * RACE SCENARIOS EXERCISED
 *   - Agent query/tree/ls while service publishes SHM snapshots
 *   - Agent mkdir/rmdir while service rescans
 *   - IPC rescan overlapping with agent filesystem mutations
 *   - Multiple concurrent metadata/heuristic submissions
 *   - SHM open/close racing with snapshot publication
 *   - Generation monotonicity under rapid concurrent mutations
 *   - Connection storms on the IPC named-pipe / unix-socket
 *
 * USAGE
 *   service_race_tester [options]
 *
 * OPTIONS
 *   --duration <sec>      Test duration in seconds (default: 30)
 *   --threads <n>         Number of worker threads (default: 8)
 *   --no-service          Assume service already running; do not start/stop
 *   --agent-only          Only run agent-command threads
 *   --ipc-only            Only run IPC-command threads
 *   --verbose, -v         Print every operation result
 *   --help, -h            Show this help
 *
 * EXIT CODES
 *   0  All operations succeeded, no crashes detected
 *   1  Service not running / could not start
 *   2  Service crashed or hung during test
 *   3  Generation monotonicity violation detected
 *   4  One or more operations failed unexpectedly
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "../src/ncd.h"
#include "../src/control_ipc.h"
#include "../src/platform.h"
#include "../src/shm_platform.h"
#include "ipc_test_common.h"

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#include <process.h>
#include <direct.h>
#include <io.h>
#define mkdir(path, mode) _mkdir(path)
#define rmdir(path)       _rmdir(path)
#define access(path, mode) _access(path, mode)
#define F_OK 0
#define POPEN _popen
#define PCLOSE _pclose
#else
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#define POPEN popen
#define PCLOSE pclose
#endif

/* ================================================================ constants */

#define DEFAULT_DURATION_SEC   30
#define DEFAULT_WORKER_THREADS 8
#define WATCHDOG_INTERVAL_MS   200
#define AGENT_CMD_MAX          4096
#define AGENT_OUT_MAX          8192
#define MAX_OPS_PER_THREAD     10000

/* ================================================================ macros */

#define SERVICE_EXE_NAME  (NCD_PLATFORM_WINDOWS ? "NCDService.exe" : "../ncd_service")
#define NCD_EXE_NAME      (NCD_PLATFORM_WINDOWS ? "..\\NewChangeDirectory.exe" : "../NewChangeDirectory")

/* ================================================================ options */

typedef struct {
    int  duration_sec;
    int  worker_threads;
    bool no_service_mgmt;
    bool agent_only;
    bool ipc_only;
    bool verbose;
    bool help;
} TesterOptions;

/* ==================================================== forward declarations */

static bool parse_options(int argc, char **argv, TesterOptions *opts);
static void print_usage(const char *prog);
static bool ensure_service_running(void);
static void ensure_service_stopped(void);
static void force_terminate_service(void);
static bool service_executable_exists(void);
static int  run_service_command(const char *cmd, char *output, size_t output_size);
static bool wait_for_service_state(bool expected_running, int timeout_seconds);
static bool service_process_still_running(void);
static void wait_for_service_fully_exited(int timeout_seconds);

static bool init_temp_dirs(void);
static void cleanup_temp_dirs(void);
static int  run_agent_cmd(const char *subcommand_and_args, char *out, size_t out_size);

static void crash_detected(const char *context);
static bool check_generation_monotonicity(uint64_t prev_meta, uint64_t prev_db,
                                          uint64_t new_meta, uint64_t new_db);

/* Threading primitives */
#if NCD_PLATFORM_WINDOWS
typedef HANDLE thread_handle_t;
typedef unsigned int thread_result_t;
#define THREAD_CALL __stdcall
#else
typedef pthread_t thread_handle_t;
typedef void *thread_result_t;
#define THREAD_CALL
#endif

static thread_handle_t spawn_thread(thread_result_t (THREAD_CALL *fn)(void *), void *arg);
static void join_thread(thread_handle_t h);

/* Synchronised start */
static volatile int g_barrier_target = 0;
static volatile int g_barrier_count  = 0;
static volatile int g_stop_flag      = 0;
static volatile int g_crash_flag     = 0;
static volatile int g_gen_violations = 0;
static char g_crash_context[256] = {0};


/* Operation categories for latency tracking */
typedef enum {
    LAT_CAT_AGENT_READ = 0,   /* agent query/tree/ls/verify/complete */
    LAT_CAT_AGENT_WRITE,      /* agent mkdir/rmdir/mv */
    LAT_CAT_IPC_READ,         /* ipc ping/state/version/status */
    LAT_CAT_IPC_WRITE,        /* ipc rescan/flush/heuristic/metadata */
    LAT_CAT_COUNT
} LatencyCategory;

static const char *lat_category_name(LatencyCategory c) {
    switch (c) {
        case LAT_CAT_AGENT_READ:  return "agent_read";
        case LAT_CAT_AGENT_WRITE: return "agent_write";
        case LAT_CAT_IPC_READ:    return "ipc_read";
        case LAT_CAT_IPC_WRITE:   return "ipc_write";
        default:                  return "unknown";
    }
}

/* Per-category latency bucket */
typedef struct {
    double *samples;
    int cap;
    int count;
    double min_us;
    double max_us;
    double total_us;
} LatencyBucket;

/* Per-thread results */
typedef struct {
    int thread_id;
    int ops_total;
    int ops_ok;
    int ops_fail;
    int ops_busy;
    int ops_connect_fail;
    int agent_fs_mutations;   /* mkdir/rmdir/mv count */
    int agent_fs_failures;
    int ipc_mutations;        /* heuristic/metadata count */
    int shm_open_failures;
    int generation_violations;
    uint64_t last_meta_gen;
    uint64_t last_db_gen;
    LatencyBucket buckets[LAT_CAT_COUNT];
} ThreadResult;

/* Record a latency sample in microseconds into a specific category */
static void record_latency(ThreadResult *r, double us, LatencyCategory cat) {
    if (us < 0 || cat < 0 || cat >= LAT_CAT_COUNT) return;
    LatencyBucket *b = &r->buckets[cat];
    if (b->count < b->cap) {
        b->samples[b->count++] = us;
    }
    if (b->count == 1 || us < b->min_us) b->min_us = us;
    if (us > b->max_us) b->max_us = us;
    b->total_us += us;
}

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* Shared temp base */
static char g_temp_base[NCD_MAX_PATH];
static char g_ncd_data_dir[NCD_MAX_PATH];
static int  g_verbose = 0;

/* ==================================================== service helpers */

static bool service_executable_exists(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA("NCDService.exe");
    if (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY)) return true;
    attribs = GetFileAttributesA("..\\NCDService.exe");
    return (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
#else
    if (access("./NCDService", X_OK) == 0) return true;
    if (access("NCDService", X_OK) == 0) return true;
    if (access("./ncd_service", X_OK) == 0) return true;
    if (access("ncd_service", X_OK) == 0) return true;
    if (access("../NCDService", X_OK) == 0) return true;
    if (access("../ncd_service", X_OK) == 0) return true;
    return false;
#endif
}

static int run_service_command(const char *cmd, char *output, size_t output_size) {
    char full_cmd[512];
#if NCD_PLATFORM_WINDOWS
    const char *svc = "NCDService.exe";
    if (GetFileAttributesA(svc) == INVALID_FILE_ATTRIBUTES) {
        svc = "..\\NCDService.exe"; /* running from test/ subdirectory */
    }
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", svc, cmd);
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
    /* Use explicit ./ prefix to avoid PATH picking up stale system binary */
    const char *svc = "./NCDService";
    if (access(svc, X_OK) != 0) svc = "NCDService";
    if (access(svc, X_OK) != 0) svc = "./ncd_service";
    if (access(svc, X_OK) != 0) svc = "ncd_service";
    if (access(svc, X_OK) != 0) svc = "../NCDService";
    if (access(svc, X_OK) != 0) svc = "../ncd_service";
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", svc, cmd);
    FILE *pipe = POPEN(full_cmd, "r");
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
    int status = PCLOSE(pipe);
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
    system("pkill -9 -x NCDService 2>/dev/null; pkill -9 -x ncd_service 2>/dev/null; killall -9 NCDService 2>/dev/null; killall -9 ncd_service 2>/dev/null");
    system("rm -f ${XDG_RUNTIME_DIR:-/tmp}/ncd_service.pid 2>/dev/null; rm -f ${XDG_RUNTIME_DIR:-/tmp}/NCDService.pid 2>/dev/null");
#endif
    for (int i = 0; i < 20; i++) {
        if (!ipc_service_exists()) break;
        platform_sleep_ms(100);
    }
}

static void wait_for_service_fully_exited(int timeout_seconds) {
    for (int i = 0; i < timeout_seconds * 10; i++) {
        if (!service_process_still_running()) return;
        platform_sleep_ms(100);
    }
}

static bool service_process_still_running(void) {
#if NCD_PLATFORM_WINDOWS
    HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, "NCDService_Instance_7D3F9A2E");
    if (!hMutex) return false;
    CloseHandle(hMutex);
    return true;
#else
    DIR *proc = opendir("/proc");
    if (!proc) return false;
    struct dirent *entry;
    pid_t my_pid = getpid();
    bool found = false;
    while ((entry = readdir(proc)) != NULL) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        pid_t pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0 || pid == my_pid) continue;
        char comm_path[256];
        snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", (int)pid);
        FILE *fcomm = fopen(comm_path, "r");
        if (!fcomm) continue;
        char name[256];
        if (fgets(name, sizeof(name), fcomm)) {
            size_t len = strlen(name);
            if (len > 0 && name[len - 1] == '\n') name[len - 1] = '\0';
            if (strcmp(name, "NCDService") == 0 || strcmp(name, "ncd_service") == 0) {
                if (kill(pid, 0) == 0) { found = true; }
            }
        }
        fclose(fcomm);
        if (found) break;
    }
    closedir(proc);
    return found;
#endif
}

static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        wait_for_service_fully_exited(5);
        if (service_process_still_running()) force_terminate_service();
        return;
    }
    char _buf[256];
    run_service_command("stop", _buf, sizeof(_buf));
    if (!wait_for_service_state(false, 5)) force_terminate_service();
    wait_for_service_fully_exited(5);
    if (service_process_still_running()) {
        force_terminate_service();
        wait_for_service_fully_exited(3);
    }
}

static bool ensure_service_running(void) {
    if (ipc_service_exists()) return true;
    if (!service_executable_exists()) return false;
    for (int attempt = 0; attempt < 2; attempt++) {
        char _buf[256];
        int rc;
        ensure_service_stopped();
        rc = run_service_command("start", _buf, sizeof(_buf));
        if (rc != 0 && !ipc_service_exists()) {
            force_terminate_service();
            continue;
        }
        if (wait_for_service_state(true, 10)) return true;
        force_terminate_service();
    }
    return false;
}

/* ==================================================== temp / agent helpers */

static bool init_temp_dirs(void) {
    const char *tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMP");
    if (!tmp) tmp = "/tmp";
#if NCD_PLATFORM_WINDOWS
    snprintf(g_temp_base, sizeof(g_temp_base), "%s\\ncd_race_test", tmp);
    snprintf(g_ncd_data_dir, sizeof(g_ncd_data_dir), "%s\\NCD", g_temp_base);
#else
    snprintf(g_temp_base, sizeof(g_temp_base), "%s/ncd_race_test", tmp);
    snprintf(g_ncd_data_dir, sizeof(g_ncd_data_dir), "%s/ncd", g_temp_base);
#endif

    /* Clean and recreate */
#if NCD_PLATFORM_WINDOWS
    {
        char cmd[NCD_MAX_PATH * 2];
        snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", g_temp_base);
        system(cmd);
    }
#else
    {
        char cmd[NCD_MAX_PATH * 2];
        snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" 2>/dev/null", g_temp_base);
        system(cmd);
    }
#endif

    if (mkdir(g_temp_base, 0755) != 0 && errno != EEXIST) return false;
    if (mkdir(g_ncd_data_dir, 0755) != 0 && errno != EEXIST) return false;
    return true;
}

static void cleanup_temp_dirs(void) {
#if NCD_PLATFORM_WINDOWS
    char cmd[NCD_MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", g_temp_base);
    system(cmd);
#else
    char cmd[NCD_MAX_PATH * 2];
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\" 2>/dev/null", g_temp_base);
    system(cmd);
#endif
}

static int run_agent_cmd(const char *subcommand_and_args, char *out, size_t out_size) {
    const char *exe = NCD_EXE_NAME;
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA(exe);
    if (attribs == INVALID_FILE_ATTRIBUTES) {
        snprintf(out, out_size, "EXE_NOT_FOUND");
        return -1;
    }
#else
    if (access(exe, X_OK) != 0) {
        snprintf(out, out_size, "EXE_NOT_FOUND");
        return -1;
    }
#endif

    char cmd[NCD_MAX_PATH * 4];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "set \"LOCALAPPDATA=%s\" && set NCD_TEST_MODE=1 && \"%s\" /agent %s",
        g_ncd_data_dir, exe, subcommand_and_args);
#else
    snprintf(cmd, sizeof(cmd),
        "XDG_DATA_HOME='%s' NCD_TEST_MODE=1 '%s' --agent:%s",
        g_ncd_data_dir, exe, subcommand_and_args);
#endif

    FILE *fp = POPEN(cmd, "r");
    if (!fp) {
        snprintf(out, out_size, "POPEN_FAIL");
        return -1;
    }
    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    return PCLOSE(fp);
}

/*
 * An agent command is considered "successful" for race-testing purposes if it
 * returns well-formed JSON (or a non-empty structured response).  Exit codes
 * vary by sub-command (query returns 1 when no matches), so we inspect output.
 */
static bool agent_response_ok(const char *out) {
    if (!out || !out[0]) return false;
    if (strncmp(out, "EXE_NOT_FOUND", 13) == 0) return false;
    if (strncmp(out, "POPEN_FAIL", 10) == 0) return false;
    /* Valid JSON starts with '{' or '[' */
    if (out[0] == '{' || out[0] == '[') return true;
    /* Some agent commands print plain text that is also OK */
    if (strstr(out, "created") || strstr(out, "removed") || strstr(out, "moved")
        || strstr(out, "verified") || strstr(out, "exists")) {
        return true;
    }
    return false;
}

/* ==================================================== thread primitives */

static thread_handle_t spawn_thread(thread_result_t (THREAD_CALL *fn)(void *), void *arg) {
#if NCD_PLATFORM_WINDOWS
    return (thread_handle_t)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
#else
    pthread_t tid;
    pthread_create(&tid, NULL, (void *(*)(void *))fn, arg);
    return tid;
#endif
}

static void join_thread(thread_handle_t h) {
#if NCD_PLATFORM_WINDOWS
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
#else
    pthread_join(h, NULL);
#endif
}

/* ==================================================== synchronised start */

static void barrier_wait(int target) {
    if (target <= 0) return;
#if NCD_PLATFORM_WINDOWS
    InterlockedIncrement((LONG *)&g_barrier_count);
    while (g_barrier_count < target && !g_stop_flag && !g_crash_flag) {
        Sleep(1);
    }
#else
    __sync_add_and_fetch(&g_barrier_count, 1);
    while (g_barrier_count < target && !g_stop_flag && !g_crash_flag) {
        usleep(1000);
    }
#endif
}

/* ==================================================== crash / invariant helpers */

static void crash_detected(const char *context) {
    if (g_crash_flag) return;
    g_crash_flag = 1;
    strncpy(g_crash_context, context, sizeof(g_crash_context) - 1);
    g_crash_context[sizeof(g_crash_context) - 1] = '\0';
}

static bool check_generation_monotonicity(uint64_t prev_meta, uint64_t prev_db,
                                          uint64_t new_meta, uint64_t new_db) {
    if (new_meta < prev_meta || new_db < prev_db) {
#if NCD_PLATFORM_WINDOWS
        InterlockedIncrement((LONG *)&g_gen_violations);
#else
        __sync_add_and_fetch(&g_gen_violations, 1);
#endif
        return false;
    }
    return true;
}

/* ==================================================== agent command builders */

static void make_thread_temp_dir(int tid, char *buf, size_t size) {
#if NCD_PLATFORM_WINDOWS
    snprintf(buf, size, "%s\\t%02d", g_temp_base, tid);
#else
    snprintf(buf, size, "%s/t%02d", g_temp_base, tid);
#endif
}

/* ---------------------------------------------------- thread 0: agent query */
static thread_result_t THREAD_CALL worker_agent_query(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;
    r->ops_busy = 0; r->ops_connect_fail = 0;

    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        char args[256];
        char out[AGENT_OUT_MAX];
        const char *term = (i % 3 == 0) ? "win" : ((i % 3 == 1) ? "sys" : "usr");
        snprintf(args, sizeof(args), "query %s --json --limit 5", term);
        ipc_time_t t0, t1;
        ipc_get_time(&t0);
        int status = run_agent_cmd(args, out, sizeof(out));
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), LAT_CAT_AGENT_READ);
        r->ops_total++;
        if (agent_response_ok(out)) {
            r->ops_ok++;
        } else {
            r->ops_fail++;
            if (g_verbose) {
                fprintf(stderr, "[T%d agent_query] FAIL: status=%d out=%.200s\n", r->thread_id, status, out);
            }
        }
        platform_sleep_ms(5);
    }
    return 0;
}

/* ---------------------------------------------------- thread 1: agent tree/ls */
static thread_result_t THREAD_CALL worker_agent_tree_ls(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;

    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        char args[512];
        char out[AGENT_OUT_MAX];
        if (i % 2 == 0) {
            snprintf(args, sizeof(args), "tree \"%s\" --json --depth 2", g_temp_base);
        } else {
            snprintf(args, sizeof(args), "ls \"%s\" --json --depth 1", g_temp_base);
        }
        ipc_time_t t0, t1;
        ipc_get_time(&t0);
        int status = run_agent_cmd(args, out, sizeof(out));
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), LAT_CAT_AGENT_READ);
        r->ops_total++;
        if (agent_response_ok(out)) {
            r->ops_ok++;
        } else {
            r->ops_fail++;
            if (g_verbose) {
                fprintf(stderr, "[T%d agent_tree_ls] FAIL: status=%d out=%.200s\n", r->thread_id, status, out);
            }
        }
        platform_sleep_ms(8);
    }
    return 0;
}

/* ---------------------------------------------------- thread 2: agent mkdir (shared collision dir) */
static thread_result_t THREAD_CALL worker_agent_mkdir(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;
    r->agent_fs_mutations = 0; r->agent_fs_failures = 0;

    char coll_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(coll_dir, sizeof(coll_dir), "%s\\collision", g_temp_base);
#else
    snprintf(coll_dir, sizeof(coll_dir), "%s/collision", g_temp_base);
#endif
    mkdir(coll_dir, 0755);

    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        char path[NCD_MAX_PATH];
        char args[NCD_MAX_PATH + 64];
        char out[AGENT_OUT_MAX];
#if NCD_PLATFORM_WINDOWS
        snprintf(path, sizeof(path), "%s\\t%d_dir_%d", coll_dir, r->thread_id, i);
#else
        snprintf(path, sizeof(path), "%s/t%d_dir_%d", coll_dir, r->thread_id, i);
#endif
#if NCD_PLATFORM_WINDOWS
        snprintf(args, sizeof(args), "mkdir \"%s\" --json", path);
#else
        snprintf(args, sizeof(args), "mkdir '%s' --json", path);
#endif
        ipc_time_t t0, t1;
        ipc_get_time(&t0);
        int status = run_agent_cmd(args, out, sizeof(out));
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), LAT_CAT_AGENT_WRITE);
        r->ops_total++;
        r->agent_fs_mutations++;
        if (agent_response_ok(out)) {
            r->ops_ok++;
        } else {
            r->ops_fail++;
            r->agent_fs_failures++;
            if (g_verbose) {
                fprintf(stderr, "[T%d agent_mkdir] FAIL: status=%d out=%.200s\n", r->thread_id, status, out);
            }
        }
        platform_sleep_ms(10);
    }
    return 0;
}

/* ---------------------------------------------------- thread 3: agent rmdir/mv (shared collision dir) */
static thread_result_t THREAD_CALL worker_agent_rmdir_mv(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;
    r->agent_fs_mutations = 0; r->agent_fs_failures = 0;

    char coll_dir[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(coll_dir, sizeof(coll_dir), "%s\\collision", g_temp_base);
#else
    snprintf(coll_dir, sizeof(coll_dir), "%s/collision", g_temp_base);
#endif
    mkdir(coll_dir, 0755);

    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        char path[NCD_MAX_PATH];
        char args[NCD_MAX_PATH * 2 + 64];
        char out[AGENT_OUT_MAX];
        int other_tid = (r->thread_id + 1) % DEFAULT_WORKER_THREADS;
#if NCD_PLATFORM_WINDOWS
        snprintf(path, sizeof(path), "%s\\t%d_dir_%d", coll_dir, other_tid, i);
#else
        snprintf(path, sizeof(path), "%s/t%d_dir_%d", coll_dir, other_tid, i);
#endif
        /* First mkdir (may already exist from prior run or other thread) */
        {
            char mk_args[NCD_MAX_PATH + 64];
#if NCD_PLATFORM_WINDOWS
            snprintf(mk_args, sizeof(mk_args), "mkdir \"%s\" --json", path);
#else
            snprintf(mk_args, sizeof(mk_args), "mkdir '%s' --json", path);
#endif
            run_agent_cmd(mk_args, out, sizeof(out));
        }

        if (i % 2 == 0) {
            /* rmdir */
#if NCD_PLATFORM_WINDOWS
            snprintf(args, sizeof(args), "rmdir \"%s\" --force --json", path);
#else
            snprintf(args, sizeof(args), "rmdir '%s' --force --json", path);
#endif
        } else {
            /* mv to sibling */
            char dst[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
            snprintf(dst, sizeof(dst), "%s\\t%d_mv_%d", coll_dir, r->thread_id, i);
            snprintf(args, sizeof(args), "mv \"%s\" \"%s\" --force --json", path, dst);
#else
            snprintf(dst, sizeof(dst), "%s/t%d_mv_%d", coll_dir, r->thread_id, i);
            snprintf(args, sizeof(args), "mv '%s' '%s' --force --json", path, dst);
#endif
        }
        ipc_time_t t0, t1;
        ipc_get_time(&t0);
        int status = run_agent_cmd(args, out, sizeof(out));
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), LAT_CAT_AGENT_WRITE);
        r->ops_total++;
        r->agent_fs_mutations++;
        if (agent_response_ok(out)) {
            r->ops_ok++;
        } else {
            r->ops_fail++;
            r->agent_fs_failures++;
            if (g_verbose) {
                fprintf(stderr, "[T%d agent_rmdir_mv] FAIL: status=%d out=%.200s\n", r->thread_id, status, out);
            }
        }
        platform_sleep_ms(12);
    }
    return 0;
}

/* ---------------------------------------------------- thread 4: agent verify/complete */
static thread_result_t THREAD_CALL worker_agent_verify_complete(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;

    char my_dir[NCD_MAX_PATH];
    make_thread_temp_dir(r->thread_id, my_dir, sizeof(my_dir));
    mkdir(my_dir, 0755);

    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        char args[512];
        char out[AGENT_OUT_MAX];
        if (i % 2 == 0) {
#if NCD_PLATFORM_WINDOWS
            snprintf(args, sizeof(args), "verify \"%s\" --json", my_dir);
#else
            snprintf(args, sizeof(args), "verify '%s' --json", my_dir);
#endif
        } else {
            snprintf(args, sizeof(args), "complete dir --json --limit 3");
        }
        ipc_time_t t0, t1;
        ipc_get_time(&t0);
        int status = run_agent_cmd(args, out, sizeof(out));
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), LAT_CAT_AGENT_READ);
        r->ops_total++;
        if (agent_response_ok(out)) {
            r->ops_ok++;
        } else {
            r->ops_fail++;
            if (g_verbose) {
                fprintf(stderr, "[T%d agent_verify_complete] FAIL: status=%d out=%.200s\n", r->thread_id, status, out);
            }
        }
        platform_sleep_ms(7);
    }
    return 0;
}

/* ---------------------------------------------------- thread 5: IPC rescan/flush */
static thread_result_t THREAD_CALL worker_ipc_rescan_flush(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;
    r->ops_busy = 0; r->ops_connect_fail = 0;

    ipc_client_init();
    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            r->ops_connect_fail++;
            platform_sleep_ms(50);
            continue;
        }

        NcdIpcResult rc;
        ipc_time_t t0, t1;
        LatencyCategory cat;
        ipc_get_time(&t0);
        if (i % 4 == 0) {
            bool drive_mask[26] = {false};
            rc = ipc_client_request_rescan(client, drive_mask, false);
            cat = LAT_CAT_IPC_WRITE;
        } else if (i % 4 == 1) {
            rc = ipc_client_request_flush(client);
            cat = LAT_CAT_IPC_WRITE;
        } else if (i % 4 == 2) {
            NcdIpcDetailedStatus info;
            rc = ipc_client_get_detailed_status(client, &info);
            cat = LAT_CAT_IPC_READ;
        } else {
            rc = ipc_client_ping(client);
            cat = LAT_CAT_IPC_READ;
        }
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), cat);

        r->ops_total++;
        if (rc == NCD_IPC_OK) {
            r->ops_ok++;
        } else if (rc == NCD_IPC_ERROR_BUSY || rc == NCD_IPC_ERROR_BUSY_SCANNING) {
            r->ops_busy++;
        } else {
            r->ops_fail++;
            if (g_verbose) {
                fprintf(stderr, "[T%d ipc_rescan_flush] FAIL: rc=%d (%s)\n", r->thread_id, rc, ipc_error_string(rc));
            }
        }

        ipc_client_disconnect(client);
        platform_sleep_ms(15);
    }

    ipc_client_cleanup();
    return 0;
}

/* ---------------------------------------------------- thread 6: IPC heuristic/metadata */
static thread_result_t THREAD_CALL worker_ipc_mutations(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;
    r->ipc_mutations = 0; r->ops_connect_fail = 0;

    ipc_client_init();
    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            r->ops_connect_fail++;
            platform_sleep_ms(50);
            continue;
        }

        NcdIpcResult rc;
        ipc_time_t t0, t1;
        LatencyCategory cat;
        ipc_get_time(&t0);
        if (i % 3 == 0) {
            char search[64], target[128];
            snprintf(search, sizeof(search), "race_t6_%d_%d", r->thread_id, i);
            snprintf(target, sizeof(target), "/race/t6/%d/%d", r->thread_id, i);
            rc = ipc_client_submit_heuristic(client, search, target);
            r->ipc_mutations++;
            cat = LAT_CAT_IPC_WRITE;
        } else if (i % 3 == 1) {
            char path[128];
            snprintf(path, sizeof(path), "/tmp/race_t6_%d_%d", r->thread_id, i);
            rc = ipc_client_submit_metadata(client, NCD_META_UPDATE_DIR_HISTORY_ADD, path, strlen(path) + 1);
            r->ipc_mutations++;
            cat = LAT_CAT_IPC_WRITE;
        } else {
            NcdIpcStateInfo info;
            rc = ipc_client_get_state_info(client, &info);
            cat = LAT_CAT_IPC_READ;
            if (rc == NCD_IPC_OK) {
                check_generation_monotonicity(r->last_meta_gen, r->last_db_gen,
                                              info.meta_generation, info.db_generation);
                r->last_meta_gen = info.meta_generation;
                r->last_db_gen = info.db_generation;

                if (shm_platform_init() == SHM_OK) {
                    ShmHandle *meta_shm = NULL;
                    ShmHandle *db_shm = NULL;
                    if (shm_open_existing(info.meta_name, SHM_ACCESS_READ, &meta_shm) != SHM_OK) {
                        r->shm_open_failures++;
                    } else {
                        shm_close(meta_shm);
                    }
                    if (shm_open_existing(info.db_name, SHM_ACCESS_READ, &db_shm) != SHM_OK) {
                        r->shm_open_failures++;
                    } else {
                        shm_close(db_shm);
                    }
                    shm_platform_cleanup();
                }
            }
        }
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), cat);

        r->ops_total++;
        if (rc == NCD_IPC_OK) {
            r->ops_ok++;
        } else if (rc == NCD_IPC_ERROR_BUSY || rc == NCD_IPC_ERROR_BUSY_SCANNING) {
            r->ops_busy++;
        } else {
            r->ops_fail++;
            if (g_verbose) {
                fprintf(stderr, "[T%d ipc_mutations] FAIL: rc=%d (%s)\n", r->thread_id, rc, ipc_error_string(rc));
            }
        }

        ipc_client_disconnect(client);
        platform_sleep_ms(10);
    }

    ipc_client_cleanup();
    return 0;
}

/* ---------------------------------------------------- thread 7: IPC version/check/shutdown storm */
static thread_result_t THREAD_CALL worker_ipc_storm(void *arg) {
    ThreadResult *r = (ThreadResult *)arg;
    r->ops_total = 0; r->ops_ok = 0; r->ops_fail = 0;
    r->ops_connect_fail = 0;

    ipc_client_init();
    barrier_wait(g_barrier_target);

    for (int i = 0; i < MAX_OPS_PER_THREAD && !g_stop_flag && !g_crash_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            r->ops_connect_fail++;
            platform_sleep_ms(20);
            continue;
        }

        NcdIpcResult rc;
        ipc_time_t t0, t1;
        ipc_get_time(&t0);
        if (i % 4 == 0) {
            NcdIpcVersionInfo info;
            rc = ipc_client_get_version(client, &info);
        } else if (i % 4 == 1) {
            rc = ipc_client_ping(client);
        } else if (i % 4 == 2) {
            NcdIpcStateInfo info;
            rc = ipc_client_get_state_info(client, &info);
            if (rc == NCD_IPC_OK) {
                check_generation_monotonicity(r->last_meta_gen, r->last_db_gen,
                                              info.meta_generation, info.db_generation);
                r->last_meta_gen = info.meta_generation;
                r->last_db_gen = info.db_generation;
            }
        } else {
            NcdIpcDetailedStatus info;
            rc = ipc_client_get_detailed_status(client, &info);
        }
        ipc_get_time(&t1);
        record_latency(r, ipc_elapsed_us(&t0, &t1), LAT_CAT_IPC_READ);

        r->ops_total++;
        if (rc == NCD_IPC_OK) {
            r->ops_ok++;
        } else {
            r->ops_fail++;
            if (g_verbose) {
                fprintf(stderr, "[T%d ipc_storm] FAIL: rc=%d (%s)\n", r->thread_id, rc, ipc_error_string(rc));
            }
        }

        ipc_client_disconnect(client);
        platform_sleep_ms(5);
    }

    ipc_client_cleanup();
    return 0;
}

/* ==================================================== watchdog thread */

static thread_result_t THREAD_CALL watchdog_thread(void *arg) {
    (void)arg;
    ipc_client_init();

    int consecutive_failures = 0;
    while (!g_stop_flag) {
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            consecutive_failures++;
            if (consecutive_failures >= 5) {
                crash_detected("IPC connect failed 5 times in a row");
                break;
            }
        } else {
            NcdIpcResult rc = ipc_client_ping(client);
            ipc_client_disconnect(client);
            if (rc == NCD_IPC_OK) {
                consecutive_failures = 0;
            } else {
                consecutive_failures++;
                if (consecutive_failures >= 5) {
                    crash_detected("IPC ping failed 5 times in a row");
                    break;
                }
            }
        }

        /* Also verify process still exists */
        if (!service_process_still_running()) {
            crash_detected("Service process no longer in process table");
            break;
        }

        platform_sleep_ms(WATCHDOG_INTERVAL_MS);
    }

    ipc_client_cleanup();
    return 0;
}

/* ==================================================== option parsing */

static bool parse_options(int argc, char **argv, TesterOptions *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->duration_sec = DEFAULT_DURATION_SEC;
    opts->worker_threads = DEFAULT_WORKER_THREADS;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            opts->help = true;
            return false;
        } else if (strcmp(argv[i], "--duration") == 0) {
            if (++i >= argc || (opts->duration_sec = atoi(argv[i])) < 1) {
                fprintf(stderr, "Error: --duration requires a positive integer\n");
                return false;
            }
        } else if (strcmp(argv[i], "--threads") == 0) {
            if (++i >= argc || (opts->worker_threads = atoi(argv[i])) < 1 || opts->worker_threads > 64) {
                fprintf(stderr, "Error: --threads requires 1-64\n");
                return false;
            }
        } else if (strcmp(argv[i], "--no-service") == 0) {
            opts->no_service_mgmt = true;
        } else if (strcmp(argv[i], "--agent-only") == 0) {
            opts->agent_only = true;
        } else if (strcmp(argv[i], "--ipc-only") == 0) {
            opts->ipc_only = true;
        } else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            opts->verbose = true;
        } else {
            fprintf(stderr, "Error: Unknown option: %s\n", argv[i]);
            return false;
        }
    }

    if (opts->agent_only && opts->ipc_only) {
        fprintf(stderr, "Error: --agent-only and --ipc-only are mutually exclusive\n");
        return false;
    }

    return true;
}

static void print_usage(const char *prog) {
    printf("NCD Service Race Condition Tester\n");
    printf("=================================\n\n");
    printf("Usage: %s [options]\n\n", prog);
    printf("Options:\n");
    printf("  --duration <sec>    Test duration in seconds (default: %d)\n", DEFAULT_DURATION_SEC);
    printf("  --threads <n>       Number of worker threads (default: %d)\n", DEFAULT_WORKER_THREADS);
    printf("  --no-service        Do not start/stop service (assume running)\n");
    printf("  --agent-only        Only exercise agent CLI commands\n");
    printf("  --ipc-only          Only exercise IPC protocol commands\n");
    printf("  --verbose, -v       Show per-operation details\n");
    printf("  --help, -h          Show this help\n");
    printf("\nThis tool launches worker threads that concurrently hit the NCD service\n");
    printf("through both agent commands and IPC calls to expose race conditions.\n");
    printf("A watchdog thread monitors service health and detects crashes/hangs.\n");
}

/* ==================================================== main */

int main(int argc, char **argv) {
    TesterOptions opts;
    if (!parse_options(argc, argv, &opts)) {
        if (opts.help) {
            print_usage(argv[0]);
            return 0;
        }
        return 4;
    }

    printf("NCD Service Race Condition Tester\n");
    printf("=================================\n\n");
    printf("Configuration:\n");
    printf("  Duration:      %d seconds\n", opts.duration_sec);
    printf("  Worker threads: %d\n", opts.worker_threads);
    printf("  Agent only:    %s\n", opts.agent_only ? "yes" : "no");
    printf("  IPC only:      %s\n", opts.ipc_only ? "yes" : "no");
    printf("  Service mgmt:  %s\n", opts.no_service_mgmt ? "external" : "managed");
    printf("\n");

    /* Init temp dirs for agent commands */
    if (!init_temp_dirs()) {
        fprintf(stderr, "ERROR: Failed to create temp directories\n");
        return 4;
    }

    /* Ensure service is running */
    if (!opts.no_service_mgmt) {
        if (!ensure_service_running()) {
            fprintf(stderr, "ERROR: Could not start NCD service\n");
            cleanup_temp_dirs();
            return 1;
        }
        printf("Service: STARTED\n");
    } else {
        if (!ipc_service_exists()) {
            fprintf(stderr, "ERROR: Service not running and --no-service specified\n");
            cleanup_temp_dirs();
            return 1;
        }
        printf("Service: ALREADY RUNNING\n");
    }

    /* Give service a moment to reach READY */
    platform_sleep_ms(500);

    /* Prepare thread results */
    int n_workers = opts.worker_threads;
    if (n_workers > 8) n_workers = 8; /* cap at predefined worker types for now */
    if (n_workers < 1) n_workers = 1;

    ThreadResult *results = calloc(n_workers, sizeof(ThreadResult));
    thread_handle_t *threads = calloc(n_workers, sizeof(thread_handle_t));

    /* Allocate per-category latency buckets */
    for (int i = 0; i < n_workers; i++) {
        for (int c = 0; c < LAT_CAT_COUNT; c++) {
            results[i].buckets[c].cap = MAX_OPS_PER_THREAD;
            results[i].buckets[c].samples = calloc(MAX_OPS_PER_THREAD, sizeof(double));
            results[i].buckets[c].min_us = 999999999.0;
            results[i].buckets[c].max_us = 0.0;
        }
    }

    /* Reset barrier */
    g_barrier_target = n_workers;
    g_barrier_count = 0;
    g_stop_flag = 0;
    g_crash_flag = 0;
    g_gen_violations = 0;
    g_verbose = opts.verbose;

    /* Spawn worker threads */
    typedef thread_result_t (THREAD_CALL *worker_fn_t)(void *);
    worker_fn_t workers[8] = {
        worker_agent_query,
        worker_agent_tree_ls,
        worker_agent_mkdir,
        worker_agent_rmdir_mv,
        worker_agent_verify_complete,
        worker_ipc_rescan_flush,
        worker_ipc_mutations,
        worker_ipc_storm
    };

    for (int i = 0; i < n_workers; i++) {
        results[i].thread_id = i;
        if (opts.agent_only && i >= 5) {
            /* Remap agent-only threads to agent workers */
            results[i].thread_id = i;
            threads[i] = spawn_thread(workers[i % 5], &results[i]);
        } else if (opts.ipc_only && i < 5) {
            /* Remap IPC-only threads to IPC workers */
            threads[i] = spawn_thread(workers[5 + (i % 3)], &results[i]);
        } else {
            threads[i] = spawn_thread(workers[i], &results[i]);
        }
    }

    /* Spawn watchdog */
    thread_handle_t watchdog = spawn_thread(watchdog_thread, NULL);

    printf("Threads: %d workers + 1 watchdog spawned\n", n_workers);
    printf("Running for %d seconds...\n\n", opts.duration_sec);

    /* Progress monitor */
    ipc_time_t test_start, now;
    ipc_get_time(&test_start);
    int last_reported_sec = -1;

    while (!g_crash_flag) {
        ipc_get_time(&now);
        double elapsed_sec = ipc_elapsed_ms(&test_start, &now) / 1000.0;
        if ((int)elapsed_sec != last_reported_sec) {
            last_reported_sec = (int)elapsed_sec;
            int total_ops = 0;
            for (int i = 0; i < n_workers; i++) total_ops += results[i].ops_total;
            printf("  [%3d/%d sec] %d ops  \r", last_reported_sec, opts.duration_sec, total_ops);
            fflush(stdout);
        }
        if (elapsed_sec >= opts.duration_sec) {
            g_stop_flag = 1;
            break;
        }
        platform_sleep_ms(100);
    }

    printf("\n\n");

    /* Signal stop and wait for workers */
    g_stop_flag = 1;

    for (int i = 0; i < n_workers; i++) {
        join_thread(threads[i]);
    }
    join_thread(watchdog);

    /* Aggregate results */
    int total_ops = 0, total_ok = 0, total_fail = 0, total_busy = 0;
    int total_connect_fail = 0, total_fs_mut = 0, total_fs_fail = 0;
    int total_ipc_mut = 0, total_shm_fail = 0;

    for (int i = 0; i < n_workers; i++) {
        total_ops += results[i].ops_total;
        total_ok += results[i].ops_ok;
        total_fail += results[i].ops_fail;
        total_busy += results[i].ops_busy;
        total_connect_fail += results[i].ops_connect_fail;
        total_fs_mut += results[i].agent_fs_mutations;
        total_fs_fail += results[i].agent_fs_failures;
        total_ipc_mut += results[i].ipc_mutations;
        total_shm_fail += results[i].shm_open_failures;
    }

    /* (latency aggregation now done per-category in the report section) */

    /* Print report */
    printf("------------------------- Results -------------------------\n");
    printf("Thread breakdown:\n");
    for (int i = 0; i < n_workers; i++) {
        printf("  T%d: %5d ops | OK=%5d FAIL=%4d BUSY=%4d CONN_FAIL=%3d",
               i, results[i].ops_total, results[i].ops_ok, results[i].ops_fail,
               results[i].ops_busy, results[i].ops_connect_fail);
        if (results[i].agent_fs_mutations > 0)
            printf(" | FS=%4d FS_FAIL=%3d", results[i].agent_fs_mutations, results[i].agent_fs_failures);
        if (results[i].ipc_mutations > 0)
            printf(" | IPC_MUT=%4d", results[i].ipc_mutations);
        if (results[i].shm_open_failures > 0)
            printf(" | SHM_FAIL=%3d", results[i].shm_open_failures);
        printf("\n");
    }
    printf("\nTotals:\n");
    printf("  Operations:       %d\n", total_ops);
    printf("  Success:          %d\n", total_ok);
    printf("  Failed:           %d\n", total_fail);
    printf("  Busy responses:   %d\n", total_busy);
    printf("  Connect failures: %d\n", total_connect_fail);
    printf("  FS mutations:     %d (failures: %d)\n", total_fs_mut, total_fs_fail);
    printf("  IPC mutations:    %d\n", total_ipc_mut);
    printf("  SHM open fails:   %d\n", total_shm_fail);
    printf("  Gen violations:   %d\n", g_gen_violations);

    printf("\nLatency by category (us):\n");
    for (int c = 0; c < LAT_CAT_COUNT; c++) {
        int cat_samples = 0;
        double cat_min = 999999999.0, cat_max = 0.0, cat_total = 0.0;
        for (int i = 0; i < n_workers; i++) {
            LatencyBucket *b = &results[i].buckets[c];
            cat_samples += b->count;
            if (b->count > 0) {
                if (b->min_us < cat_min) cat_min = b->min_us;
                if (b->max_us > cat_max) cat_max = b->max_us;
                cat_total += b->total_us;
            }
        }
        if (cat_samples == 0) {
            printf("  %s: no samples\n", lat_category_name(c));
            continue;
        }
        double *all = malloc(cat_samples * sizeof(double));
        int idx = 0;
        for (int i = 0; i < n_workers; i++) {
            LatencyBucket *b = &results[i].buckets[c];
            memcpy(all + idx, b->samples, b->count * sizeof(double));
            idx += b->count;
        }
        qsort(all, cat_samples, sizeof(double), compare_doubles);
        double avg = cat_total / cat_samples;
        double p50 = all[(int)(cat_samples * 0.50)];
        double p95 = all[(int)(cat_samples * 0.95)];
        double p99 = all[(int)(cat_samples * 0.99)];
        printf("  %s: n=%d min=%.2f max=%.2f avg=%.2f p50=%.2f p95=%.2f p99=%.2f us\n",
               lat_category_name(c), cat_samples, cat_min, cat_max, avg, p50, p95, p99);
        free(all);
    }

    bool service_alive = ipc_service_exists();
    printf("\n  Service alive:    %s\n", service_alive ? "YES" : "NO");

    if (g_crash_flag) {
        printf("\n*** SERVICE CRASH/HANG DETECTED ***\n");
        printf("Context: %s\n", g_crash_context);
    }

    /* Machine-parseable JSON summary for test harness integration */
    printf("\n=== MACHINE PARSEABLE SUMMARY ===\n");
    printf("{\n");
    printf("  \"test\": \"service_race_tester\",\n");
    printf("  \"result\": \"%s\",\n", g_crash_flag ? "CRASH" : (g_gen_violations > 0 ? "GEN_VIOLATION" : (total_fail > 0 ? "PARTIAL" : "PASS")));
    printf("  \"exit_code\": %d,\n", g_crash_flag ? 2 : (g_gen_violations > 0 ? 3 : (total_fail > 0 ? 4 : 0)));
    printf("  \"crash\": %s,\n", g_crash_flag ? "true" : "false");
    printf("  \"gen_violations\": %d,\n", g_gen_violations);
    printf("  \"total_ops\": %d,\n", total_ops);
    printf("  \"total_ok\": %d,\n", total_ok);
    printf("  \"total_fail\": %d,\n", total_fail);
    printf("  \"total_busy\": %d,\n", total_busy);
    printf("  \"total_connect_fail\": %d,\n", total_connect_fail);
    printf("  \"total_fs_mutations\": %d,\n", total_fs_mut);
    printf("  \"total_fs_failures\": %d,\n", total_fs_fail);
    printf("  \"total_ipc_mutations\": %d,\n", total_ipc_mut);
    printf("  \"total_shm_fail\": %d,\n", total_shm_fail);
    printf("  \"latency_by_category\": {\n");
    for (int c = 0; c < LAT_CAT_COUNT; c++) {
        int cat_samples = 0;
        double cat_min = 999999999.0, cat_max = 0.0, cat_total = 0.0;
        for (int i = 0; i < n_workers; i++) {
            LatencyBucket *b = &results[i].buckets[c];
            cat_samples += b->count;
            if (b->count > 0) {
                if (b->min_us < cat_min) cat_min = b->min_us;
                if (b->max_us > cat_max) cat_max = b->max_us;
                cat_total += b->total_us;
            }
        }
        double cat_avg = (cat_samples > 0) ? cat_total / cat_samples : 0.0;
        double cat_p50 = 0.0, cat_p95 = 0.0, cat_p99 = 0.0;
        if (cat_samples > 0) {
            double *all = malloc(cat_samples * sizeof(double));
            int idx = 0;
            for (int i = 0; i < n_workers; i++) {
                LatencyBucket *b = &results[i].buckets[c];
                memcpy(all + idx, b->samples, b->count * sizeof(double));
                idx += b->count;
            }
            qsort(all, cat_samples, sizeof(double), compare_doubles);
            cat_p50 = all[(int)(cat_samples * 0.50)];
            cat_p95 = all[(int)(cat_samples * 0.95)];
            cat_p99 = all[(int)(cat_samples * 0.99)];
            free(all);
        }
        printf("    \"%s\": {\"samples\": %d, \"min_us\": %.2f, \"max_us\": %.2f, \"avg_us\": %.2f, \"p50_us\": %.2f, \"p95_us\": %.2f, \"p99_us\": %.2f}%s\n",
               lat_category_name(c), cat_samples, cat_min, cat_max, cat_avg, cat_p50, cat_p95, cat_p99,
               (c < LAT_CAT_COUNT - 1) ? "," : "");
    }
    printf("  }\n");
    printf("}\n");
    printf("=== END MACHINE PARSEABLE SUMMARY ===\n");

    printf("\n");

    /* Stop service if we started it */
    if (!opts.no_service_mgmt) {
        printf("Stopping service...\n");
        ensure_service_stopped();
    }

    /* Cleanup */
    cleanup_temp_dirs();
    for (int i = 0; i < n_workers; i++) {
        for (int c = 0; c < LAT_CAT_COUNT; c++) {
            free(results[i].buckets[c].samples);
        }
    }
    free(results);
    free(threads);

    /* Determine exit code */
    if (g_crash_flag) {
        printf("RESULT: CRASH DETECTED\n");
        return 2;
    }
    if (g_gen_violations > 0) {
        printf("RESULT: GENERATION MONOTONICITY VIOLATION\n");
        return 3;
    }
    if (total_fail > 0) {
        printf("RESULT: PARTIAL (some operations failed but service survived)\n");
        return 4;
    }
    printf("RESULT: PASS\n");
    return 0;
}
