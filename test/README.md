# NCD Test Suite

This directory contains the comprehensive test suite for NewChangeDirectory (NCD).

## Test Isolation Requirements

**IMPORTANT:** All tests must be self-contained and must NOT touch the user's real data:

- **DO NOT scan user drives** - Tests must only scan temporary test directories (VHD on Windows, ramdisk or /tmp on Linux)
- **DO NOT use user metadata** - Tests must use isolated metadata/config locations via `-conf` option or environment variables
- **DO NOT modify user databases** - Tests must use temporary database locations

### Test Isolation Mechanisms

**Windows:**
- Uses VHD (virtual hard disk) mounted to an unused drive letter (e.g., Z:)
- Falls back to `%TEMP%\ncd_test_data_*` directory if VHD creation fails
- Sets `LOCALAPPDATA` environment variable to temp location for metadata isolation
- Sets `NCD_TEST_MODE=1` environment variable to disable background rescans
- Uses `/r.` (subdirectory rescan) instead of `/r` (full drive scan)
- Uses `/rDRIVE` to scan only the test drive

**WSL/Linux:**
- Uses ramdisk (`tmpfs`) mounted at `/mnt/ncd_test_ramdisk_*`
- Falls back to `/tmp/ncd_test_tree_*` if ramdisk unavailable
- Sets `XDG_DATA_HOME` environment variable to temp location for metadata isolation
- Sets `NCD_TEST_MODE=1` environment variable to disable background rescans
- Uses `/r.` (subdirectory rescan) instead of `/r` (full mount scan)

**When Writing New Tests:**
- Always use `/r.` or `/rDRIVE` to scan specific locations, never bare `/r`
- Set custom metadata path with `-conf <path>` if not using environment isolation
- Create all test data in temporary directories only
- Clean up test data after tests complete

## Quick Start

### Comprehensive Test Runner (6 Test Suites)

The comprehensive test runner executes **six test suites** organized by platform and service configuration:

| # | Test Script | Platform | Tests |
|---|-------------|----------|-------|
| 1 | `test_service_win.bat` | Windows | Service in isolation (no client) |
| 2 | `test_service_wsl.sh` | WSL | Service in isolation (no client) |
| 3 | `test_ncd_win_standalone.bat` | Windows | NCD client without service |
| 4 | `test_ncd_win_with_service.bat` | Windows | NCD client with service |
| 5 | `test_ncd_wsl_standalone.sh` | WSL | NCD client without service |
| 6 | `test_ncd_wsl_with_service.sh` | WSL | NCD client with service |

**Quick Run (All Tests from Root):**
```batch
:: Builds and runs all 6 test suites
build_and_run_alltests.bat
```

**PowerShell Alternative (Windows or WSL):**
```powershell
cd test
powershell -ExecutionPolicy Bypass -File run_all_tests.ps1
```

**Bash Alternative (WSL):**
```bash
cd test
./run_all_tests.sh
```

**Options for build_and_run_alltests.bat:**
```batch
build_and_run_alltests.bat --windows-only     :: Run only Windows tests
build_and_run_alltests.bat --wsl-only         :: Run only WSL tests
build_and_run_alltests.bat --no-service       :: Skip tests requiring service
build_and_run_alltests.bat --skip-build       :: Skip build phase, just run tests
```

### Individual Test Suites

```bash
# Linux/WSL - Unit Tests
cd test
make test

# Linux/WSL - Database Corruption Tests
cd test
make corruption

# Linux/WSL - Recursive Mount Tests (requires root)
cd test
make recursive-mount

# Windows PowerShell
cd test\PowerShell
.\Run-Tests.ps1

# Windows CMD
cd test\Win
test_integration.bat

# Windows Agent Command Tests
cd test\Win
test_agent_commands.bat

# WSL Agent Command Tests
cd test/Wsl
chmod +x test_agent_commands.sh
./test_agent_commands.sh
```

## Test Structure

### Main Test Scripts (6 Test Suites)

These are the primary test entry points organized by platform and service configuration:

| Test Script | Platform | Description |
|-------------|----------|-------------|
| `test_service_win.bat` | Windows | Service tests without client (isolated) |
| `test_service_wsl.sh` | WSL/Linux | Service tests without client (isolated) |
| `test_ncd_win_standalone.bat` | Windows | NCD client tests - standalone mode |
| `test_ncd_win_with_service.bat` | Windows | NCD client tests - with service |
| `test_ncd_wsl_standalone.sh` | WSL/Linux | NCD client tests - standalone mode |
| `test_ncd_wsl_with_service.sh` | WSL/Linux | NCD client tests - with service |

### Complete Directory Structure

