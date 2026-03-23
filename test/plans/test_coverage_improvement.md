# NCD Test Coverage Improvement Plan

## Current State

The existing unit test suite covers **~30%** of the codebase's exported functions. Coverage is strong for:
- `database.c` — creation, binary save/load, corruption handling, history, groups, heuristics roundtrip
- `matcher.c` — exact match, prefix, glob patterns, hidden/system filtering
- `shared_state.c` — header validation, CRC64, section lookup, snapshot info
- `test_bugs.c` — regression tests for known bugs (strbuilder, full_path, fuzzy perf, version checks)

Coverage is **absent or minimal** for:
- `main.c` (CLI parsing, agent mode, result writing, heuristic helpers)
- `scanner.c` (directory scanning, exclusion matching)
- `ui.c` (all TUI code)
- `platform.c` (platform abstraction utilities)
- `strbuilder.c` (most builder operations)
- `common.c` (overflow-checked allocation)
- `service_*.c` (service state mutations, publishing, IPC handling)
- `shm_platform_*.c` (shared memory platform layer)
- `control_ipc_*.c` (IPC client/server)
- `state_backend_service.c` (service-backed state)

---

## Priority Tiers

### Tier 1 — High Value, Low Effort (pure logic, no I/O or TUI)

These functions are deterministic, side-effect-free, and can be tested in isolation with the existing test framework.

#### 1.1 `test_strbuilder.c` (NEW) — ~15 tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | Init sets valid state | `sb_init()` |
| 2 | Append single string | `sb_append()` |
| 3 | Append multiple strings concatenates | `sb_append()` x3 |
| 4 | Appendn truncates at n bytes | `sb_appendn()` |
| 5 | Appendc single character | `sb_appendc()` |
| 6 | Appendf formatted output | `sb_appendf()` |
| 7 | Append_json_str escapes quotes | `sb_append_json_str()` |
| 8 | Append_json_str escapes backslash | `sb_append_json_str()` |
| 9 | Append_json_str escapes control chars | `sb_append_json_str()` |
| 10 | Clear resets length but keeps buffer | `sb_clear()` |
| 11 | Steal transfers ownership | `sb_steal()` |
| 12 | Dup copies buffer | `sb_dup()` |
| 13 | Ensure_cap grows buffer | `sb_ensure_cap()` |
| 14 | Large append triggers realloc | `sb_append()` with >initial capacity |
| 15 | Free then init cycle | `sb_free()`, `sb_init()` |

#### 1.2 `test_common.c` (NEW) — ~7 tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | ncd_malloc returns non-null | `ncd_malloc()` |
| 2 | ncd_calloc returns zeroed memory | `ncd_calloc()` |
| 3 | ncd_realloc grows allocation | `ncd_realloc()` |
| 4 | ncd_strdup copies string | `ncd_strdup()` |
| 5 | ncd_malloc_array with safe sizes | `ncd_malloc_array()` |
| 6 | ncd_mul_overflow_check detects overflow | `ncd_mul_overflow_check()` |
| 7 | ncd_add_overflow_check detects overflow | `ncd_add_overflow_check()` |

#### 1.3 `test_platform.c` (NEW) — ~12 tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | platform_strcasestr finds substring | `platform_strcasestr()` |
| 2 | platform_strcasestr case insensitive | `platform_strcasestr()` |
| 3 | platform_strcasestr returns NULL on miss | `platform_strcasestr()` |
| 4 | platform_is_drive_specifier valid ("C:") | `platform_is_drive_specifier()` |
| 5 | platform_is_drive_specifier invalid | `platform_is_drive_specifier()` |
| 6 | platform_parse_drive_from_search extracts letter | `platform_parse_drive_from_search()` |
| 7 | platform_parse_drive_from_search no drive | `platform_parse_drive_from_search()` |
| 8 | platform_build_mount_path valid letter | `platform_build_mount_path()` |
| 9 | platform_filter_available_drives filters correctly | `platform_filter_available_drives()` |
| 10 | platform_is_pseudo_fs matches known types | `platform_is_pseudo_fs()` |
| 11 | platform_get_app_title returns non-null | `platform_get_app_title()` |
| 12 | ncd_platform_db_default_path returns valid path | `ncd_platform_db_default_path()` |

