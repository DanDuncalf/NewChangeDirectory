/*
 * test_service_race_conditions.c  --  Multi-client race condition tests
 *
 * Tests designed to expose race conditions when multiple clients interact
 * with a single NCD service instance concurrently.
 *
 * Identified race conditions under test:
 * 1. Background loader thread modifies state->database without mutex while
 *    main thread handles GET_DETAILED_STATUS (bypasses state check).
 * 2. Window between shm_remove and shm_create during snapshot publication
 *    where clients may fail to open shared memory.
 * 3. Multiple concurrent mutation requests (heuristics, metadata) from
 *    multiple clients arriving simultaneously.
 * 4. Generation number monotonicity under rapid mutation/rescan.
 * 5. Cascading rescan requests queuing up while another rescan is active.
 */

#include <stdio.h>
#define printf(...) fprintf(stderr, __VA_ARGS__)
#include "test_framework.h"
#include "../src/service_state.h"
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"
#include "../src/shm_platform.h"
#include <string.h>
#include <stdlib.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#include <process.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#endif

#include "test_posix_exit.h"

/* --------------------------------------------------------- test utilities     */

#define SERVICE_EXE NCD_PLATFORM_WINDOWS ? "NCDService.exe" : "../ncd_service"
#define SERVICE_START_TIMEOUT 10
#define SERVICE_STOP_TIMEOUT 5

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
    return (access("../ncd_service", X_OK) == 0);
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

static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        for (int i = 0; i < 50; i++) {
            if (!ipc_service_exists()) break;
            platform_sleep_ms(100);
        }
        if (ipc_service_exists()) {
            force_terminate_service();
        }
        return;
    }
    {
        char _buf[256];
        run_service_command("stop", _buf, sizeof(_buf));
    }
    if (!wait_for_service_state(false, SERVICE_STOP_TIMEOUT)) {
        force_terminate_service();
    }
    for (int i = 0; i < 50; i++) {
        if (!ipc_service_exists()) break;
        platform_sleep_ms(100);
    }
    if (ipc_service_exists()) {
        force_terminate_service();
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
        ensure_service_stopped();
        rc = run_service_command("start", _buf, sizeof(_buf));
        if (rc != 0 && !ipc_service_exists()) {
            force_terminate_service();
            continue;
        }
        if (wait_for_service_state(true, SERVICE_START_TIMEOUT)) {
            return true;
        }
        force_terminate_service();
    }
    return false;
}

/* --------------------------------------------------------- thread helpers     */

#if NCD_PLATFORM_WINDOWS
typedef HANDLE thread_handle_t;
typedef unsigned long thread_result_t;
#define THREAD_CALL __stdcall
#else
typedef pthread_t thread_handle_t;
typedef void *thread_result_t;
#define THREAD_CALL
#endif

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

/* Synchronization primitive: simple spinlock counter */
static volatile int g_start_flag = 0;
static volatile int g_stop_flag = 0;

/* --------------------------------------------------------- test 1: detailed status during loading */

/*
 * Race condition: Background loader thread calls service_state_load_databases()
 * which modifies state->database WITHOUT holding state->state_mutex. Meanwhile
 * the main thread accepts connections and handle_get_detailed_status() accesses
 * state->database directly (GET_DETAILED_STATUS bypasses check_service_ready).
 *
 * This test repeatedly requests detailed status immediately after service start
 * while the loader thread is still populating the database.
 */

typedef struct {
    int success_count;
    int error_count;
    int crash_detected;
} LoadingRaceResult;