```
test/
├── test_service_win.bat       # [NEW] Windows Service tests (no client)
├── test_service_wsl.sh        # [NEW] WSL Service tests (no client)
├── test_ncd_win_standalone.bat   # [NEW] NCD Windows without service
├── test_ncd_win_with_service.bat # [NEW] NCD Windows with service
├── test_ncd_wsl_standalone.sh    # [NEW] NCD WSL without service
├── test_ncd_wsl_with_service.sh  # [NEW] NCD WSL with service
├── test_framework.h           # Minimal unit testing framework
├── test_framework.c           # Test framework implementation
├── test_database.c            # Database module tests (34 tests)
├── test_matcher.c             # Matcher module tests (23 tests)
├── test_scanner.c             # Scanner module tests (9 tests)
├── test_metadata.c            # Metadata module tests (11 tests)
├── test_platform.c            # Platform abstraction tests (12 tests)
├── test_strbuilder.c          # String builder tests (15 tests)
├── test_common.c              # Common/memory allocation tests (7 tests)
├── test_history.c             # Directory history tests (14 tests)
├── test_db_corruption.c       # Database corruption tests (16 tests, +3 DEBUG)
├── test_bugs.c                # Known bug detection tests (20 tests)
├── test_service_lazy_load.c   # Service lazy loading tests
├── test_service_parity.c      # Service vs standalone parity tests
├── test_service_lifecycle.c   # Service start/stop lifecycle tests (13 tests)
├── test_service_integration.c # NCD client service integration tests (12 tests)
├── test_service_database.c    # Service database snapshot tests
├── test_service_ipc.c         # Service IPC protocol tests (Tier 5)
├── test_shared_state.c        # Shared state validation tests
├── test_shared_state_extended.c # Extended shared state tests
├── test_shm_platform.c        # Shared memory platform tests
├── test_ipc.c                 # IPC communication tests
├── test_cli_parse.c           # CLI parsing tests (31 tests)
├── test_cli_parse_extended.c  # Extended CLI parsing tests (31 additional tests)
├── test_agent_mode.c          # Agent mode tests (16 tests)
├── test_platform_extended.c   # Extended platform tests (34 additional tests)
├── test_strbuilder_extended.c # Extended string builder tests (39 additional tests)
├── test_common_extended.c     # Extended common/memory tests (37 additional tests)
├── test_integration_extended.c # Extended integration tests (19 additional tests)
├── test_result_output.c       # Result output tests
├── fuzz_database.c            # Fuzz testing for database loading
├── bench_matcher.c            # Performance benchmarks
├── Makefile                   # Test build system
├── run_all_tests.ps1          # Comprehensive test runner (PowerShell)
├── run_all_tests.sh           # Comprehensive test runner (Bash)
├── Win/                       # Windows-specific tests
│   ├── test_features.bat      # Windows feature tests (VHD-based)
│   └── test_agent_commands.bat # Agent command tests
├── Wsl/                       # WSL/Linux-specific tests
│   ├── test_features.sh       # WSL feature tests (ramdisk-based)
│   ├── test_agent_commands.sh # Agent command tests
│   ├── test_integration.sh    # Integration tests
│   └── test_recursive_mount.sh # Recursive mount stress tests
└── PowerShell/                # PowerShell-specific tests
    └── Run-Tests.ps1          # PowerShell test runner
```

### Comprehensive Test Runner

The `run_all_tests.ps1` and `run_all_tests.sh` scripts provide a unified interface for running the complete NCD test suite across **all four testing environments**:

| Environment | Platform | Service State |
|-------------|----------|---------------|
| 1 | Windows | Standalone (no service) |
| 2 | Windows | With Service (shared memory) |
| 3 | WSL | Standalone (no service) |
| 4 | WSL | With Service (shared memory) |

This ensures that all functionality works correctly regardless of platform or whether the optional resident service is running.

## Test Categories

### 1. Unit Tests (`test_*.c`)

These test individual modules in isolation:

| Test File | Test Count | Description |
|-----------|------------|-------------|
| **test_database.c** | 34 | Database creation, manipulation, save/load, exclusions, heuristics, text encoding |
| **test_matcher.c** | 23 | Search matching, glob patterns, fuzzy matching, name index |
| **test_scanner.c** | 9 | Directory scanning, hidden/system detection, exclusions, merging |
| **test_metadata.c** | 11 | Consolidated metadata (config, groups, exclusions, heuristics, history) |
| **test_platform.c** | 12 | Platform abstraction, string utilities, drive handling |
| **test_platform_extended.c** | 34 | Extended platform tests (drive handling, filtering, pseudo-fs, strings) |
| **test_strbuilder.c** | 15 | String builder, JSON escaping, buffer management |
| **test_strbuilder_extended.c** | 39 | Extended string builder tests (edge cases, stress tests, JSON) |
| **test_common.c** | 7 | Memory allocation wrappers, overflow checking |
| **test_common_extended.c** | 37 | Extended memory tests (stress, edge cases, alignment) |
| **test_history.c** | 14 | Directory history add/remove/clear/swap operations |
| **test_cli_parse.c** | 31 | CLI argument parsing (basic options, flags, combinations) |
| **test_cli_parse_extended.c** | 31 | Extended CLI parsing (history, groups, exclusions, agent commands) |
| **test_integration_extended.c** | 19 | Integration tests (database workflows, matcher scenarios, metadata) |

**Total Unit Tests: 325+**

### 2. Service Tests

Tests for the optional resident service and shared memory state:

| Test File | Test Count | Description |
|-----------|------------|-------------|
| **test_service_lifecycle.c** | 13 | Service start/stop/restart, IPC connectivity, state progression |
| **test_service_integration.c** | 12 | NCD client service status reporting, help output, agent commands |
| **test_service_lazy_load.c** | - | Service lazy loading and state machine |
| **test_service_parity.c** | - | Service vs standalone mode parity |
| **test_service_database.c** | - | Service database snapshot loading |
| **test_service_ipc.c** | - | Tier 5 IPC integration tests |