#### 1.4 Expand `test_matcher.c` — ~8 additional tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | Fuzzy match with 1-char typo | `matcher_find_fuzzy()` |
| 2 | Fuzzy match with transposition | `matcher_find_fuzzy()` |
| 3 | Fuzzy match returns NULL on total mismatch | `matcher_find_fuzzy()` |
| 4 | Fuzzy match with case difference | `matcher_find_fuzzy()` |
| 5 | Name index build returns non-null | `name_index_build()` |
| 6 | Name index find_by_hash finds entry | `name_index_find_by_hash()` |
| 7 | Name index find_by_hash misses unknown hash | `name_index_find_by_hash()` |
| 8 | Name index free doesn't crash | `name_index_free()` |

---

### Tier 2 — Medium Value, Medium Effort (requires file I/O or metadata state)

#### 2.1 Expand `test_database.c` — ~20 additional tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | db_save JSON then db_load roundtrip | `db_save()`, `db_load()` |
| 2 | db_load_auto detects binary format | `db_load_auto()` |
| 3 | db_load_auto detects JSON format | `db_load_auto()` |
| 4 | db_check_file_version returns correct version | `db_check_file_version()` |
| 5 | db_check_file_version on empty file returns error | `db_check_file_version()` |
| 6 | db_check_all_versions with no databases | `db_check_all_versions()` |
| 7 | db_set_skipped_rescan_flag sets flag in file | `db_set_skipped_rescan_flag()` |
| 8 | db_config_init_defaults populates all fields | `db_config_init_defaults()` |
| 9 | db_config_save then db_config_load roundtrip | `db_config_save()`, `db_config_load()` |
| 10 | db_exclusion_add adds pattern | `db_exclusion_add()` |
| 11 | db_exclusion_remove removes pattern | `db_exclusion_remove()` |
| 12 | db_exclusion_check matches pattern | `db_exclusion_check()` |
| 13 | db_exclusion_check rejects non-matching | `db_exclusion_check()` |
| 14 | db_exclusion_init_defaults adds defaults | `db_exclusion_init_defaults()` |
| 15 | db_heur_calculate_score returns higher for recent | `db_heur_calculate_score()` |
| 16 | db_heur_find locates entry by search | `db_heur_find()` |
| 17 | db_heur_find_best finds best match | `db_heur_find_best()` |
| 18 | db_drive_backup_create/restore roundtrip | `db_drive_backup_create()`, `db_drive_restore_from_backup()` |
| 19 | db_find_drive returns correct drive | `db_find_drive()` |
| 20 | db_find_drive returns NULL for missing | `db_find_drive()` |

#### 2.2 `test_metadata.c` (NEW) — ~12 tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | db_metadata_create returns valid struct | `db_metadata_create()` |
| 2 | db_metadata_save then load roundtrip | `db_metadata_save()`, `db_metadata_load()` |
| 3 | db_metadata_exists returns false before save | `db_metadata_exists()` |
| 4 | db_metadata_exists returns true after save | `db_metadata_exists()` |
| 5 | Metadata preserves config section | `db_metadata_save()`, `db_metadata_load()` |
| 6 | Metadata preserves groups section | roundtrip with groups |
| 7 | Metadata preserves exclusions section | roundtrip with exclusions |
| 8 | Metadata preserves heuristics section | roundtrip with heuristics |
| 9 | Metadata preserves history section | roundtrip with history |
| 10 | db_metadata_migrate handles no legacy files | `db_metadata_migrate()` |
| 11 | db_metadata_cleanup_legacy returns 0 with none | `db_metadata_cleanup_legacy()` |
| 12 | db_metadata_free handles NULL | `db_metadata_free()` |

#### 2.3 `test_scanner.c` (NEW) — ~8 tests

