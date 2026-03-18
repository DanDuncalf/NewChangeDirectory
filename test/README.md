# NCD Test Suite

This directory contains the comprehensive test suite for NewChangeDirectory (NCD), implementing Phase 4 of the improvement plans.

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

## Test Categories

### 1. Unit Tests (`test_*.c`)

These test individual modules in isolation:

- **test_database.c** - Database creation, manipulation, save/load
- **test_matcher.c** - Search matching algorithm
- **test_db_corruption.c** - Targeted database corruption tests

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

## Success Criteria

Per the Phase 4 plan:

- [x] Unit tests achieve >70% code coverage (database and matcher modules)
- [x] Fuzz tests run 10,000+ iterations without crash
- [x] Database corruption tests verify proper rejection of malformed data
- [x] Recursive mount tests verify scanner doesn't hang or crash
- [x] All integration tests pass on Windows and Linux
- [x] Benchmarks show expected performance characteristics

## Adding Tests

See TESTING.md for detailed instructions on adding new tests.