### 3. Shared Memory / IPC Tests

| Test File | Description |
|-----------|-------------|
| **test_shared_state.c** | Snapshot header validation, checksum verification, section lookup |
| **test_shared_state_extended.c** | Extended shared state validation |
| **test_shm_platform.c** | Platform-specific shared memory implementation |
| **test_ipc.c** | IPC communication protocol tests |

### 4. Fuzz Tests (`fuzz_*.c`)

These test robustness against malformed input:

- **fuzz_database.c** - 10,000+ iterations of random binary corruption

### 5. Database Corruption Tests (`test_db_corruption.c`)

Targeted corruption of specific database fields (16 tests, +3 in DEBUG mode):
- Magic number corruption (change 'NCDB' to invalid)
- Version field corruption (invalid version numbers)
- Drive count overflow (0xFFFFFFFF, 0, large values)
- Directory count overflow
- Bit flipping in header (32 different bit positions)
- File truncation at various points
- Appending garbage data
- Random bit flips (alternating 1-bit and 2-bit flips)
- Checksum corruption and detection
- DEBUG: `/test NC` (no checksum) mode tests

### 6. Bug Detection Tests (`test_bugs.c`)

Tests that expose identified bugs (20 tests):
- Metadata heuristics/groups parsing bounds check bugs
- StringBuilder infinite loop after `sb_free()`
- `db_full_path()` silent truncation at MAX_DEPTH=128
- `db_full_path()` circular parent chain handling
- `generate_variations_recursive()` combinatorial explosion
- Unchecked `fread()` returns in `db_check_file_version()`
- `BinFileHdr` field layout verification
- Matcher with empty database

### 7. Performance Benchmarks (`bench_*.c`)

These measure performance characteristics:

- **bench_matcher.c** - Matcher query performance on synthetic databases

### 8. Integration Tests

Platform-specific end-to-end tests:

- **Win/test_features.bat** - Windows CMD integration tests (VHD-based)
- **Win/test_agent_commands.bat** - Windows agent command tests
- **Wsl/test_features.sh** - Linux/WSL bash integration tests (ramdisk-based)
- **Wsl/test_integration.sh** - WSL integration tests
- **Wsl/test_recursive_mount.sh** - Recursive mount stress tests
- **PowerShell/Run-Tests.ps1** - PowerShell test runner with build automation

### 9. CLI and Agent Tests

| Test File | Description |
|-----------|-------------|
| **test_cli_parse.c** | Command-line argument parsing |
| **test_agent_mode.c** | Agent mode API functionality |
| **test_result_output.c** | Result output formatting and escaping |

## Running Tests

### Linux/WSL

```bash
cd test
make test        # Run unit tests
make fuzz        # Fuzz tests (60 second timeout)
make bench       # Performance benchmarks
make corruption  # Database corruption tests
make bugs        # Bug detection tests
make service-test # Service lifecycle and integration tests
make all-environments  # All 4 environments (comprehensive)
```

### Windows (MSVC)

```batch
cd test
cl /nologo /W3 /O2 /Isrc /I. /I../../shared test_database.c test_framework.c src\database.c /Fe:test_database.exe
cl /nologo /W3 /O2 /Isrc /I. /I../../shared test_matcher.c test_framework.c src\matcher.c src\database.c /Fe:test_matcher.exe
test_database.exe
test_matcher.exe
```

### Windows (MinGW)

```batch
cd test
gcc -Wall -Wextra -I../src -I../../shared -I. -o test_database.exe test_database.c test_framework.c ../src/database.c -lpthread
gcc -Wall -Wextra -I../src -I../../shared -I. -o test_matcher.exe test_matcher.c test_framework.c ../src/matcher.c ../src/database.c -lpthread
test_database.exe
test_mailer.exe
```

## Test Coverage

### Database Tests (test_database.c - 34 tests)

**Basic Operations:**
- `create_and_free` - Database creation and cleanup
- `add_drive` - Adding drives to database
- `add_directory` - Adding directories with parent relationships
- `full_path_reconstruction` - Reconstructing full paths from parent chain

**Save/Load Operations:**
- `binary_save_load_roundtrip` - Binary format serialization/deserialization
- `json_save_load_roundtrip` - JSON format serialization/deserialization
- `load_auto_detects_binary_format` - Auto-detection of binary format
- `load_auto_detects_json_format` - Auto-detection of JSON format

**Corruption Handling:**
- `binary_load_corrupted_rejected` - Rejection of corrupted binary files
- `binary_load_truncated_rejected` - Rejection of truncated binary files

**Version Checking:**
- `check_file_version_returns_correct_version` - Version checking
- `check_file_version_empty_file_returns_error` - Empty file handling
- `check_all_versions_with_no_databases` - No database handling

**Configuration:**
- `config_init_defaults_populates_all_fields` - Default config values
- `config_save_load_roundtrip` - Config persistence
- `config_encoding_defaults_to_utf8` - Text encoding defaults
- `config_encoding_migrates_from_v3` - Config version migration