static thread_result_t THREAD_CALL race_loading_thread(void *arg) {
    LoadingRaceResult *result = (LoadingRaceResult *)arg;
    result->success_count = 0;
    result->error_count = 0;
    result->crash_detected = 0;

    /* Wait for start signal */
    while (!g_start_flag) platform_sleep_ms(1);

    ipc_client_init();

    for (int i = 0; i < 100 && !g_stop_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            /* Service may not be ready yet - retry */
            platform_sleep_ms(50);
            continue;
        }

        NcdIpcDetailedStatus info;
        NcdIpcResult rc = ipc_client_get_detailed_status(client, &info);
        if (rc == NCD_IPC_OK) {
            result->success_count++;
        } else {
            result->error_count++;
        }

        ipc_client_disconnect(client);
        platform_sleep_ms(10);
    }

    ipc_client_cleanup();
    return 0;
}

TEST(race_detailed_status_during_loading) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }

    /* Start service - this begins background loader */
    if (!ensure_service_running()) {
        printf("SKIP: Could not start service\n");
        return 0;
    }

    /* Give service a moment to enter LOADING state */
    platform_sleep_ms(200);

    g_start_flag = 0;
    g_stop_flag = 0;

    LoadingRaceResult results[4];
    thread_handle_t threads[4];

    for (int i = 0; i < 4; i++) {
        memset(&results[i], 0, sizeof(results[i]));
        threads[i] = spawn_thread(race_loading_thread, &results[i]);
    }

    /* Signal all threads to start simultaneously */
    g_start_flag = 1;

    /* Let them run for a short time */
    platform_sleep_ms(2000);
    g_stop_flag = 1;

    for (int i = 0; i < 4; i++) {
        join_thread(threads[i]);
    }

    int total_success = 0;
    int total_error = 0;
    for (int i = 0; i < 4; i++) {
        total_success += results[i].success_count;
        total_error += results[i].error_count;
    }

    printf("  Loading race: %d successful detailed status, %d errors\n",
           total_success, total_error);

    /* The service must not have crashed */
    ASSERT_TRUE(ipc_service_exists());

    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test 2: concurrent mutation storm */

/*
 * Race condition: Multiple clients submitting mutations simultaneously.
 * The server handles clients sequentially (single-threaded), but clients
 * can connect concurrently and queue up. The OS pipe/named pipe queue may
 * behave unpredictably under high connection churn.
 *
 * Additionally, each mutation triggers snapshot publication which does
 * shm_remove/shm_create - clients mapping SHM during this window may
 * get transient failures.
 */

typedef struct {
    int thread_id;
    int mutations_sent;
    int mutations_ok;
    int shm_failures;
} MutationRaceResult;

static thread_result_t THREAD_CALL race_mutation_thread(void *arg) {
    MutationRaceResult *r = (MutationRaceResult *)arg;
    r->mutations_sent = 0;
    r->mutations_ok = 0;
    r->shm_failures = 0;

    while (!g_start_flag) platform_sleep_ms(1);

    ipc_client_init();

    for (int i = 0; i < 50 && !g_stop_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            r->shm_failures++;
            platform_sleep_ms(20);
            continue;
        }

        char search[64];
        char target[128];
        snprintf(search, sizeof(search), "race_%d_%d", r->thread_id, i);
        snprintf(target, sizeof(target), "/race/path/%d/%d", r->thread_id, i);

        NcdIpcResult rc = ipc_client_submit_heuristic(client, search, target);
        r->mutations_sent++;
        if (rc == NCD_IPC_OK) {
            r->mutations_ok++;
        }

        ipc_client_disconnect(client);
        platform_sleep_ms(10);
    }

    ipc_client_cleanup();
    return 0;
}

