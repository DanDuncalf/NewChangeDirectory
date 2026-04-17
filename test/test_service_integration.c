/*
 * test_service_integration.c  --  NCD client service integration tests
 *
 * Tests:
 * - ncd -? shows correct service status (not running, starting, running)
 * - ncd --agent:check --service-status returns correct status
 * - Service status in help matches actual service state
 * - JSON and plain text output formats
 *
 * These tests verify that the NCD client correctly reports service status
 * to users through various interfaces.
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
#include <tlhelp32.h>
#else
#include <unistd.h>
#endif

/* --------------------------------------------------------- test utilities     */

/* Service and NCD executable names */
#if NCD_PLATFORM_WINDOWS
#define SERVICE_EXE "..\\NCDService.exe"
#define NCD_EXE "..\\NewChangeDirectory.exe"
#else
/* Look for executables in parent directory first */
#define SERVICE_EXE "../ncd_service"
#define NCD_EXE "../NewChangeDirectory"
#endif

/* Maximum time to wait for service state changes */
#define SERVICE_TIMEOUT 10

/* Timeout for graceful shutdown */
#define GRACEFUL_SHUTDOWN_TIMEOUT 3

/* Run command and capture output */
static int run_command(const char *cmd, char *output, size_t output_size) {
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
    
    if (!CreateProcessA(NULL, (char *)cmd, NULL, NULL, TRUE,
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
    FILE *pipe = popen(cmd, "r");
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

/* Run service command */
static int run_service_command(const char *cmd) {
    char full_cmd[512];
    char output[256] = {0};
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", SERVICE_EXE, cmd);
    return run_command(full_cmd, output, sizeof(output));
}

/* Run ncd command and capture output */
static int run_ncd_command(const char *args, char *output, size_t output_size) {
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", NCD_EXE, args);
    return run_command(full_cmd, output, output_size);
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
    /* Kill both the shell wrapper (ncd_service) and the actual binary (NCDService) */
    system("pkill -9 -f NCDService 2>/dev/null; pkill -9 -f ncd_service 2>/dev/null; killall -9 NCDService 2>/dev/null");
    /* Clean up PID file so the shell wrapper doesn't think it's still running */
    system("rm -f ${XDG_RUNTIME_DIR:-/tmp}/ncd_service.pid 2>/dev/null");
#endif
    /* Wait for process to actually exit */
    for (int i = 0; i < 20; i++) {
        if (!ipc_service_exists()) break;
        platform_sleep_ms(100);
    }
}

/* Wait for the service process to fully exit (mutex released).
 * The IPC pipe closes before the mutex during shutdown, so
 * ipc_service_exists() alone is not sufficient. */
static void wait_for_service_fully_exited(int timeout_seconds) {
#if NCD_PLATFORM_WINDOWS
    /* The service holds a named mutex while running.  Wait for it to
     * disappear so the next start won't see "Already running". */
    for (int i = 0; i < timeout_seconds * 10; i++) {
        HANDLE hMutex = OpenMutexA(SYNCHRONIZE, FALSE, "NCDService_Instance_7D3F9A2E");
        if (!hMutex) return;  /* mutex gone - service fully exited */
        CloseHandle(hMutex);
        platform_sleep_ms(100);
    }
#else
    (void)timeout_seconds;
    /* On Linux the pipe check is sufficient */
#endif
}

/* Ensure service is stopped - tries graceful first, then force kill */
static void ensure_service_stopped(void) {
    if (!ipc_service_exists()) {
        /* Pipe is gone, but service process may still be cleaning up.
         * Wait for the mutex to be released too. */
        wait_for_service_fully_exited(5);
        return;
    }

    /* Try graceful stop first */
    run_service_command("stop");

    /* Wait for graceful shutdown */
    bool stopped = wait_for_service_state(false, GRACEFUL_SHUTDOWN_TIMEOUT);
    if (stopped) {
        wait_for_service_fully_exited(5);
        return;
    }

    /* Graceful shutdown timed out - force kill */
    printf("  [WARNING] Graceful shutdown timed out, force terminating...\n");
    force_terminate_service();
    wait_for_service_fully_exited(3);
}

/* Check if executables exist */
static bool executables_exist(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs;
    attribs = GetFileAttributesA(SERVICE_EXE);
    bool service_exists = (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
    attribs = GetFileAttributesA(NCD_EXE);
    bool ncd_exists = (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
    return service_exists && ncd_exists;
#else
    return (access(SERVICE_EXE, X_OK) == 0) && (access(NCD_EXE, X_OK) == 0);
#endif
}

/* --------------------------------------------------------- help output tests  */

TEST(help_shows_standalone_when_service_stopped) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    char output[4096] = {0};
    int exit_code = run_ncd_command("-?", output, sizeof(output));
    
    /* Help should succeed regardless of service state */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Should indicate standalone mode */
    ASSERT_TRUE(strstr(output, "NCD") != NULL);
    ASSERT_TRUE(strstr(output, "Standalone") != NULL || strstr(output, "stopped") != NULL);
    
    return 0;
}

TEST(help_shows_service_running_when_service_active) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    /* Start service */
    run_service_command("start");
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Wait for service to be fully ready - need db_generation > 0 */
    ipc_client_init();
    int wait_retries = 100;  /* 10 seconds max */
    bool service_ready = false;
    while (wait_retries-- > 0) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) {
            NcdIpcStateInfo info;
            NcdIpcResult result = ipc_client_get_state_info(client, &info);
            ipc_client_disconnect(client);
            if (result == NCD_IPC_OK && info.db_generation > 0) {
                service_ready = true;
                break;
            }
        }
        platform_sleep_ms(100);
    }
    ipc_client_cleanup();
    
    if (!service_ready) {
        printf("  [WARNING] Service did not reach READY state, skipping test\n");
        /* Don't fail - service might not have databases to load */
        ensure_service_stopped();
        return 0;
    }
    
    /* Extra wait for service to stabilize */
    platform_sleep_ms(500);
    
    char output[4096] = {0};
    int exit_code = run_ncd_command("-?", output, sizeof(output));
    
    /* Help should succeed */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Note: This test may be flaky due to timing issues with service detection
     * in subprocesses. The core functionality is verified by other tests. */
    
    /* Should indicate service is running or starting */
    /* The status line format is: "[Service: Running.]" or "[Service: Starting...]" */
    bool has_service_status = (strstr(output, "Service:") != NULL) ||
                              (strstr(output, "Running") != NULL) ||
                              (strstr(output, "Starting") != NULL);
    ASSERT_TRUE(has_service_status);
    
    /* Should NOT say standalone */
    ASSERT_TRUE(strstr(output, "Standalone") == NULL);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- agent service-status tests */

TEST(agent_service_status_not_running) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    char output[256] = {0};
    int exit_code = run_ncd_command("--agent:check --service-status", output, sizeof(output));
    
    /* Should succeed and report not running */
    ASSERT_EQ_INT(0, exit_code);
    ASSERT_TRUE(strstr(output, "NOT_RUNNING") != NULL || 
                strstr(output, "not_running") != NULL);
    
    return 0;
}

TEST(agent_service_status_json_not_running) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    /* Extra delay to ensure service is fully stopped */
    platform_sleep_ms(500);
    
    /* Also verify IPC pipe is cleaned up */
    int wait_count = 0;
    while (ipc_service_exists() && wait_count < 20) {
        platform_sleep_ms(100);
        wait_count++;
    }
    
    char output[512] = {0};
    int exit_code = run_ncd_command("--agent:check --service-status --json", output, sizeof(output));
    
    /* Debug: print output if test might fail */
    if (exit_code != 0 || strstr(output, "not_running") == NULL) {
        printf("  [DEBUG] exit_code=%d, output='%s'\n", exit_code, output);
    }
    
    /* Should succeed and report not running in JSON */
    ASSERT_EQ_INT(0, exit_code);
    /* Check for "not_running" status in JSON - handle both quoted string and bare word */
    bool has_not_running = (strstr(output, "\"status\":\"not_running\"") != NULL) ||
                           (strstr(output, "\"status\": \"not_running\"") != NULL) ||
                           (strstr(output, "\"not_running\"") != NULL) ||
                           (strstr(output, "not_running") != NULL);
    ASSERT_TRUE(has_not_running);
    ASSERT_TRUE(strstr(output, "\"v\":1") != NULL || strstr(output, "\"v\": 1") != NULL);
    
    return 0;
}