**Exclusions:**
- `exclusion_add_adds_pattern` - Adding exclusion patterns
- `exclusion_remove_removes_pattern` - Removing exclusion patterns
- `exclusion_check_matches_pattern` - Pattern matching
- `exclusion_check_rejects_non_matching` - Non-matching rejection
- `exclusion_init_defaults_adds_defaults` - Default exclusions

**Heuristics:**
- `heur_calculate_score_returns_higher_for_recent` - Scoring algorithm
- `heur_find_locates_entry_by_search` - Finding entries
- `heur_find_best_finds_best_match` - Best match finding

**Drive Operations:**
- `drive_backup_create_restore_roundtrip` - Drive backup/restore
- `find_drive_returns_correct_drive` - Drive lookup
- `find_drive_returns_null_for_missing` - Missing drive handling

**Text Encoding:**
- `text_encoding_default_is_utf8` - Default encoding
- `text_encoding_set_get_roundtrip` - Encoding get/set
- `text_encoding_invalid_value_ignored` - Invalid encoding handling
- `binary_save_load_utf8_no_bom` - UTF-8 without BOM
- `binary_save_load_utf16_with_bom` - UTF-16 with BOM
- `binary_load_rejects_bom_encoding_mismatch` - BOM/encoding mismatch rejection

**Version Consistency:**
- `saved_database_version_matches_current` - Atomic save verification
- `saved_database_version_is_correct_binary_version` - Binary version check

**Exclusion Filtering:**
- `filter_excluded_removes_matching_directories` - Directory filtering
- `filter_excluded_keeps_non_matching_directories` - Non-matching preservation
- `filter_excluded_updates_parent_indices` - Parent index update

### Matcher Tests (test_matcher.c - 23 tests)

**Basic Matching:**
- `match_single_component` - Single-component search matching
- `match_two_components` - Multi-component path matching
- `match_case_insensitive` - Case-insensitive matching
- `match_with_hidden_filter` - Hidden directory filtering
- `match_no_results` - Empty result handling
- `match_empty_search` - Empty search handling

**Prefix Matching:**
- `match_prefix_single_component` - Single component prefix
- `match_prefix_multi_component` - Multi-component prefix

**Glob Patterns:**
- `match_glob_star_suffix` - Suffix wildcard (`driv*`)
- `match_glob_star_prefix` - Prefix wildcard (`*loads`)
- `match_glob_star_both` - Both sides wildcard (`*own*`)
- `match_glob_question_single` - Single char wildcard (`Sys?em32`)
- `match_glob_question_multiple` - Multiple char wildcards
- `match_glob_in_path` - Wildcards in path components
- `match_glob_no_match` - No match handling

**Fuzzy Matching:**
- `fuzzy_match_with_typo` - Typo tolerance
- `fuzzy_match_with_transposition` - Transposed letters
- `fuzzy_match_no_results_on_total_mismatch` - Total mismatch handling
- `fuzzy_match_case_difference` - Case difference tolerance

**Name Index:**
- `name_index_build_returns_non_null` - Index building
- `name_index_find_by_hash_finds_entry` - Hash-based lookup
- `name_index_find_by_hash_misses_unknown_hash` - Unknown hash handling
- `name_index_free_doesnt_crash` - Cleanup safety

### Scanner Tests (test_scanner.c - 9 tests)

- `scan_mount_populates_database` - Basic directory scanning
- `scan_mount_respects_hidden_flag` - Hidden directory filtering during scan
- `scan_mount_applies_exclusions` - Exclusion list acceptance
- `scan_mount_excludes_directories_from_database` - Excluded directories NOT added
- `scan_subdirectory_merges_into_existing_db` - Partial scanning merges correctly
- `find_is_directory_returns_true_for_dirs` - Directory detection helper
- `find_is_hidden_detects_hidden_entries` - Hidden attribute detection
- `find_is_reparse_detects_symlinks` - Symlink/junction detection
- `scan_mounts_handles_empty_mount_list` - Empty mount list handling

### Metadata Tests (test_metadata.c - 11 tests)

- `metadata_create_returns_valid_struct` - Metadata creation
- `metadata_save_load_roundtrip` - Full save/load cycle
- `metadata_exists_returns_false_before_save` - File existence check
- `metadata_preserves_config_section` - Config preservation
- `metadata_preserves_groups_section` - Groups preservation
- `metadata_preserves_exclusions_section` - Exclusions preservation
- `metadata_preserves_heuristics_section` - Heuristics preservation
- `metadata_preserves_history_section` - History preservation
- `metadata_cleanup_legacy_returns_0_with_none` - Legacy cleanup
- `metadata_free_handles_null` - Null handling
- `metadata_group_remove_path` - Group removal

### Platform Tests (test_platform.c - 12 tests)

**String Utilities:**
- `strcasestr_finds_substring` - Case-insensitive substring search
- `strcasestr_case_insensitive` - Case insensitivity
- `strcasestr_returns_null_on_miss` - Miss handling

**Drive Handling:**
- `is_drive_specifier_valid` - Valid drive specifiers
- `is_drive_specifier_invalid` - Invalid drive specifiers
- `parse_drive_from_search_extracts_letter` - Drive extraction
- `parse_drive_from_search_no_drive` - No drive handling
- `build_mount_path_valid_letter` - Mount path building
- `filter_available_drives_filters_correctly` - Drive filtering

