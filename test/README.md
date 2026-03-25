# NCD Test Suite

This directory contains the comprehensive test suite for NewChangeDirectory (NCD).

## Quick Start

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
```

## Test Structure

```
test/
├── test_framework.h           # Minimal unit testing framework
├── test_framework.c           # Test framework implementation
├── test_database.c            # Database module tests
├── test_matcher.c             # Matcher module tests
├── test_db_corruption.c       # Targeted database corruption tests
├── test_service_lazy_load.c   # Service lazy loading tests
├── test_service_parity.c      # Service vs standalone parity tests
├── test_service_lifecycle.c   # Service start/stop lifecycle tests
├── test_service_integration.c # NCD client service integration tests
├── fuzz_database.c            # Fuzz testing for database loading
├── bench_matcher.c            # Performance benchmarks
├── Makefile                   # Test build system
├── Win/                       # Windows-specific tests
├── Wsl/                       # WSL-specific tests
└── PowerShell/                # PowerShell-specific tests
```

## Test Categories

### 1. Unit Tests (`test_*.c`)

These test individual modules in isolation:

- **test_database.c** - Database creation, manipulation, save/load
- **test_matcher.c** - Search matching algorithm
- **test_db_corruption.c** - Targeted database corruption tests
- **test_service_lazy_load.c** - Service lazy loading and state machine
- **test_service_parity.c** - Service vs standalone mode parity
- **test_service_lifecycle.c** - Service start/stop/restart operations
- **test_service_integration.c** - NCD client service status reporting

### 2. Fuzz Tests (`fuzz_*.c`)

These test robustness against malformed input:

- **fuzz_database.c** - 10,000+ iterations of random binary/JSON corruption

### 3. Database Corruption Tests (`test_db_corruption.c`)

Targeted corruption of specific database fields:
- Magic number corruption (change 'NCDB' to invalid)
- Version field corruption (invalid version numbers)
- Drive count overflow (0xFFFFFFFF, 0, large values)
- Directory count overflow
- Bit flipping in header
- File truncation at various points
- Appending garbage data

### 4. Recursive Mount Tests (`Wsl/test_recursive_mount.sh`)

Linux-specific mount stress tests:
- Self-recursive bind mounts (directory mounted on itself)
- Circular mount chains via symlinks
- Deeply nested mount points
- Special filesystem handling (/proc, /sys)
- Very deep directory structures (100+ levels)

**Note:** Requires root privileges for mount operations.

### 5. Performance Benchmarks (`bench_*.c`)

These measure performance characteristics:

- **bench_matcher.c** - Matcher query performance on synthetic databases

### 6. Integration Tests

Platform-specific end-to-end tests:

- **Win/test_integration.bat** - Windows CMD integration tests
- **Wsl/test_integration.sh** - Linux/WSL bash integration tests  
- **Wsl/test_recursive_mount.sh** - Recursive mount stress tests
- **PowerShell/Run-Tests.ps1** - PowerShell test runner with build automation

## Running Tests

### Linux/WSL

```bash
cd test
make test        # Unit tests
make fuzz        # Fuzz tests (60 second timeout)
make bench       # Performance benchmarks
make corruption  # Database corruption tests
```

### Windows (MSVC)

```batch
cd test
cl /nologo /W3 /O2 /Isrc /I. test_database.c test_framework.c src\database.c /Fe:test_database.exe
cl /nologo /W3 /O2 /Isrc /I. test_matcher.c test_framework.c src\matcher.c src\database.c /Fe:test_matcher.exe
test_database.exe
test_matcher.exe
```

### Windows (MinGW)

```batch
cd test
gcc -Wall -Wextra -I../src -I. -o test_database.exe test_database.c test_framework.c ../src/database.c -lpthread
gcc -Wall -Wextra -I../src -I. -o test_matcher.exe test_matcher.c test_framework.c ../src/matcher.c ../src/database.c -lpthread
test_database.exe
test_matcher.exe
```

## Test Coverage

### Database Tests (test_database.c)

- `create_and_free` - Database creation and cleanup
- `add_drive` - Adding drives to database
- `add_directory` - Adding directories with parent relationships
- `full_path_reconstruction` - Reconstructing full paths from parent chain
- `binary_save_load_roundtrip` - Binary format serialization/deserialization
- `binary_load_corrupted_rejected` - Rejection of corrupted binary files
- `binary_load_truncated_rejected` - Rejection of truncated binary files

### Matcher Tests (test_matcher.c)

- `match_single_component` - Single-component search matching
- `match_two_components` - Multi-component path matching
- `match_case_insensitive` - Case-insensitive matching
- `match_with_hidden_filter` - Hidden directory filtering
- `match_no_results` - Empty result handling
- `match_empty_search` - Empty search handling

### Scanner Tests (test_scanner.c)

- `scan_mount_populates_database` - Basic directory scanning
- `scan_mount_respects_hidden_flag` - Hidden directory filtering during scan
- `scan_mount_applies_exclusions` - Exclusion list is accepted by scanner
- `scan_mount_excludes_directories_from_database` - **Excluded directories are NOT added to database**
- `scan_subdirectory_merges_into_existing_db` - Partial scanning merges correctly
- `find_is_directory_returns_true_for_dirs` - Directory detection helper
- `find_is_hidden_detects_hidden_entries` - Hidden attribute detection
- `find_is_reparse_detects_symlinks` - Symlink/junction detection
- `scan_mounts_handles_empty_mount_list` - Empty mount list handling

### Mkdirs Tests (test_mkdirs.c)

Integration tests for the `/agent mkdirs` command that creates directory trees:

- `mkdirs_flat_format_simple` - Basic flat file format with 2-space indentation
- `mkdirs_flat_format_nested` - Deeply nested directory structures
- `mkdirs_flat_format_empty_lines` - Handling empty lines in flat format
- `mkdirs_json_format_simple` - JSON format with objects and children
- `mkdirs_json_format_nested` - Deeply nested JSON structures
- `mkdirs_json_format_string_array` - Simple JSON string array format
- `mkdirs_json_inline_argument` - JSON passed as command line argument
- `mkdirs_json_output_format` - JSON output with per-directory results
- `mkdirs_missing_file_error` - Error handling for missing input file
- `mkdirs_no_input_error` - Error handling when no input provided

## Test Framework

The custom test framework (`test_framework.h`) provides:

- `TEST(name)` - Declare a test function
- `RUN_TEST(name)` - Execute a test and track results
- `ASSERT_*` macros - Various assertion types
- `TEST_MAIN(...)` - Generate main() with summary

## Makefile Targets

| Target | Description |
|--------|-------------|
| `make` or `make all` | Build all test executables |
| `make test` | Run unit tests |
| `make corruption` | Run database corruption tests |
| `make fuzz` | Run fuzz tests (60 second timeout) |
| `make bench` | Run performance benchmarks |
| `make recursive-mount` | Run recursive mount tests (requires root) |
| `make service-test` | Run service lifecycle and integration tests |
| `make test_mkdirs` | Run mkdirs agent command integration tests |
| `make clean` | Remove build artifacts |

## Platform-Specific Notes

### Windows

The tests compile with MSVC using the project source files. The PowerShell runner (`PowerShell/Run-Tests.ps1`) automatically builds and runs all tests.

Compile corruption test manually:
```batch
cl /nologo /W3 /O2 /Isrc /I. test_db_corruption.c test_framework.c src\database.c /Fe:test_db_corruption.exe
test_db_corruption.exe
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

## Success Criteria

Per the Phase 4 plan:

- [x] Unit tests achieve >70% code coverage (database and matcher modules)
- [x] Fuzz tests run 10,000+ iterations without crash
- [x] Database corruption tests verify proper rejection of malformed data
- [x] Recursive mount tests verify scanner doesn't hang or crash
- [x] All integration tests pass on Windows and Linux
- [x] Benchmarks show expected performance characteristics