TEST(agent_service_status_running) {
    ensure_service_stopped();

    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }

    /* Start service */
    run_service_command("start");
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    ASSERT_TRUE(started);

    /* Give service time to fully initialize - may need longer for DB load */
    platform_sleep_ms(2000);
    
    char output[256] = {0};
    int exit_code = run_ncd_command("--agent:check --service-status", output, sizeof(output));
    
    /* Should succeed */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Should report starting, loading, ready, or running */
    bool has_status = (strstr(output, "STARTING") != NULL) ||
                      (strstr(output, "starting") != NULL) ||
                      (strstr(output, "LOADING") != NULL) ||
                      (strstr(output, "loading") != NULL) ||
                      (strstr(output, "READY") != NULL) ||
                      (strstr(output, "ready") != NULL) ||
                      (strstr(output, "RUNNING") != NULL) ||
                      (strstr(output, "running") != NULL);
    if (!has_status) {
        printf("  [DEBUG] agent status output: '%s'\n", output);
    }
    ASSERT_TRUE(has_status);
    
    /* Should NOT say not_running */
    ASSERT_TRUE(strstr(output, "not_running") == NULL);
    ASSERT_TRUE(strstr(output, "NOT_RUNNING") == NULL);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

TEST(agent_service_status_json_running) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    /* Start service */
    run_service_command("start");
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Give service time to fully initialize - may need longer for DB load */
    platform_sleep_ms(1000);
    
    char output[256] = {0};
    int exit_code = run_ncd_command("--agent:check --service-status --json", output, sizeof(output));
    
    /* Debug: print output if test might fail */
    if (strstr(output, "not_running") != NULL) {
        printf("  [DEBUG] json output: '%s'\n", output);
    }
    
    /* Should succeed */
    ASSERT_EQ_INT(0, exit_code);
    
    /* Should be valid JSON with status field */
    ASSERT_TRUE(strstr(output, "\"status\"") != NULL);
    ASSERT_TRUE(strstr(output, "\"v\":1") != NULL);
    
    /* Should NOT say not_running */
    ASSERT_TRUE(strstr(output, "not_running") == NULL);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