**Filesystem:**
- `is_pseudo_fs_matches_known_types` - Pseudo filesystem detection

**Application:**
- `get_app_title_returns_non_null` - App title
- `db_default_path_returns_valid_path` - Default database path

### Extended Platform Tests (test_platform_extended.c - 34 tests)

**Drive Handling Extended:**
- `parse_drive_from_search_with_backslash` - Windows path style
- `parse_drive_from_search_with_forward_slash` - Mixed path style
- `parse_drive_from_search_empty_after_drive` - Drive with no path
- `parse_drive_from_search_lowercase_drive` - Lowercase drive letter
- `is_drive_specifier_with_backslash` - Drive with backslash
- `is_drive_specifier_lowercase` - Lowercase specifier
- `build_mount_path_drive_z` - Z drive handling
- `build_mount_path_drive_a` - A drive handling

**Available Drives:**
- `get_available_drives_returns_valid_count` - Drive enumeration
- `get_available_drives_with_small_buffer` - Buffer size handling

**Drive Filtering Extended:**
- `filter_available_drives_empty_input` - Empty input handling
- `filter_available_drives_skip_all` - Skip all drives
- `filter_available_drives_keep_all` - Keep all drives
- `filter_available_drives_small_output_buffer` - Output buffer limits

**Pseudo Filesystem Extended:**
- `is_pseudo_fs_devpts` - devpts detection
- `is_pseudo_fs_cgroup2` - cgroup2 detection
- `is_pseudo_fs_overlay` - overlay detection
- `is_pseudo_fs_aufs` - aufs detection
- `is_pseudo_fs_null` - NULL handling
- `is_pseudo_fs_empty` - Empty string handling
- `is_pseudo_fs_case_insensitive` - Case insensitivity

**String Utilities Extended:**
- `strcasestr_at_start` - Match at string start
- `strcasestr_at_end` - Match at string end
- `strcasestr_multiple_occurrences` - Multiple matches
- `strcasestr_empty_needle` - Empty needle handling
- `strcasestr_needle_longer_than_haystack` - Needle larger than haystack
- `strncasecmp_equal_strings` - Equal string comparison
- `strncasecmp_different_strings` - Different string comparison
- `strncasecmp_partial_match` - Partial match comparison
- `strncasecmp_zero_length` - Zero length comparison

**Database Path Tests:**
- `db_default_path_returns_ncd_in_path` - Path validation
- `db_drive_path_c_drive` - C drive path
- `db_drive_path_z_drive` - Z drive path

**Help Text Tests:**
- `get_app_title_returns_string` - Title content validation
- `write_help_suffix_returns_non_negative` - Suffix writing
- `write_help_suffix_small_buffer` - Small buffer handling

**Mount Enumeration:**
- `enumerate_mounts_with_null_params` - NULL parameter handling
- `enumerate_mounts_zero_max` - Zero max mounts

### String Builder Tests (test_strbuilder.c - 15 tests)

- `init_sets_valid_state` - Initialization
- `append_single_string` - Single string append
- `append_multiple_strings_concatenates` - Multiple appends
- `appendn_truncates_at_n_bytes` - Length-limited append
- `appendc_single_character` - Character append
- `appendf_formatted_output` - Formatted append
- `append_json_str_escapes_quotes` - JSON quote escaping
- `append_json_str_escapes_backslash` - JSON backslash escaping
- `append_json_str_escapes_control_chars` - JSON control char escaping
- `clear_resets_length_but_keeps_buffer` - Clear operation
- `steal_transfers_ownership` - Buffer stealing
- `dup_copies_buffer` - Buffer duplication
- `ensure_cap_grows_buffer` - Capacity growth
- `large_append_triggers_realloc` - Reallocation
- `free_then_init_cycle` - Reuse cycle

### Extended String Builder Tests (test_strbuilder_extended.c - 39 tests)

**Initialization:**
- `init_with_null_buffer` - NULL buffer handling
- `init_multiple_independent` - Multiple builders

**Append Extended:**
- `append_empty_string` - Empty string append
- `append_null_pointer` - NULL pointer handling
- `append_exactly_fills_capacity` - Exact capacity fill
- `append_triggers_realloc` - Reallocation trigger
- `append_unicode_characters` - UTF-8 handling

**Appendn Extended:**
- `appendn_zero_bytes` - Zero byte append
- `appendn_exceeds_source_length` - Exceed source length
- `appendn_exact_length` - Exact length append

**Appendc Extended:**
- `appendc_null_character` - Null character append
- `appendc_special_characters` - Special characters
- `appendc_many_characters` - Many characters stress test

**Appendf Extended:**
- `appendf_empty_format` - Empty format string
- `appendf_no_format_args` - No format arguments
- `appendf_multiple_format_args` - Multiple format args
- `appendf_large_output` - Large output generation
- `appendf_hex_and_octal` - Hex and octal formatting

**JSON Escaping Extended:**
- `append_json_str_null` - NULL string handling
- `append_json_str_empty` - Empty string JSON
- `append_json_str_quotes` - Quote escaping
- `append_json_str_backslash` - Backslash escaping
- `append_json_str_all_control_chars` - All control chars
- `append_json_str_unicode` - Unicode handling
- `append_json_str_high_control_chars` - High control chars (0x00-0x1F)