Requires creating temporary directory trees on disk.

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | scan_mount populates database | `scan_mount()` |
| 2 | scan_mount respects hidden flag | `scan_mount()` with include_hidden |
| 3 | scan_mount applies exclusions | `scan_mount()` with exclusion list |
| 4 | scan_subdirectory merges into existing db | `scan_subdirectory()` |
| 5 | find_is_directory returns true for dirs | `find_is_directory()` |
| 6 | find_is_hidden detects hidden entries | `find_is_hidden()` |
| 7 | find_is_reparse detects symlinks/junctions | `find_is_reparse()` |
| 8 | scan_mounts handles empty mount list | `scan_mounts()` |

---

### Tier 3 — Medium Value, Higher Effort (agent mode, CLI parsing)

#### 3.1 `test_cli_parse.c` (NEW) — ~20 tests

Extract `parse_args()` and `parse_agent_args()` for testing. Currently these are `static` in `main.c` — expose them via a test-only header or `#include "main.c"` trick.

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | No args: show_help default | `parse_args()` |
| 2 | `/h` sets show_help | `parse_args()` |
| 3 | `/?` sets show_help | `parse_args()` |
| 4 | `/v` sets show_version | `parse_args()` |
| 5 | `/r` sets force_rescan | `parse_args()` |
| 6 | `/r.` sets subdir_rescan | `parse_args()` |
| 7 | `/i` sets show_hidden | `parse_args()` |
| 8 | `/s` sets show_system | `parse_args()` |
| 9 | `/a` sets both hidden and system | `parse_args()` |
| 10 | `/z` sets fuzzy_match | `parse_args()` |
| 11 | `/f` sets show_history | `parse_args()` |
| 12 | `/fc` sets clear_history | `parse_args()` |
| 13 | `/g @name` sets group_set + group_name | `parse_args()` |
| 14 | `/g- @name` sets group_remove + group_name | `parse_args()` |
| 15 | `/gl` sets group_list | `parse_args()` |
| 16 | `/d path` sets db_path | `parse_args()` |
| 17 | `/t 30` sets timeout_seconds=30 | `parse_args()` |
| 18 | `/c` sets config_edit | `parse_args()` |
| 19 | Search term captured in opts.search | `parse_args()` |
| 20 | Combined flags `/ris` sets all three | `parse_args()` |

#### 3.2 `test_agent_mode.c` (NEW) — ~15 tests

Test agent mode subcommands by capturing their output (they write to stdout). Requires a test database.

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | agent query with match returns JSON | `agent_mode_query()` |
| 2 | agent query no match returns empty | `agent_mode_query()` |
| 3 | agent query respects --limit | `agent_mode_query()` |
| 4 | agent ls lists directory contents | `agent_mode_ls()` |
| 5 | agent ls with --json outputs JSON | `agent_mode_ls()` |
| 6 | agent ls nonexistent returns error | `agent_mode_ls()` |
| 7 | agent tree outputs tree structure | `agent_mode_tree()` |
| 8 | agent tree respects --depth | `agent_mode_tree()` |
| 9 | agent check with existing dir returns OK | `agent_mode_check()` |
| 10 | agent check with missing dir returns error | `agent_mode_check()` |
| 11 | agent mkdir creates directory | `agent_mode_mkdir()` |
| 12 | agent complete returns completions | `agent_mode_complete()` |
| 13 | glob_match exact match | `glob_match()` |
| 14 | glob_match with wildcard | `glob_match()` |
| 15 | glob_match no match | `glob_match()` |

#### 3.3 `test_result_output.c` (NEW) — ~8 tests

Test the result file writing functions.

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | write_result OK writes correct format | `write_result()` |
| 2 | write_result ERROR writes error format | `write_result()` |
| 3 | write_result CANCEL writes cancel format | `write_result()` |
| 4 | result_ok writes path correctly | `result_ok()` |
| 5 | result_error writes error message | `result_error()` |
| 6 | result_cancel writes cancel status | `result_cancel()` |
| 7 | heur_sanitize lowercases and trims | `heur_sanitize()` |
| 8 | heur_promote_match moves preferred to top | `heur_promote_match()` |

---

### Tier 4 — Service Layer (requires service infrastructure or mocking)

