# Dead Code & Test-Unreachable Code Analysis

This document catalogs code that is either dead (never called), effectively unreachable
in automated tests, or structurally difficult to cover.

---

## 1. Structurally Untestable Code (TUI / Interactive)

These functions require a live terminal with cursor positioning, keyboard input, and
screen rendering. They cannot be exercised by the current test framework without a
terminal emulator or significant refactoring.

### `ui.c` — ~2200 lines, 0% testable with current framework

| Function | Lines (approx) | Reason |
|----------|---------------|--------|
| `ui_select_match()` | ~170 | Reads keyboard, draws to console |
| `ui_select_match_ex()` | ~170 | Same — extended variant |
| `ui_select_history()` | ~120 | History browser with delete support |
| `ui_navigate_directory()` | ~35 | Wrapper around `navigate_run()` |
| `navigate_run()` | ~100 | Full navigator loop |
| `ui_select_drives_for_update()` | ~180 | Drive selection TUI |
| `ui_edit_config()` | ~240 | Config editor TUI |
| `ui_edit_exclusions()` | ~100 | Exclusion editor TUI |
| All `draw_*` / `con_*` / `read_key` | ~500 | Internal rendering and input |
| `list_subdirs()` | ~50 | Directory listing for navigator |
| `NameList` helpers | ~50 | Support for navigator lists |

**Recommendation:** The integration test scripts (`test_features.bat`, `test_features.sh`)
partially cover these by piping keystrokes (Escape/Enter) and checking for crashes. To
improve, consider extracting the *logic* (filtering, selection index management, scroll
clamping) from the *rendering* into testable pure functions:
- `apply_filter()` — already exists as static, could be exposed
- `clamp_scroll()` — already exists as static, could be exposed
- `nav_clamp_scroll()` — could be exposed

### `main.c` — Console I/O helpers (~100 lines)

| Function | Reason |
|----------|--------|
| `con_init()` / `con_close()` | Opens/closes direct console handle |
| `ncd_print()` / `ncd_println()` / `ncd_printf()` | Direct console output |
| `agent_print()` / `agent_printf()` / `agent_print_char()` | Agent mode stdout output |
| `agent_json_escape()` | Writes JSON-escaped string to stdout |

These are thin wrappers around `WriteConsole`/`write` — not worth testing directly.

---

## 3. Code Paths Unreachable in Tests

### 3.1 `main()` orchestration — first-run configuration path

Lines ~2334-2358 of `main.c` handle the first-run experience:
- Checks `!db_metadata_exists()`
- Launches `ui_edit_config()` interactively
- Saves default configuration

This path requires no metadata file to exist AND interactive terminal input.

**Testing approach:** Could be tested by running the binary in a clean temp directory
with piped input (Escape key to dismiss config editor).

### 3.2 `spawn_background_rescan()` — background process spawning

Spawns a child process to perform rescan in the background. Depends on the binary
being available as an executable and uses platform-specific process creation.

**Testing approach:** Integration test only. Unit testing would require mocking
process creation.

### 3.3 `check_service_version()` — service version compatibility check

Connects to the running service, checks version compatibility, and may stop the
service if versions don't match. Requires a running NCDService.exe.

**Testing approach:** Service integration test only.

### 3.4 `run_requested_rescan()` — full rescan orchestration

Lines ~2247-2308. Calls `scan_mounts()` or `scan_mount()` depending on options,
writes progress, handles timeouts. Heavily I/O dependent.

**Testing approach:** Integration test with temp directory tree (already partially
covered by `test_features.bat`/`.sh` category B and C tests).

### 3.5 Platform-conditional code (Windows vs POSIX)

Several files have dual implementations guarded by `#ifdef _WIN32`:
- `shm_platform_win.c` vs `shm_platform_posix.c`
- `control_ipc_win.c` vs `control_ipc_posix.c`
- Windows/POSIX branches in `ui.c`, `scanner.c`, `platform.c`