**Clear Extended:**
- `clear_on_empty_builder` - Clear empty builder
- `clear_then_append` - Clear and reuse
- `clear_preserves_capacity` - Capacity preservation

**Steal/Dup Extended:**
- `steal_returns_allocated_memory` - Buffer stealing
- `steal_from_empty_builder` - Steal from empty
- `dup_returns_copy` - Buffer duplication
- `dup_empty_builder` - Dup empty builder

**Capacity Management:**
- `ensure_cap_same_capacity` - Same capacity request
- `ensure_cap_less_than_current` - Smaller capacity request
- `ensure_cap_huge_request` - Very large capacity

**Free Extended:**
- `free_null_pointer` - NULL free
- `free_empty_builder` - Free empty builder
- `double_free_safe` - Double free safety

**Complex Usage:**
- `append_then_clear_multiple_times` - Multiple cycles
- `json_object_building` - JSON object construction
- `rapid_append_and_clear` - Rapid operations

### Common/Memory Tests (test_common.c - 7 tests)

- `malloc_returns_non_null` - Basic allocation
- `calloc_returns_zeroed_memory` - Zeroed allocation
- `realloc_grows_allocation` - Reallocation with preservation
- `strdup_copies_string` - String duplication
- `malloc_array_with_safe_sizes` - Array allocation
- `mul_overflow_check_detects_overflow` - Multiplication overflow check
- `add_overflow_check_detects_overflow` - Addition overflow check

### History Tests (test_history.c - 14 tests)

- `history_add_single` - Single entry addition
- `history_add_multiple` - Multiple entries (most recent first)
- `history_add_duplicate_moves_to_top` - Duplicate handling
- `history_add_same_case_insensitive` - Case-insensitive duplicates
- `history_add_max_limit` - Maximum entry limit
- `history_get_out_of_bounds` - Bounds checking
- `history_clear` - Clear operation
- `history_remove_by_index` - Remove by index
- `history_remove_first` - Remove first entry
- `history_remove_last` - Remove last entry
- `history_remove_out_of_bounds` - Invalid index handling
- `history_remove_from_empty` - Empty list removal
- `history_swap_first_two` - Swap first two entries
- `history_swap_with_less_than_two` - Insufficient entries handling

### Extended Common/Memory Tests (test_common_extended.c - 37 tests)

**Malloc Extended:**
- `malloc_zero_size` - Zero size allocation
- `malloc_large_size` - Large allocation (1MB)
- `malloc_multiple_allocations` - Multiple allocations

**Calloc Extended:**
- `calloc_zero_size` - Zero size calloc
- `calloc_zero_nmemb` - Zero members
- `calloc_single_large` - Single large object
- `calloc_many_small` - Many small objects
- `calloc_size_overflow_check` - Overflow detection

**Realloc Extended:**
- `realloc_null_pointer` - NULL pointer realloc
- `realloc_zero_size` - Zero size realloc
- `realloc_shrink` - Buffer shrinking
- `realloc_grow_preserves_data` - Growth with preservation
- `realloc_multiple_times` - Multiple reallocations

**Strdup Extended:**
- `strdup_empty_string` - Empty string dup
- `strdup_single_char` - Single character
- `strdup_long_string` - Long string (10KB)
- `strdup_special_chars` - Special characters
- `strdup_modifications_independent` - Independence check

**Malloc Array Extended:**
- `malloc_array_normal` - Normal array allocation
- `malloc_array_zero_nmemb` - Zero members
- `malloc_array_zero_size` - Zero size
- `malloc_array_large` - Large array (1MB)
- `malloc_array_struct_array` - Struct array

**Overflow Checks Extended:**
- `mul_overflow_check_zero` - Zero multiplication
- `mul_overflow_check_one` - Identity multiplication
- `mul_overflow_check_large_values` - Large values
- `add_overflow_check_zero` - Zero addition
- `add_overflow_check_commutative` - Commutative property
- `add_overflow_check_chained` - Chained additions
- `add_overflow_check_large_values` - Large values

**Stress Tests:**
- `alloc_free_stress` - Rapid alloc/free (10K iterations)
- `calloc_realloc_stress` - Calloc/realloc stress
- `strdup_stress` - Strdup stress (1K iterations)

**Edge Cases:**
- `pointer_alignment` - Pointer alignment
- `malloc_array_overflow_detection` - Overflow detection

### Extended CLI Parsing Tests (test_cli_parse_extended.c - 31 tests)

**History Commands:**
- `history_pingpong_flag` - /0 ping-pong
- `history_index_1_to_9` - /1 through /9
- `history_list_flag` - /hl list
- `history_clear_flag` - /hc clear
- `history_remove_by_index` - /hc# remove
- `history_remove_invalid_index_rejected` - Invalid index rejection

**Group Commands:**
- `group_set_requires_at_name` - /g @name
- `group_set_rejects_without_at` - Reject without @
- `group_remove_requires_at_name` - /g- @name
- `group_list_dash_variant` - -gl variant
- `group_list_uppercase` - /gL variant

**Exclusion Commands:**
- `exclusion_add_dash_variant` - -x pattern
- `exclusion_remove_dash_variant` - -x- pattern
- `exclusion_list_dash_variant` - -xl list
- `exclusion_add_slash_variant` - /x pattern

