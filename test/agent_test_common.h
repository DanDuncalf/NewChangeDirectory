/* agent_test_common.h -- Shared agent test utilities
 *
 * Extracted from duplicated helpers across:
 *   test_agent_ls_check.c
 *   test_agent_query_tree.c
 *   test_agent_complete_mkdir_quit.c
 *   test_agent_integration.c
 *
 * Provides cross-platform utilities for:
 * - Temp directory creation and cleanup
 * - NCD executable discovery
 * - Database path building
 * - Agent subprocess execution
 * - Filesystem tree setup for tests
 */

#ifndef AGENT_TEST_COMMON_H
#define AGENT_TEST_COMMON_H

#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "../src/platform.h"

#if NCD_PLATFORM_WINDOWS
#include <windows.h>
#include <direct.h>
#include <io.h>
#define AGENT_MKDIR(path, mode) _mkdir(path)
#define AGENT_RMDIR(path)       _rmdir(path)
#define AGENT_ACCESS(path, mode) _access(path, mode)
#define AGENT_F_OK 0
#define AGENT_X_OK 0
#define AGENT_POPEN  _popen
#define AGENT_PCLOSE _pclose
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#define AGENT_MKDIR(path, mode) mkdir(path, mode)
#define AGENT_RMDIR(path)       rmdir(path)
#define AGENT_ACCESS(path, mode) access(path, mode)
#define AGENT_F_OK F_OK
#define AGENT_X_OK X_OK
#define AGENT_POPEN  popen
#define AGENT_PCLOSE pclose
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 * Temp directory helpers
 * ------------------------------------------------------------------ */

/* Build a temp directory path under %TEMP% (Windows) or /tmp (Linux).
 * The suffix is appended to create a unique name per test file. */
void agent_get_temp_dir(char *buf, size_t size, const char *suffix);

/* Build an NCD data directory path inside a base directory. */
void agent_build_ncd_dir(char *buf, size_t size, const char *base);

/* Recursively remove a directory tree.
 * Uses rmdir /s /q on Windows, rm -rf on Linux. */
void agent_rm_rf(const char *path);

/* Check if a directory exists. */
bool agent_dir_exists(const char *path);

/* ------------------------------------------------------------------
 * NCD executable discovery
 * ------------------------------------------------------------------ */

/* Find the NCD executable, checking parent dir first, then current dir.
 * Returns NULL if not found. */
const char *agent_find_exe(void);

/* ------------------------------------------------------------------
 * Agent subprocess execution
 * ------------------------------------------------------------------ */

/* Build a database file path for a given drive letter and NCD dir. */
void agent_build_db_path(char *buf, size_t size, const char *ncd_dir, char drive_letter);

/* Run an agent command via the NCD executable.
 * Sets LOCALAPPDATA/XDG_DATA_HOME to ncd_dir and NCD_TEST_MODE=1.
 * Captures output and returns exit code (or -1 on error). */
int agent_run(const char *ncd_dir, const char *agent_args, char *out, size_t out_size,
              const char *db_path);

/* Kill any lingering NCDService processes. Safe cleanup before tests. */
void agent_kill_any_service(void);

/* ------------------------------------------------------------------
 * Test database helpers
 * ------------------------------------------------------------------ */

/* Get the test drive letter (C: on Windows, detected on Linux). */
char agent_test_drive_letter(void);

/* Build the root path for the test drive. */
void agent_build_test_drive_root(char *buf, size_t size);

/* Create a test database with a known directory tree structure. */
bool agent_create_test_db(const char *ncd_dir, char drive_letter);

/* Create a filesystem directory tree for ls/check tests. */
void agent_make_fs_tree(const char *base);

/* Macro: set up a temp directory + NCD dir + test database.
 * Usage: AGENT_SETUP_DB(base_buf, ncd_buf, "suffix");
 * Defines base and ncd_dir buffers and initializes the test environment. */
#define AGENT_SETUP_DB(base, ncd_dir, suffix) do { \
    agent_get_temp_dir(base, sizeof(base), suffix); \
    agent_build_ncd_dir(ncd_dir, sizeof(ncd_dir), base); \
    agent_rm_rf(base); \
    AGENT_MKDIR(base, 0755); \
    AGENT_MKDIR(ncd_dir, 0755); \
    ASSERT_TRUE(agent_create_test_db(ncd_dir, agent_test_drive_letter())); \
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* AGENT_TEST_COMMON_H */