Only the platform matching the build target is compiled. The other platform's code
is structurally unreachable in any single test run.

**Testing approach:** CI matrix with both Windows and Linux builds. Already addressed
by having `test/Win/` and `test/Wsl/` directories.

### 3.6 Error paths in `common.c` — allocation failure

`ncd_malloc()`, `ncd_realloc()`, `ncd_calloc()`, `ncd_strdup()` all call
`exit(EXIT_FAILURE)` on allocation failure. These paths are unreachable in normal
test execution since malloc rarely fails.

**Testing approach:** Not worth testing. The behavior (exit on OOM) is intentional
and correct for a CLI tool.

### 3.7 `service_main.c` — request handlers

All `handle_*` functions (handle_get_version, handle_request_shutdown,
handle_submit_heuristic, etc.) are `static` functions called from the service's
main event loop. They're only reachable when the service binary is running and
receiving IPC messages.

**Testing approach:** Service integration tests (Tier 5 in coverage plan).

### 3.8 `service_publish.c` — snapshot building

`build_metadata_snapshot()` and `build_database_snapshot()` are `static` functions
called by the publisher. `compute_string_pool_size()`, `compute_metadata_snapshot_size()`,
`compute_database_snapshot_size()` are internal sizing functions.

**Testing approach:** Could be exposed via a test header. Alternatively, test
indirectly through `snapshot_publisher_publish_meta()` which orchestrates the full
build-and-publish cycle.

---

## 4. `#if DEBUG` — Conditional Debug Code

### 4.1 `g_test_no_checksum` / `g_test_slow_mode` flags

These are debug-only globals set from command-line flags `/test NC` and `/test SL`.
They're tested in `test_db_corruption.c` (tests 14-16) but only when compiled with
`DEBUG` defined.

**Status:** Properly guarded. Tests exist but are conditional.

**Recommendation:** Ensure CI builds include a DEBUG configuration to exercise these.

---

## 5. Summary Table

| Category | Lines (est) | Can Unit Test? | Can Integration Test? | Action |
|----------|-------------|----------------|----------------------|--------|
| TUI rendering (`ui.c`) | ~1500 | No | Partial (pipe keys) | Extract logic into testable functions |
| TUI logic (`ui.c` filter/scroll) | ~200 | Yes, if exposed | Yes | Expose static functions for testing |
| Console I/O wrappers | ~100 | No | N/A | Not worth testing |
| Legacy migration code | ~150 | Yes | Yes | Add migration tests or remove |
| `scan_set_exclusion_list()` | ~5 | N/A | N/A | **Remove — dead code** |
| First-run config path | ~25 | No | Yes (pipe input) | Add integration test |
| Background rescan spawn | ~25 | No | Yes | Integration test |
| Service request handlers | ~300 | No (static) | Yes (service tests) | Service integration tests |
| Snapshot building | ~200 | Possible (expose) | Yes (service tests) | Expose or test via publisher |
| Platform-other code | ~1000 | Only on that platform | CI matrix | Ensure CI covers both platforms |
| OOM exit paths | ~20 | No (exits process) | No | Intentional — skip |
| `db_save()` JSON | ~100 | Yes | Yes | Verify callers, add test or remove |

---

## 6. Recommended Immediate Actions

1. **Delete** `scan_set_exclusion_list()` — confirmed dead code, no callers.
2. **Verify** `db_save()` caller status — if unused, remove; if used, add test.
3. **Mark** legacy functions (`db_group_create`, `db_group_load`, `db_group_path`,
   `db_config_path`, `db_group_free`) with `/* LEGACY: migration only */` comments.
4. **Extract** `apply_filter()`, `clamp_scroll()`, `nav_clamp_scroll()` from `ui.c`
   into a testable `ui_logic.c` or expose via test header.
5. **Ensure** CI builds include both `Release` and `Debug` configurations to cover
   `#if DEBUG` paths.