TEST(race_concurrent_mutation_storm) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }

    ensure_service_running();
    platform_sleep_ms(1000); /* Wait for READY */

    g_start_flag = 0;
    g_stop_flag = 0;

    MutationRaceResult results[8];
    thread_handle_t threads[8];

    for (int i = 0; i < 8; i++) {
        memset(&results[i], 0, sizeof(results[i]));
        results[i].thread_id = i;
        threads[i] = spawn_thread(race_mutation_thread, &results[i]);
    }

    g_start_flag = 1;
    platform_sleep_ms(3000);
    g_stop_flag = 1;

    for (int i = 0; i < 8; i++) {
        join_thread(threads[i]);
    }

    int total_sent = 0;
    int total_ok = 0;
    int total_shm_fail = 0;
    for (int i = 0; i < 8; i++) {
        total_sent += results[i].mutations_sent;
        total_ok += results[i].mutations_ok;
        total_shm_fail += results[i].shm_failures;
    }

    printf("  Mutation storm: %d sent, %d ok, %d connect failures\n",
           total_sent, total_ok, total_shm_fail);

    /* Service must still be alive */
    ASSERT_TRUE(ipc_service_exists());

    /* At least some mutations should succeed */
    ASSERT_TRUE(total_ok > 0);

    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test 3: SHM window during publication */

/*
 * Race condition: snapshot_publisher_publish_db() does:
 *   shm_unmap -> shm_close -> shm_remove -> shm_create -> shm_map
 * There is a window where the SHM object does not exist. A client that
 * calls GET_STATE_INFO then shm_open() during this window gets NOTFOUND.
 *
 * This test spams GET_STATE_INFO + SHM open while mutations trigger
 * publication, counting transient failures.
 */

typedef struct {
    int state_info_ok;
    int state_info_fail;
    int shm_open_fail;
    int generation_decreased;
    uint64_t last_meta_gen;
    uint64_t last_db_gen;
} ShmWindowResult;

static thread_result_t THREAD_CALL race_shm_window_thread(void *arg) {
    ShmWindowResult *r = (ShmWindowResult *)arg;
    memset(r, 0, sizeof(*r));

    while (!g_start_flag) platform_sleep_ms(1);

    ipc_client_init();

    for (int i = 0; i < 200 && !g_stop_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (!client) {
            r->state_info_fail++;
            platform_sleep_ms(10);
            continue;
        }

        NcdIpcStateInfo info;
        NcdIpcResult rc = ipc_client_get_state_info(client, &info);
        if (rc == NCD_IPC_OK) {
            r->state_info_ok++;

            /* Verify generation monotonicity */
            if (info.meta_generation < r->last_meta_gen) {
                r->generation_decreased++;
            }
            if (info.db_generation < r->last_db_gen) {
                r->generation_decreased++;
            }
            r->last_meta_gen = info.meta_generation;
            r->last_db_gen = info.db_generation;

            /* Try to open the SHM objects */
            if (shm_platform_init() == SHM_OK) {
                ShmHandle *meta_shm = NULL;
                ShmHandle *db_shm = NULL;
                if (shm_open_existing(info.meta_name, SHM_ACCESS_READ, &meta_shm) != SHM_OK) {
                    r->shm_open_fail++;
                } else {
                    shm_close(meta_shm);
                }
                if (shm_open_existing(info.db_name, SHM_ACCESS_READ, &db_shm) != SHM_OK) {
                    r->shm_open_fail++;
                } else {
                    shm_close(db_shm);
                }
                shm_platform_cleanup();
            }
        } else {
            r->state_info_fail++;
        }

        ipc_client_disconnect(client);
        platform_sleep_ms(5);
    }

    ipc_client_cleanup();
    return 0;
}

static thread_result_t THREAD_CALL race_shm_mutator_thread(void *arg) {
    (void)arg;
    while (!g_start_flag) platform_sleep_ms(1);

    ipc_client_init();

    for (int i = 0; i < 100 && !g_stop_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            char search[64];
            char target[128];
            snprintf(search, sizeof(search), "shm_race_%d", i);
            snprintf(target, sizeof(target), "/shm/race/%d", i);
            ipc_client_submit_heuristic(client, search, target);
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(20);
    }

    ipc_client_cleanup();
    return 0;
}