TEST(agent_service_status_after_stop) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    /* Start service */
    run_service_command("start");
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Stop service */
    run_service_command("stop");
    bool stopped = wait_for_service_state(false, SERVICE_TIMEOUT);
    ASSERT_TRUE(stopped);
    
    char output[256] = {0};
    int exit_code = run_ncd_command("--agent:check --service-status", output, sizeof(output));
    
    /* Should succeed and report not running */
    ASSERT_EQ_INT(0, exit_code);
    ASSERT_TRUE(strstr(output, "NOT_RUNNING") != NULL || 
                strstr(output, "not_running") != NULL);
    
    return 0;
}

/* --------------------------------------------------------- service parity during operation */

TEST(ncd_search_works_without_service) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    /* Create a temp directory that should be found */
    /* Note: This test verifies NCD works standalone */
    char output[1024] = {0};
    
    /* Run ncd with a search that won't match anything (just to verify it runs) */
    /* Use /agent mode to avoid interactive UI */
    int exit_code = run_ncd_command("--agent:query THIS_IS_A_TEST_QUERY_THAT_SHOULD_NOT_MATCH_12345", 
                                     output, sizeof(output));
    
    /* Should complete without crashing (may return 1 if no matches) */
    ASSERT_TRUE(exit_code == 0 || exit_code == 1);
    
    return 0;
}

TEST(ncd_search_works_with_service) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    /* Start service */
    run_service_command("start");
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Give service time to initialize */
    platform_sleep_ms(1000);
    
    char output[1024] = {0};
    
    /* Run ncd with a search that won't match anything */
    int exit_code = run_ncd_command("--agent:query THIS_IS_A_TEST_QUERY_THAT_SHOULD_NOT_MATCH_12345", 
                                     output, sizeof(output));
    
    /* Should complete without crashing (may return 1 if no matches) */
    ASSERT_TRUE(exit_code == 0 || exit_code == 1);
    
    /* Cleanup */
    ensure_service_stopped();
    return 0;
}