#### 4.1 Expand `test_service_lazy_load.c` — ~12 additional tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | service_state_note_heuristic records choice | `service_state_note_heuristic()` |
| 2 | service_state_add_group adds group | `service_state_add_group()` |
| 3 | service_state_remove_group removes group | `service_state_remove_group()` |
| 4 | service_state_add_exclusion adds pattern | `service_state_add_exclusion()` |
| 5 | service_state_remove_exclusion removes pattern | `service_state_remove_exclusion()` |
| 6 | service_state_update_config updates config | `service_state_update_config()` |
| 7 | service_state_flush persists state | `service_state_flush()` |
| 8 | service_state_needs_flush detects dirty | `service_state_needs_flush()` |
| 9 | service_state_get/bump_meta_generation | generation counter functions |
| 10 | service_state_get/bump_db_generation | generation counter functions |
| 11 | service_state_get_stats returns valid stats | `service_state_get_stats()` |
| 12 | service_state_get/clear_dirty_flags | dirty flag functions |

#### 4.2 `test_shared_state_extended.c` (NEW) — ~6 tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | shm_compute_checksum returns deterministic value | `shm_compute_checksum()` |
| 2 | shm_validate_checksum passes for valid data | `shm_validate_checksum()` |
| 3 | shm_validate_checksum fails for corrupted data | `shm_validate_checksum()` |
| 4 | shm_make_meta_name returns valid name | `shm_make_meta_name()` |
| 5 | shm_make_db_name returns valid name | `shm_make_db_name()` |
| 6 | shm_round_up_size rounds correctly | `shm_round_up_size()` |

#### 4.3 `test_shm_platform.c` (NEW) — ~10 tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | shm_platform_init succeeds | `shm_platform_init()` |
| 2 | shm_create/shm_close lifecycle | `shm_create()`, `shm_close()` |
| 3 | shm_create then shm_open reads same data | `shm_create()`, `shm_open()` |
| 4 | shm_map/shm_unmap lifecycle | `shm_map()`, `shm_unmap()` |
| 5 | shm_get_size returns correct size | `shm_get_size()` |
| 6 | shm_get_name returns correct name | `shm_get_name()` |
| 7 | shm_exists returns true after create | `shm_exists()` |
| 8 | shm_exists returns false after unlink | `shm_unlink()`, `shm_exists()` |
| 9 | shm_get_page_size returns nonzero | `shm_get_page_size()` |
| 10 | shm_error_string returns non-null for all codes | `shm_error_string()` |

#### 4.4 `test_ipc.c` (NEW) — ~8 tests

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | ipc_client_init succeeds | `ipc_client_init()` |
| 2 | ipc_client_connect fails when no service | `ipc_client_connect()` |
| 3 | ipc_make_address returns valid path | `ipc_make_address()` |
| 4 | ipc_error_string for all result codes | `ipc_error_string()` |
| 5 | ipc_client_cleanup doesn't crash | `ipc_client_cleanup()` |
| 6 | ipc_client_disconnect handles NULL | `ipc_client_disconnect()` |
| 7 | ipc_client_ping fails without connection | `ipc_client_ping()` |
| 8 | ipc_client_get_version fails without connection | `ipc_client_get_version()` |

---

### Tier 5 — Integration & End-to-End

#### 5.1 Expand integration test scripts

The existing `test_strategy.md` defines 218 test cases across categories A-U. Many of these are already implemented in `test/Win/test_features.bat` and `test/Wsl/test_features.sh`. Gaps to fill:

- **Category F (Fuzzy match):** Only F4 performance tested in unit tests; F1-F3, F5-F6 need integration coverage
- **Category K (Config):** No automated config TUI tests
- **Category N (Navigator):** No automated navigator tests
- **Category Q (Version update flow):** No tests for version mismatch prompts
- **Category U (Circular history):** Unit tests exist for primitives, but no end-to-end wrapper script tests

#### 5.2 `test_service_integration.c` (NEW or expand existing) — ~10 tests

Requires starting NCDService.exe in the background.