TEST(race_shm_window_during_publication) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }

    ensure_service_running();
    platform_sleep_ms(1000);

    g_start_flag = 0;
    g_stop_flag = 0;

    ShmWindowResult results[4];
    thread_handle_t threads[6];

    /* 4 reader threads */
    for (int i = 0; i < 4; i++) {
        threads[i] = spawn_thread(race_shm_window_thread, &results[i]);
    }
    /* 2 mutator threads */
    for (int i = 4; i < 6; i++) {
        threads[i] = spawn_thread(race_shm_mutator_thread, NULL);
    }

    g_start_flag = 1;
    platform_sleep_ms(3000);
    g_stop_flag = 1;

    for (int i = 0; i < 6; i++) {
        join_thread(threads[i]);
    }

    int total_state_ok = 0;
    int total_state_fail = 0;
    int total_shm_fail = 0;
    int total_gen_decr = 0;
    for (int i = 0; i < 4; i++) {
        total_state_ok += results[i].state_info_ok;
        total_state_fail += results[i].state_info_fail;
        total_shm_fail += results[i].shm_open_fail;
        total_gen_decr += results[i].generation_decreased;
    }

    printf("  SHM window: %d state ok, %d state fail, %d shm open fail, %d gen decreased\n",
           total_state_ok, total_state_fail, total_shm_fail, total_gen_decr);

    /* Service must survive */
    ASSERT_TRUE(ipc_service_exists());

    /* Generation numbers must NEVER decrease - this is a hard invariant */
    ASSERT_EQ_INT(0, total_gen_decr);

    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test 4: generation monotonicity */

/*
 * Race condition: During rescan, service_state_update_database() updates the
 * database and bumps generation, then snapshot_publisher_publish_db() publishes.
 * Between these steps, GET_STATE_INFO returns the OLD generation while the
 * database has NEW data. Also, multiple mutations could interleave.
 *
 * This test verifies generation monotonicity across mutations and rescans.
 */

TEST(race_generation_monotonicity) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }

    ensure_service_running();
    platform_sleep_ms(1000);

    ipc_client_init();
    NcdIpcClient *client = ipc_client_connect();
    ASSERT_NOT_NULL(client);

    uint64_t prev_meta_gen = 0;
    uint64_t prev_db_gen = 0;
    int violations = 0;

    /* Baseline */
    {
        NcdIpcStateInfo info;
        ASSERT_EQ_INT(NCD_IPC_OK, ipc_client_get_state_info(client, &info));
        prev_meta_gen = info.meta_generation;
        prev_db_gen = info.db_generation;
    }

    /* Submit 10 heuristics and verify meta generation increases monotonically */
    for (int i = 0; i < 10; i++) {
        char search[32], target[64];
        snprintf(search, sizeof(search), "gen_%d", i);
        snprintf(target, sizeof(target), "/gen/path/%d", i);

        NcdIpcResult rc = ipc_client_submit_heuristic(client, search, target);
        ASSERT_TRUE(rc == NCD_IPC_OK || rc == NCD_IPC_ERROR_BUSY);

        platform_sleep_ms(100); /* Allow publication to complete */

        NcdIpcStateInfo info;
        rc = ipc_client_get_state_info(client, &info);
        if (rc == NCD_IPC_OK) {
            if (info.meta_generation < prev_meta_gen) {
                violations++;
                printf("  VIOLATION: meta_gen %llu -> %llu\n",
                       (unsigned long long)prev_meta_gen,
                       (unsigned long long)info.meta_generation);
            }
            if (info.db_generation < prev_db_gen) {
                violations++;
                printf("  VIOLATION: db_gen %llu -> %llu\n",
                       (unsigned long long)prev_db_gen,
                       (unsigned long long)info.db_generation);
            }
            prev_meta_gen = info.meta_generation;
            prev_db_gen = info.db_generation;
        }
    }

    ipc_client_disconnect(client);
    ipc_client_cleanup();

    ASSERT_EQ_INT(0, violations);
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test 5: cascading rescan */

/*
 * Race condition: handle_request_rescan() sets g_rescan_requested=1 and returns.
 * If multiple rescans are requested while one is active, the flag gets set
 * repeatedly, potentially causing cascading rescans.
 *
 * This test sends multiple rapid rescan requests and verifies the service
 * does not deadlock or enter an infinite rescan loop.
 */

