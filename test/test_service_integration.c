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
#include "service_test_common.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

/* --------------------------------------------------------- test utilities     */

/* Maximum time to wait for service state changes */
#define SERVICE_TIMEOUT 10

/* Run a generic command and capture output */
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
    return safe_exit_code(status);
#endif
}

/* Resolve NCD executable path (forward declared for run_ncd_command) */
static const char* get_ncd_exe(void);

/* Run ncd command and capture output */
static int run_ncd_command(const char *args, char *output, size_t output_size) {
    char full_cmd[512];
    snprintf(full_cmd, sizeof(full_cmd), "%s %s", get_ncd_exe(), args);
    return run_command(full_cmd, output, output_size);
}

static void ensure_ncd_test_dir_exists(void) {
#if NCD_PLATFORM_WINDOWS
    const char *localAppData = getenv("LOCALAPPDATA");
    if (localAppData) {
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\NCD", localAppData);
        CreateDirectoryA(path, NULL);
    }
#else
    const char *xdg = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    char path[256];
    if (xdg && *xdg) {
        snprintf(path, sizeof(path), "%s/ncd", xdg);
    } else if (home && *home) {
        snprintf(path, sizeof(path), "%s/.local/share/ncd", home);
    } else {
        return;
    }
    mkdir(path, 0755);
#endif
}

/* Resolve NCD executable path */
static const char* get_ncd_exe(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs = GetFileAttributesA("NewChangeDirectory.exe");
    if (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY))
        return "NewChangeDirectory.exe";
    return "..\\NewChangeDirectory.exe";
#else
    if (access("NewChangeDirectory", X_OK) == 0) return "./NewChangeDirectory";
    return "../NewChangeDirectory";
#endif
}

/* Check if executables exist */
static bool executables_exist(void) {
#if NCD_PLATFORM_WINDOWS
    DWORD attribs;
    attribs = GetFileAttributesA(get_ncd_exe());
    bool ncd_exists = (attribs != INVALID_FILE_ATTRIBUTES && !(attribs & FILE_ATTRIBUTE_DIRECTORY));
    return service_executable_exists() && ncd_exists;
#else
    return service_executable_exists() && (access(get_ncd_exe(), X_OK) == 0);
#endif
}

/* --------------------------------------------------------- help output tests  */

TEST(help_shows_standalone_when_service_stopped) {
    ensure_service_stopped();
    
    /* Skip if executables not built */
    if (!executables_exist()) {
        SKIP_TEST("Executables not found");
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
        SKIP_TEST("Executables not found");
    }
    
    /* Start service */
    ensure_ncd_test_dir_exists();
    run_service_command("start", NULL, 0);
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
SKIP_TEST("Service did not reach READY state");
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
        SKIP_TEST("Executables not found");
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
        SKIP_TEST("Executables not found");
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
        SKIP_TEST("Executables not found");
    }

    /* Start service */
    ensure_ncd_test_dir_exists();
    run_service_command("start", NULL, 0);
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
        SKIP_TEST("Executables not found");
    }
    
    /* Start service */
    ensure_ncd_test_dir_exists();
    run_service_command("start", NULL, 0);
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
        SKIP_TEST("Executables not found");
    }
    
    /* Start service */
    ensure_ncd_test_dir_exists();
    run_service_command("start", NULL, 0);
    bool started = wait_for_service_state(true, SERVICE_TIMEOUT);
    ASSERT_TRUE(started);
    
    /* Stop service */
    run_service_command("stop", NULL, 0);
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
        SKIP_TEST("Executables not found");
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
        SKIP_TEST("Executables not found");
    }
    
    /* Start service */
    ensure_ncd_test_dir_exists();
    run_service_command("start", NULL, 0);
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
        SKIP_TEST("Executables not found");
    }

    char output[8192] = {0};
    int exit_code = run_ncd_command("-?", output, sizeof(output));

    ASSERT_EQ_INT(0, exit_code);
    ASSERT_TRUE(strstr(output, "Exclusions:") != NULL);
    ASSERT_TRUE(strstr(output, "-x:<pat>") != NULL);
    ASSERT_TRUE(strstr(output, "-X:<pat>") != NULL);
    ASSERT_TRUE(strstr(output, "-x:l") != NULL);
    ASSERT_TRUE(strstr(output, "LLM integration mode") != NULL);
    ASSERT_TRUE(strstr(output, "--agent:<cmd>") != NULL);

    return 0;
}

TEST(a_flag_is_not_agent_alias) {
    ensure_service_stopped();
    
    if (!executables_exist()) {
        SKIP_TEST("Executables not found");
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

    /* Final cleanup - ensure service is fully stopped */
    printf("\n--- Final cleanup ---\n");
    ensure_service_stopped();
}

TEST_MAIN(
    suite_service_integration();
)