**Timeout and Retry:**
- `timeout_with_no_space` - /t30 form
- `timeout_dash_variant` - -t seconds
- `retry_count_parsing` - /retry n
- `retry_count_dash_variant` - -retry n

**Combined Flags:**
- `combined_flags_iz` - /iz (hidden + fuzzy)
- `combined_flags_sz` - /sz (system + fuzzy)
- `combined_flags_isz` - /isz (all three)
- `combined_flags_all_four` - /iszv (all four)

**Agent Commands Extended:**
- `agent_mode_without_subcommand_rejected` - Missing subcommand
- `agent_complete_with_limit` - complete --limit
- `agent_mkdir_with_path` - mkdir path
- `agent_mkdirs_with_file` - mkdirs --file
- `agent_check_all_flags` - check --all
- `agent_tree_with_flat_and_depth` - tree --flat --depth
- `agent_ls_with_dirs_only` - ls --dirs-only
- `agent_ls_with_files_only` - ls --files-only

**Edge Cases:**
- `empty_string_arg_rejected` - Empty argument
- `multiple_search_terms_not_allowed` - Multiple terms
- `dash_help_long_form` - --help

### Extended Integration Tests (test_integration_extended.c - 19 tests)

**Database Integration:**
- `database_create_add_save_load_roundtrip` - Full workflow
- `database_multiple_drives` - Multiple drives
- `database_empty_save_load` - Empty database

**Matcher Integration:**
- `matcher_finds_across_drives` - Cross-drive search
- `matcher_with_hidden_directories` - Hidden filtering
- `matcher_with_system_directories` - System filtering
- `matcher_multi_component_path` - Multi-component paths
- `matcher_fuzzy_typo_tolerance` - Typo tolerance

**Metadata Integration:**
- `metadata_full_workflow` - Complete workflow
- `metadata_groups_multiple_paths` - Group updates
- `metadata_exclusion_patterns` - Pattern matching

**Exclusion Filtering:**
- `exclusion_filtering_removes_directories` - Directory filtering

**Name Index:**
- `name_index_build_and_search` - Index building/searching

### Service Lifecycle Tests (test_service_lifecycle.c - 13 tests)

**Basic Lifecycle:**
- `service_status_when_stopped` - Status when not running
- `service_start_when_stopped` - Start service
- `service_double_start_fails` - Prevent double start
- `service_stop_when_running` - Stop running service
- `service_stop_when_already_stopped` - Stop when already stopped
- `service_restart` - Stop and restart

**IPC Connectivity:**
- `service_ipc_ping` - IPC ping when running
- `service_ipc_ping_when_stopped` - IPC ping when stopped
- `service_ipc_get_version` - Version query
- `service_ipc_get_state_info` - State info query
- `service_ipc_shutdown_request` - Graceful shutdown

**State Management:**
- `service_state_progression` - STARTING → LOADING → READY progression
- `service_termination_graceful_then_force` - Graceful then force termination

### Service Integration Tests (test_service_integration.c - 12 tests)

**Help Output:**
- `help_shows_standalone_when_service_stopped` - Standalone status in help
- `help_shows_service_running_when_service_active` - Service status in help
- `help_includes_exclusion_and_agent_options` - Help content verification

**Agent Service Status:**
- `agent_service_status_not_running` - Plain text when stopped
- `agent_service_status_json_not_running` - JSON when stopped
- `agent_service_status_running` - Plain text when running
- `agent_service_status_json_running` - JSON when running
- `agent_service_status_after_stop` - Status after stop

**Operation Parity:**
- `ncd_search_works_without_service` - Standalone operation
- `ncd_search_works_with_service` - Service-backed operation
- `a_flag_is_not_agent_alias` - `/a` flag parsing
- `agentic_debug_mode_with_service_exits_cleanly` - Debug mode

## Test Framework

The custom test framework (`test_framework.h`) provides:

- `TEST(name)` - Declare a test function
- `RUN_TEST(name)` - Execute a test and track results
- `ASSERT_*` macros - Various assertion types:
  - `ASSERT_TRUE(expr)` / `ASSERT_FALSE(expr)`
  - `ASSERT_EQ_INT(expected, actual)` / `ASSERT_NE_INT(expected, actual)`
  - `ASSERT_EQ_STR(expected, actual)` / `ASSERT_STR_CONTAINS(haystack, needle)`
  - `ASSERT_NULL(ptr)` / `ASSERT_NOT_NULL(ptr)`