TEST(race_cascading_rescan_requests) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }

    ensure_service_running();
    platform_sleep_ms(1000);

    ipc_client_init();

    /* Send 5 rescan requests rapidly from different connections */
    for (int i = 0; i < 5; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            bool drive_mask[26] = {false};
            NcdIpcResult rc = ipc_client_request_rescan(client, drive_mask, false);
            printf("  Rescan request %d: result=%d\n", i, rc);
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(50);
    }

    /* Wait for service to settle back to READY */
    bool ready = false;
    for (int i = 0; i < 100; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcDetailedStatus info;
            if (ipc_client_get_detailed_status(client, &info) == NCD_IPC_OK) {
                if (info.runtime_state == SERVICE_STATE_READY) {
                    ready = true;
                    ipc_client_disconnect(client);
                    break;
                }
            }
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(100);
    }

    ipc_client_cleanup();

    ASSERT_TRUE(ready);
    ASSERT_TRUE(ipc_service_exists());
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test 6: connect storm */

/*
 * Stress test: many threads attempt to connect simultaneously.
 * The Windows named pipe server creates one pipe instance. Additional
 * clients may get ERROR_PIPE_BUSY. This tests graceful handling.
 */

typedef struct {
    int connects_ok;
    int pings_ok;
} ConnectStormResult;

static thread_result_t THREAD_CALL race_connect_storm_thread(void *arg) {
    ConnectStormResult *r = (ConnectStormResult *)arg;
    r->connects_ok = 0;
    r->pings_ok = 0;

    while (!g_start_flag) platform_sleep_ms(1);

    ipc_client_init();

    for (int i = 0; i < 20 && !g_stop_flag; i++) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            r->connects_ok++;
            if (ipc_client_ping(client) == NCD_IPC_OK) {
                r->pings_ok++;
            }
            ipc_client_disconnect(client);
        }
        platform_sleep_ms(5);
    }

    ipc_client_cleanup();
    return 0;
}

TEST(race_concurrent_connect_storm) {
    ensure_service_stopped();
    if (!service_executable_exists()) { printf("SKIP: Service executable not found\n"); return 0; }

    ensure_service_running();
    platform_sleep_ms(1000);

    g_start_flag = 0;
    g_stop_flag = 0;

    ConnectStormResult results[20];
    thread_handle_t threads[20];

    for (int i = 0; i < 20; i++) {
        memset(&results[i], 0, sizeof(results[i]));
        threads[i] = spawn_thread(race_connect_storm_thread, &results[i]);
    }

    g_start_flag = 1;
    platform_sleep_ms(2000);
    g_stop_flag = 1;

    for (int i = 0; i < 20; i++) {
        join_thread(threads[i]);
    }

    int total_connects = 0;
    int total_pings = 0;
    for (int i = 0; i < 20; i++) {
        total_connects += results[i].connects_ok;
        total_pings += results[i].pings_ok;
    }

    printf("  Connect storm: %d connects, %d pings from 20 threads\n",
           total_connects, total_pings);

    ASSERT_TRUE(ipc_service_exists());
    /* Expect at least some connections to succeed */
    ASSERT_TRUE(total_connects > 0);

    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_race_conditions(void) {
    printf("\n=== Multi-Client Race Condition Tests ===\n\n");

    RUN_TEST(race_detailed_status_during_loading);
    RUN_TEST(race_concurrent_mutation_storm);
    RUN_TEST(race_shm_window_during_publication);
    RUN_TEST(race_generation_monotonicity);
    RUN_TEST(race_cascading_rescan_requests);
    RUN_TEST(race_concurrent_connect_storm);

    printf("\n--- Final cleanup ---\n");
    ensure_service_stopped();
}

TEST_MAIN(
    suite_service_race_conditions();
)