| # | Test | Functions Exercised |
|---|------|-------------------|
| 1 | Service starts and responds to ping | service startup, `ipc_client_ping()` |
| 2 | Service provides database via shared memory | `state_backend_open_service()` |
| 3 | Service accepts heuristic update | `ipc_client_submit_heuristic()` |
| 4 | Service accepts metadata update | `ipc_client_submit_metadata()` |
| 5 | Service handles rescan request | `ipc_client_request_rescan()` |
| 6 | Service handles flush request | `ipc_client_request_flush()` |
| 7 | Service version check compatibility | `ipc_client_check_version()` |
| 8 | Service graceful shutdown | `ipc_client_request_shutdown()` |
| 9 | Client falls back to local on service down | `state_backend_open_best_effort()` |
| 10 | Snapshot publisher produces valid snapshots | `snapshot_publisher_publish_meta()` |

---

## Implementation Order

```
Phase 1 (Tier 1): ~42 tests — no new infrastructure needed
  1. test_strbuilder.c
  2. test_common.c
  3. test_platform.c
  4. Expand test_matcher.c (fuzzy + name index)

Phase 2 (Tier 2): ~40 tests — needs temp file/dir helpers
  5. Expand test_database.c (JSON, config, exclusions, heuristics scoring)
  6. test_metadata.c
  7. test_scanner.c

Phase 3 (Tier 3): ~43 tests — needs main.c refactoring or #include trick
  8. test_cli_parse.c
  9. test_agent_mode.c
  10. test_result_output.c

Phase 4 (Tier 4): ~36 tests — service layer
  11. Expand test_service_lazy_load.c (state mutations)
  12. test_shared_state_extended.c
  13. test_shm_platform.c
  14. test_ipc.c

Phase 5 (Tier 5): ~10+ tests — full integration
  15. test_service_integration.c
  16. Expand shell test scripts for remaining categories
```

## Build System Updates

Add new test targets to `test/Makefile`:

```makefile
# New test binaries
TEST_STRBUILDER  = test_strbuilder$(EXT)
TEST_COMMON      = test_common$(EXT)
TEST_PLATFORM    = test_platform$(EXT)
TEST_METADATA    = test_metadata$(EXT)
TEST_SCANNER     = test_scanner$(EXT)
TEST_CLI_PARSE   = test_cli_parse$(EXT)
TEST_AGENT_MODE  = test_agent_mode$(EXT)
TEST_RESULT      = test_result_output$(EXT)
TEST_SHM_EXT     = test_shared_state_ext$(EXT)
TEST_SHM_PLAT    = test_shm_platform$(EXT)
TEST_IPC         = test_ipc$(EXT)
TEST_SVC_INT     = test_service_integration$(EXT)
```

Add to `build-tests.bat` for Windows builds as well.

## Estimated Coverage After Full Implementation

| Module | Current | After Phase 1 | After Phase 2 | After Phase 3 | After All |
|--------|---------|---------------|---------------|---------------|-----------|
| database.c | ~40% | ~40% | ~75% | ~75% | ~80% |
| matcher.c | ~60% | ~85% | ~85% | ~85% | ~90% |
| scanner.c | ~0% | ~0% | ~40% | ~40% | ~50% |
| ui.c | ~0% | ~0% | ~0% | ~0% | ~0% |
| main.c | ~0% | ~0% | ~0% | ~60% | ~65% |
| platform.c | ~5% | ~50% | ~50% | ~50% | ~55% |
| strbuilder.c | ~15% | ~90% | ~90% | ~90% | ~90% |
| common.c | ~20% | ~90% | ~90% | ~90% | ~90% |
| shared_state.c | ~70% | ~70% | ~70% | ~70% | ~90% |
| service_state.c | ~30% | ~30% | ~30% | ~30% | ~70% |
| service_publish.c | ~0% | ~0% | ~0% | ~0% | ~30% |
| shm_platform_*.c | ~0% | ~0% | ~0% | ~0% | ~60% |
| control_ipc_*.c | ~0% | ~0% | ~0% | ~0% | ~40% |
| state_backend_*.c | ~20% | ~20% | ~20% | ~20% | ~50% |
| **Overall** | **~25%** | **~35%** | **~45%** | **~55%** | **~65%** |

Note: `ui.c` remains at 0% because TUI code requires terminal emulation — see dead code document for details.