- `TEST_MAIN(...)` - Generate main() with summary

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make` or `make all` | Build all test executables |
| `make test` | Run unit tests |
| `make corruption` | Run database corruption tests |
| `make bugs` | Run bug detection tests |
| `make fuzz` | Run fuzz tests (60 second timeout) |
| `make bench` | Run performance benchmarks |
| `make recursive-mount` | Run recursive mount tests (requires root) |
| `make service-test` | Run service lifecycle and integration tests |
| `make all-environments` | **Comprehensive test across all 4 environments** |
| `make all-environments-standalone` | Tests without service (standalone mode) |
| `make features` | Comprehensive feature tests (ramdisk, requires root) |
| `make features-noroot` | Feature tests without ramdisk (uses /tmp) |
| `make clean` | Remove build artifacts |

## Platform-Specific Notes

### Windows

The tests compile with MSVC using the project source files. The PowerShell runner (`PowerShell/Run-Tests.ps1`) automatically builds and runs all tests.

Compile corruption test manually:
```batch
cl /nologo /W3 /O2 /Isrc /I. test_db_corruption.c test_framework.c src\database.c /Fe:test_db_corruption.exe
test_db_corruption.exe
```

For debug build with `/test NC` tests:
```batch
cl /nologo /W3 /O2 /DDEBUG /Isrc /I. test_db_corruption.c test_framework.c src\database.c /Fe:test_db_corruption.exe
```

### Linux/WSL

Tests use GCC with `-lpthread` for threading support. The Makefile is configured for Linux builds.

The recursive mount test requires root privileges:
```bash
sudo make recursive-mount
```

## Integration Tests (Wsl/test_features.sh, Win/test_features.bat)

Comprehensive end-to-end tests that verify NCD behavior through the actual executable:

### Category I: Exclusion Patterns

Tests that verify exclusion patterns prevent directories from being added to the database:

- `I1-I2` - Adding and listing exclusion patterns
- `I3` - Excluded directories not found in search
- `I4-I5` - Multiple exclusions work correctly
- `I6-I7` - Removing exclusions works
- `I8` - After removing exclusion, directory is findable again
- `I9` - Removing nonexistent exclusion doesn't crash
- **`I10a`** - **Excluded directories not found via regular search**
- **`I10b`** - **Excluded directories not shown in `/agent tree` output**

### Category V: Agent Tree

Tests for the `/agent tree` command output formats:

- `V1-V5` - JSON, flat, indented, and depth-limited output formats
- `V6-V10` - Error handling and path validation

### Category W: Agent Mode Commands (test_agent_commands)

Comprehensive tests for all `/agent` subcommands running on virtual disk:

**Agent Query Tests (W1-W4):**
- `W1` - query returns JSON with results
- `W2` - query with --limit returns results  
- `W3` - query nonexistent returns empty
- `W4` - query with --all flag works

**Agent List Tests (W5-W9):**
- `W5` - ls lists directories in path
- `W6` - ls --depth shows nested directories
- `W7` - ls --dirs-only works
- `W8` - ls --pattern filters results
- `W9` - ls fails on non-existent path

**Agent Tree Tests (W10-W14):**
- `W10` - tree --json returns valid JSON
- `W11` - tree --flat shows relative paths
- `W12` - tree --depth limits results
- `W13` - tree fails on non-existent path
- `W14` - tree requires path argument

**Agent Check Tests (W15-W19):**
- `W15` - check path exists returns success
- `W16` - check non-existent path fails
- `W17` - check --db-age returns data
- `W18` - check --stats returns data
- `W19` - check --service-status returns status

**Agent Complete Tests (W20-W22):**
- `W20` - complete returns suggestions
- `W21` - complete finds matching dirs
- `W22` - complete handles no matches

**Agent Mkdir Tests (W23-W26):**
- `W23` - mkdir creates single directory
- `W24` - mkdir creates nested directories
- `W25` - mkdir handles existing directory
- `W26` - mkdir fails on invalid path

**Agent Mkdirs Tests (W27-W30):**
- `W27` - mkdirs creates tree from flat file
- `W28` - mkdirs creates from JSON array
- `W29` - mkdirs creates from JSON object tree
- `W30` - mkdirs requires input

## Adding New Tests

1. Create a new test function using the `TEST(name)` macro
2. Use assertion macros (`ASSERT_TRUE`, `ASSERT_EQ_INT`, etc.)
3. Return 0 for success, non-zero for failure
4. Register the test in the appropriate suite with `RUN_TEST(name)`

Example:

```c
TEST(my_new_test) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Your test logic here */
    ASSERT_EQ_INT(1, db->version);
    
    db_free(db);
    return 0;
}

void suite_database(void) {
    RUN_TEST(create_and_free);
    /* ... other tests ... */
    RUN_TEST(my_new_test);  /* Add here */
}
```

## Continuous Integration

These tests are designed to run in CI environments:
- Return code 0 indicates all tests passed
- Return code non-zero indicates failures
- No interactive input required
- No system modifications (uses temp files only)

## Known Test Limitations

### Service Integration Tests

Some service integration tests may fail in automated environments because:

1. **Console Output Capture**: NCD writes its UI output directly to the Windows console using `CONOUT$` handle (via `WriteConsoleW`), not to stdout. This means:
   - Pipe capture fails: Tests using `popen()` or pipe redirection receive empty output
   - Help text tests fail: Tests looking for "NCD", "Standalone", "Service:" in output get nothing
   - Service status tests fail: Tests cannot verify service status from captured output

2. **Recommendation**: These tests are marked to skip gracefully when the service executable is not available or when running in CI environments.

## Success Criteria

Per the test suite design:

- [x] Unit tests achieve >70% code coverage (database and matcher modules)
- [x] Fuzz tests run 10,000+ iterations without crash
- [x] Database corruption tests verify proper rejection of malformed data
- [x] Recursive mount tests verify scanner doesn't hang or crash
- [x] All integration tests pass on Windows and Linux
- [x] Benchmarks show expected performance characteristics
- [x] Bug detection tests properly identify known issues