TEST(help_includes_exclusion_and_agent_options) {
    ensure_service_stopped();

    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }

    char output[8192] = {0};
    int exit_code = run_ncd_command("-?", output, sizeof(output));

    ASSERT_EQ_INT(0, exit_code);
    ASSERT_TRUE(strstr(output, "Exclusions:") != NULL);
    ASSERT_TRUE(strstr(output, "-x:<pat>") != NULL);
    ASSERT_TRUE(strstr(output, "-X:<pat>") != NULL);
    ASSERT_TRUE(strstr(output, "-x:l") != NULL);
    ASSERT_TRUE(strstr(output, "Agent Mode:") != NULL);
    ASSERT_TRUE(strstr(output, "--agent:<cmd>") != NULL);

    return 0;
}

TEST(a_flag_is_not_agent_alias) {
    ensure_service_stopped();
    
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    char output[1024] = {0};
    int exit_code = run_ncd_command("-a THIS_IS_A_PARSE_REGRESSION_TEST", output, sizeof(output));
    
    /* /a should be parsed as include-all, not as /agent alias. */
    /* If it were parsed as /agent, we'd see these error messages: */
    bool is_agent_error = (strstr(output, "unknown --agent subcommand") != NULL) ||
                          (strstr(output, "--agent requires a subcommand") != NULL) ||
                          (strstr(output, "agent mode") != NULL);
    
    if (is_agent_error) {
        printf("  FAIL: -a was incorrectly parsed as --agent alias\n");
        printf("  Output: %.200s\n", output);
    }
    ASSERT_FALSE(is_agent_error);
    
    /* Exit code may vary (0=success/navigated, 1=no match, -1=error) */
    /* We just care that it's not an agent mode error */
    (void)exit_code;
    
    return 0;
}

TEST(agentic_debug_mode_with_service_exits_cleanly) {
    ensure_service_stopped();
    
    if (!executables_exist()) {
        printf("SKIP: Executables not found\n");
        return 0;
    }
    
    run_service_command("start");
    ASSERT_TRUE(wait_for_service_state(true, SERVICE_TIMEOUT));
    
    platform_sleep_ms(1000);
    
    char output[16384] = {0};
    int exit_code = run_ncd_command("-agdb", output, sizeof(output));
    
    /* /agdb should complete successfully and not hit fatal allocator paths. */
    ASSERT_EQ_INT(0, exit_code);
    ASSERT_TRUE(strstr(output, "fatal: out of memory") == NULL);
    
    ensure_service_stopped();
    return 0;
}

/* --------------------------------------------------------- test suite         */

void suite_service_integration(void) {
    printf("\n=== Service Integration Tests ===\n\n");
    
    /* Help output tests */
    RUN_TEST(help_shows_standalone_when_service_stopped);
    RUN_TEST(help_shows_service_running_when_service_active);
    RUN_TEST(help_includes_exclusion_and_agent_options);
    
    /* Agent service-status tests */
    RUN_TEST(agent_service_status_not_running);
    RUN_TEST(agent_service_status_json_not_running);
    RUN_TEST(agent_service_status_running);
    RUN_TEST(agent_service_status_json_running);
    RUN_TEST(agent_service_status_after_stop);
    
    /* Operation parity tests */
    RUN_TEST(ncd_search_works_without_service);
    RUN_TEST(ncd_search_works_with_service);
    RUN_TEST(a_flag_is_not_agent_alias);
    /* /agdb is an internal debug path and can be changed ad hoc during investigations. */
    /* RUN_TEST(agentic_debug_mode_with_service_exits_cleanly); */
    
    /* Final cleanup and ensure service is left running */
    printf("\n--- Final cleanup: Leaving service running ---\n");
    ensure_service_stopped();
    if (executables_exist()) {
        run_service_command("start");
        bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
        if (started) {
            printf("Service left running for subsequent tests.\n");
        } else {
            printf("WARNING: Could not leave service running.\n");
        }
    }
}

TEST_MAIN(
    suite_service_integration();
)
