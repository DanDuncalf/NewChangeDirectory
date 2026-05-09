/* service_test_common.h -- Shared service test utilities
 *
 * Extracted from duplicated helpers across:
 *   test_service_integration.c
 *   test_service_lifecycle.c
 *   test_service_ipc.c
 *
 * Provides cross-platform utilities for:
 * - Service executable path resolution
 * - Service command execution with output capture
 * - Service state polling (wait_for_service_state)
 * - Force termination and cleanup
 * - ensure_service_stopped() for test setup/teardown
 */

#ifndef SERVICE_TEST_COMMON_H
#define SERVICE_TEST_COMMON_H

#include "test_framework.h"
#include "../src/control_ipc.h"
#include "../src/ncd.h"
#include "../src/platform.h"

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>
#endif

#include "test_posix_exit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Service command execution
 * ------------------------------------------------------------------ */

/* Resolve path to the service executable.
 * Checks current directory first, then parent directory. */
const char *service_get_executable_path(void);

/* Check if the service executable exists and is executable. */
bool service_executable_exists(void);

/* Run a service command (e.g. "start", "stop", "status").
 * If output is non-NULL, captures stdout/stderr into it.
 * Returns the process exit code, or -1 on failure. */
int run_service_command(const char *cmd, char *output, size_t output_size);

/* Run a service command with extra arguments appended.
 * (e.g. run_service_command_ex("start", "-log2", out, sizeof(out)))
 * On Linux, extra_args is currently ignored (shell wrapper limitation). */
int run_service_command_ex(const char *cmd, const char *extra_args,
                           char *output, size_t output_size);

/* ------------------------------------------------------------------
 * Service state polling
 * ------------------------------------------------------------------ */

/* Poll ipc_service_exists() until it matches expected_running.
 * Returns true once the condition is met or false on timeout. */
bool wait_for_service_state(bool expected_running, int timeout_seconds);

/* Check if the service process is still alive (may outlive IPC pipe).
 * On Windows: checks the named mutex.
 * On Linux: checks PID file + /proc for running NCDService process. */
bool service_process_still_running(void);

/* Wait for the service process to fully exit (mutex released on Windows,
 * process no longer visible on Linux). Call after IPC pipe closes
 * to ensure cleanup is complete. */
void wait_for_service_fully_exited(int timeout_seconds);

/* Timeout for graceful shutdown (seconds) */
#define GRACEFUL_SHUTDOWN_TIMEOUT 3

/* ------------------------------------------------------------------
 * Service lifecycle helpers
 * ------------------------------------------------------------------ */

/* Force-terminate the service process.
 * Uses CreateToolhelp32Snapshot on Windows, pkill/killall on Linux. */
void force_terminate_service(void);

/* Ensure the service is fully stopped.
 * Tries graceful stop first, then force-terminates if needed.
 * Safe to call regardless of current service state. */
void ensure_service_stopped(void);

/* ------------------------------------------------------------------
 * Bounded condition polling
 * ------------------------------------------------------------------ */

/* Poll a condition function until it returns true or timeout expires.
 * Returns true if the condition became true before timeout. */
typedef bool (*wait_condition_fn)(void);
bool wait_until(wait_condition_fn cond, int timeout_ms, int interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* SERVICE_TEST_COMMON_H */
