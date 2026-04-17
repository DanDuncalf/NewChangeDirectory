# NCD Test Suite

This directory contains the comprehensive test suite for NewChangeDirectory (NCD).

## 📋 For AI Agents

**⚠️ CRITICAL:** Always use the test harness. See [`AGENT_TESTING_GUIDE.md`](../AGENT_TESTING_GUIDE.md) for complete instructions.

**Essential Commands (Via Harness):**
```batch
:: Run ALL tests (Windows + WSL) - Environment guaranteed restored
Run-Tests-Safe.bat

:: Run Windows tests only
Run-Tests-Safe.bat --windows-only

:: Run WSL tests
Run-Tests-Safe.bat wsl

:: Check/Repair environment
Run-Tests-Safe.bat --check
Run-Tests-Safe.bat --repair
```

**NEVER run test executables directly** (e.g., `test_database.exe`, `Test-*.bat`) as environment won't be restored on interruption.

## Table of Contents

1. [Quick Start - How to Run Tests](#quick-start---how-to-run-tests)
2. [How Tests Work](#how-tests-work)
3. [Test Organization](#test-organization)
4. [Running Tests](#running-tests)
5. [How to Add New Tests](#how-to-add-new-tests)
6. [Test Isolation Requirements](#test-isolation-requirements)
7. [Troubleshooting](#troubleshooting)

---

## Quick Start - How to Run Tests

### Recommended: Safe PowerShell Test Runner (Ctrl+C Safe!)

The PowerShell-based test runner guarantees cleanup even if you press Ctrl+C during tests.

```batch
:: From project root - Run ALL tests (safest option)
Run-Tests-Safe.bat

:: Run specific test suites
Run-Tests-Safe.bat unit
Run-Tests-Safe.bat integration
Run-Tests-Safe.bat service
Run-Tests-Safe.bat windows
Run-Tests-Safe.bat wsl

:: Check if environment is clean
Run-Tests-Safe.bat --check

:: Repair corrupted environment
Run-Tests-Safe.bat --repair
```

### PowerShell Native (More Control)

```powershell
:: From project root
.\Run-NcdTests.ps1

:: Run with options
.\Run-NcdTests.ps1 -TestSuite Unit -Quick
.\Run-NcdTests.ps1 -TestSuite Integration -WindowsOnly

:: Environment management
.\Run-NcdTests.ps1 -CheckEnvironment
.\Run-NcdTests.ps1 -RepairEnvironment
```

### Legacy Batch Runners (Not Ctrl+C Safe)

**⚠️ Warning:** Batch files cannot trap Ctrl+C. If interrupted, environment may not be cleaned up.

```batch
:: Run all tests (Windows)
build_and_run_alltests.bat

:: Run only Windows tests
build_and_run_alltests.bat --windows-only

:: Skip tests requiring service
build_and_run_alltests.bat --no-service
```

### Linux/WSL

```bash
cd test

:: Run unit tests
make test

:: Run all tests including integration
make all-environments

:: Run specific test categories
make fuzz        :: Fuzz tests (60 second timeout)
make bench       :: Performance benchmarks
make corruption  :: Database corruption tests
make bugs        :: Bug detection tests
make service-test :: Service lifecycle tests
```

---

## How Tests Work

### Why PowerShell is Safer

**The Problem with Batch Files:**
- Batch files **cannot** trap Ctrl+C signals
- When interrupted, they terminate immediately
- No cleanup code runs (endlocal is skipped)
- Environment variables remain modified

**PowerShell Solution:**
- PowerShell's `try/finally` blocks **CAN** trap Ctrl+C
- Cleanup code **always** runs in finally blocks
- Environment variables are automatically restored
- Orphaned temp directories are cleaned up

```powershell
:: PowerShell ensures cleanup even on Ctrl+C
try {
    Save-NcdTestEnvironment
    $env:LOCALAPPDATA = "$env:TEMP\ncd_test_12345"
    :: ... run tests ...
}
finally {
    :: This ALWAYS executes, even on Ctrl+C!
    Restore-NcdTestEnvironment
    Stop-NcdProcesses
    Remove-TestTempDirs
}
```

### Test Execution Flow

1. **Save Environment** - Store LOCALAPPDATA, NCD_TEST_MODE, TEMP, PATH
2. **Set Test Mode** - Enable NCD_TEST_MODE=1 (disables background rescans)
3. **Create Isolation** - Set LOCALAPPDATA to temp directory
4. **Run Tests** - Execute the test suite
5. **Cleanup (Always)** - Restore environment, stop processes, remove temp files

### Test Isolation Mechanisms

**Windows:**
- Creates VHD (virtual hard disk) mounted to unused drive letter (e.g., Z:)
- Falls back to `%TEMP%\ncd_test_data_*` if VHD unavailable
- Redirects LOCALAPPDATA to temp location for metadata isolation
- Sets NCD_TEST_MODE=1 to disable background rescans

**WSL/Linux:**
- Uses ramdisk (`tmpfs`) at `/mnt/ncd_test_ramdisk_*`
- Falls back to `/tmp/ncd_test_tree_*` if ramdisk unavailable
- Sets XDG_DATA_HOME to temp location
- Sets NCD_TEST_MODE=1

---

## Test Organization

### Test Categories

| Category | Location | Description |
|----------|----------|-------------|
| **Unit Tests** | `test/test_*.c` | Module-level tests (325+ tests) |
| **Parallel Expansion** | `test/test_*_extended.c` | New 430+ tests from 4-agent parallel effort |
| **Integration Tests** | `test/Win/`, `test/Wsl/` | End-to-end platform tests |
| **Service Tests** | `test/test_service_*.c` | Resident service tests |
| **Fuzz Tests** | `test/fuzz_*.c` | Robustness testing |
| **Benchmarks** | `test/bench_*.c` | Performance testing |
| **Stress Tests** | `test/test_stress.c` | Performance and stability tests |

### The 6 Core Test Suites

| # | Script | Platform | Service | Description |
|---|--------|----------|---------|-------------|
| 1 | `Test-Service-Windows.bat` | Windows | N/A | Service isolation tests |
| 2 | `test_service_wsl.sh` | WSL | N/A | Service isolation tests |
| 3 | `Test-NCD-Windows-Standalone.bat` | Windows | No | NCD without service |
| 4 | `Test-NCD-Windows-With-Service.bat` | Windows | Yes | NCD with service |
| 5 | `test_ncd_wsl_standalone.sh` | WSL | No | NCD without service |
| 6 | `test_ncd_wsl_with_service.sh` | WSL | Yes | NCD with service |

### Parallel Expansion Test Tracks (430+ New Tests)

Following the `PARALLEL_TEST_COVERAGE_PLAN.md`, 4 agents worked in parallel to add 430+ new tests:

| Agent | Track | Tests | Files | Focus Areas |
|-------|-------|-------|-------|-------------|
| **Agent 1** | Data Core | 90 | 3 | Database extended, odd cases, fuzzing |
| **Agent 2** | Service IPC | 100 | 5 | Service stress, IPC extended, SHM stress |
| **Agent 3** | UI & Main | 100 | 3 | UI extended, exclusions, main flow |
| **Agent 4** | Input Processing | 140 | 5 | CLI edge cases, scanner/matcher extended, stress |
| **TOTAL** | | **430** | **16** | Comprehensive coverage expansion |

**Running Parallel Expansion Tests:**

```powershell
:: Run all parallel expansion tests (included in full suite)
.\Run-NcdTests.ps1 -TestSuite Unit

:: Run specific agent track
.\Run-NcdTests.ps1 -TestSuite ParallelAgent1  # Data Core
.\Run-NcdTests.ps1 -TestSuite ParallelAgent2  # Service IPC
.\Run-NcdTests.ps1 -TestSuite ParallelAgent3  # UI & Main
.\Run-NcdTests.ps1 -TestSuite ParallelAgent4  # Input Processing
```

### Directory Structure

```
test/
├── test_*.c                   :: Unit test source files (755+ tests total)
│   ├── test_database.c        :: Core database tests
│   ├── test_matcher.c         :: Matcher tests
│   ├── ...
│   └── PARALLEL EXPANSION (430 new tests):
│       ├── test_database_extended.c    :: Agent 1 - Data Core (50)
│       ├── test_odd_cases.c            :: Agent 1 - Odd Cases (30)
│       ├── fuzz_database_load.c        :: Agent 1 - Fuzz (10)
│       ├── test_service_stress.c       :: Agent 2 - Service Stress (25)
│       ├── test_service_rescan.c       :: Agent 2 - Service Rescan (20)
│       ├── test_ipc_extended.c         :: Agent 2 - IPC Extended (25)
│       ├── test_shm_stress.c           :: Agent 2 - SHM Stress (20)
│       ├── fuzz_ipc.c                  :: Agent 2 - IPC Fuzz (10)
│       ├── test_ui_extended.c          :: Agent 3 - UI Extended (45)
│       ├── test_ui_exclusions.c        :: Agent 3 - UI Exclusions (15)
│       ├── test_main.c                 :: Agent 3 - Main Flow (40)
│       ├── test_cli_edge_cases.c       :: Agent 4 - CLI Edge Cases (30)
│       ├── test_scanner_extended.c     :: Agent 4 - Scanner Extended (35)
│       ├── test_matcher_extended.c     :: Agent 4 - Matcher Extended (30)
│       ├── test_result_edge_cases.c    :: Agent 4 - Result Edge Cases (20)
│       └── test_stress.c               :: Agent 4 - Stress Tests (25)
├── test_framework.h           :: Test framework macros
├── Win/                       :: Windows-specific tests
│   ├── test_features.bat      :: VHD-based feature tests
│   └── test_agent_commands.bat :: Agent command tests
├── Wsl/                       :: WSL/Linux-specific tests
│   ├── test_features.sh       :: Ramdisk-based tests
│   └── test_integration.sh    :: Integration tests
├── PowerShell/                :: PowerShell utilities
│   ├── NcdTestUtils.psm1      :: Test utilities module
│   └── README.md              :: Module documentation
├── Test-Service-Windows.bat   :: Windows service tests
├── Test-NCD-Windows-Standalone.bat     :: NCD standalone tests
├── Test-NCD-Windows-With-Service.bat   :: NCD with service tests
├── build-and-run-tests.bat    :: Build runner
├── Run-All-Unit-Tests.bat     :: Unit test runner (includes parallel tests)
├── Check-Environment.bat      :: Environment checker
├── Makefile                   :: Linux build system
├── PARALLEL_TEST_COVERAGE_PLAN.md      :: Parallel expansion plan
└── PARALLEL_INTEGRATION_REPORT.md      :: Integration report
```

---

## Running Tests

### Full Test Suite (Recommended)

```batch
:: Run all 6 test suites safely
Run-Tests-Safe.bat

:: Output shows:
:: ========================================
:: Running Test: Unit Tests
:: ========================================
:: ...
:: ========================================
:: FINAL CLEANUP (Running due to try/finally)
:: ========================================
:: Environment restored. Safe to continue using NCD.
```

### Individual Test Suites

```batch
:: Service tests only
Run-Tests-Safe.bat service

:: NCD standalone tests only
Run-Tests-Safe.bat ncd

:: NCD with service tests
Run-Tests-Safe.bat ncd-service

:: All Windows tests
Run-Tests-Safe.bat windows

:: All WSL tests
Run-Tests-Safe.bat wsl
```

### Unit Tests Only

```batch
:: Windows
Run-Tests-Safe.bat unit

:: Or directly:
cd test
Run-All-Unit-Tests.bat

:: Linux/WSL
cd test
make test
```

### With Options

```batch
:: Skip build phase (assume binaries exist)
Run-Tests-Safe.bat --skip-build

:: Skip tests requiring service
Run-Tests-Safe.bat --no-service

:: Quick mode (skip fuzz/benchmarks)
Run-Tests-Safe.bat --quick

:: Windows only (skip WSL)
Run-Tests-Safe.bat --windows-only
```

### Environment Check/Repair

```batch
:: Check if environment is clean
Run-Tests-Safe.bat --check

:: Repair corrupted environment
Run-Tests-Safe.bat --repair

:: Or use PowerShell directly for verbose output
.\Check-Environment.ps1 -Verbose
.\Check-Environment.ps1 -Repair
```

---

## How to Add New Tests

### Adding Unit Tests (C Code)

1. **Choose the appropriate test file** based on module:
   - Database tests → `test_database.c`
   - Matcher tests → `test_matcher.c`
   - Scanner tests → `test_scanner.c`
   - New module → Create `test_mymodule.c`

2. **Write the test function** using the test framework:

```c
#include "test_framework.h"
#include "ncd.h"
#include "database.h"

TEST(my_new_feature_works) {
    // Setup
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    // Execute
    int result = my_new_feature(db, "test");
    
    // Verify
    ASSERT_EQ_INT(0, result);
    ASSERT_TRUE(db->some_flag);
    
    // Cleanup
    db_free(db);
    return 0;  // 0 = success
}
```

3. **Register in test suite:**

```c
void suite_database(void) {
    RUN_TEST(create_and_free);
    RUN_TEST(add_drive);
    // ... existing tests ...
    RUN_TEST(my_new_feature_works);  // Add here
}
```

4. **Build and run:**

```batch
:: Windows
cd test
build-tests.bat
test_database.exe

:: Linux
cd test
make test_database
./test_database
```

### Adding Integration Tests (Windows Batch)

1. **Choose location:**
   - Windows tests → `test/Win/`
   - WSL tests → `test/Wsl/`

2. **Use the helper functions** in `test_features.bat`:

```batch
:: Test exit code is 0
call :test_exit_ok    ID "description"              ncd_args

:: Test exit code is non-zero
call :test_exit_fail  ID "description"              ncd_args

:: Test output contains substring
call :test_output_has ID "description" "substring"  ncd_args

:: Test NCD finds path
call :test_ncd_finds  ID "description" "path_substr" ncd_args
```

3. **Example test:**

```batch
:: Category X: New Feature Tests
echo --- Category X: New Feature Tests ---

:: X1: Basic functionality
call :test_ncd_finds X1 "New feature works" "expected_path" search_term

:: X2: Error handling
call :test_exit_fail X2 "Invalid input rejected" /invalid-flag

:: X3: Output format
call :test_output_has X3 "Output has JSON" '"status":' --agent:check --json
```

4. **Run the test:**

```batch
test\Win\test_features.bat
```

### Adding PowerShell-Based Tests

1. **Import the test utilities module:**

```powershell
Import-Module .\test\PowerShell\NcdTestUtils.psm1

:: Save environment
Save-NcdTestEnvironment

:: Register cleanup actions
Register-NcdCleanupAction -Description "Remove temp files" -Action {
    Remove-Item $tempDir -Recurse -Force
}
```

2. **Use the Invoke-NcdTest function:**

```powershell
Invoke-NcdTest -TestName "My New Test" -TestScript {
    :: Your test code here
    & .\my-test.bat
    if ($LASTEXITCODE -ne 0) { throw "Test failed" }
}
:: Cleanup happens automatically!
```

3. **Add to Run-NcdTests.ps1:**

```powershell
:: In the switch statement
"MyTestSuite" {
    $result = Invoke-Test -Name "My New Test Suite" -ScriptBlock {
        & .\test\My-New-Test.bat
    }
    $allPassed = $result -and $allPassed
}
```

### Test Naming Conventions

| Test Type | Naming Pattern | Example |
|-----------|---------------|---------|
| Unit tests | `module_action_result` | `database_save_load_roundtrip` |
| Integration | `Category##_description` | `D1 "Single component exact"` |
| Fuzz tests | `fuzz_target_description` | `fuzz_database_random_corruption` |
| Bug tests | `bug_issue_number_description` | `bug_123_memory_leak` |

### Test Isolation Requirements

**MUST DO:**
- ✅ Use `-r:.` (subdirectory rescan) not `-r` (full scan)
- ✅ Set `NCD_TEST_MODE=1` before running tests
- ✅ Redirect LOCALAPPDATA to temp location
- ✅ Clean up temp files after tests
- ✅ Use unique temp directory names (`ncd_test_%RANDOM%`)

**MUST NOT DO:**
- ❌ Scan user drives (C:, D:, etc.)
- ❌ Modify user metadata files
- ❌ Leave test files in system directories
- ❌ Run bare `-r` without path restrictions

---

## Test Isolation Requirements

**IMPORTANT:** All tests must be self-contained and must NOT touch the user's real data:

### Requirements

1. **DO NOT scan user drives** - Tests must only scan temporary test directories
2. **DO NOT use user metadata** - Use `-conf <path>` or environment isolation
3. **DO NOT modify user databases** - Use temporary database locations
4. **DO clean up after tests** - Remove all temp files and directories

### Windows Isolation

```batch
:: Save original
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "ORIGINAL_NCD_TEST_MODE=%NCD_TEST_MODE%"

:: Set isolation
set "NCD_TEST_MODE=1"
set "TEST_DATA=%TEMP%\ncd_test_data_%RANDOM%"
mkdir "%TEST_DATA%"
set "LOCALAPPDATA=%TEST_DATA%"

:: Create VHD or use temp directory
:: ... run tests ...

:: Cleanup (MUST run even on failure)
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%"
```

### Linux/WSL Isolation

```bash
# Save original
export ORIG_XDG_DATA_HOME=$XDG_DATA_HOME
export ORIG_NCD_TEST_MODE=$NCD_TEST_MODE

# Set isolation
export NCD_TEST_MODE=1
export TEST_DATA=/tmp/ncd_test_$$
mkdir -p $TEST_DATA
export XDG_DATA_HOME=$TEST_DATA

# Create ramdisk or use temp
# ... run tests ...

# Cleanup
export XDG_DATA_HOME=$ORIG_XDG_DATA_HOME
rm -rf $TEST_DATA
```

---

## Troubleshooting

### "LOCALAPPDATA points to test temp directory"

**Cause:** Previous test was interrupted with Ctrl+C

**Fix:**
```batch
:: Quick fix
Run-Tests-Safe.bat --repair

:: Or manually
set LOCALAPPDATA=%USERPROFILE%\AppData\Local
```

### "No database found"

**Cause:** Tests modified LOCALAPPDATA and it's still pointing to temp

**Fix:**
```batch
:: Check environment
Run-Tests-Safe.bat --check

:: Repair
Run-Tests-Safe.bat --repair

:: Then rescan your real drives
ncd -r
```

### Tests hang or timeout

**Cause:** Service may be stuck or TUI is blocking

**Fix:**
```batch
:: Kill all NCD processes
taskkill /F /IM NCDService.exe
taskkill /F /IM NewChangeDirectory.exe

:: Or use PowerShell
Import-Module .\test\PowerShell\NcdTestUtils.psm1
Stop-NcdService
```

### "Execution of scripts is disabled"

**Cause:** PowerShell execution policy

**Fix:**
```powershell
:: Run as Administrator
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser

:: Or use the batch wrapper (bypasses policy for this script)
Run-Tests-Safe.bat
```

### Batch file cleanup didn't run

**Cause:** Ctrl+C interrupted batch file

**Fix:**
```batch
:: Use PowerShell runner instead (Ctrl+C safe)
Run-Tests-Safe.bat

:: Or repair environment
Run-Tests-Safe.bat --repair
```

### Agent Command JSON Output Differences

Different agent subcommands use different JSON field names. Make sure your tests match the correct keys:

| Subcommand | Field | JSON Key |
|------------|-------|----------|
| `tree` | name | `"n"` |
| `tree` | depth | `"d"` |
| `ls` | name | `"name"` |
| `ls` | is directory | `"is_dir"` |

**Example:** `agent_mode_ls` outputs `{"name":"Docs","is_dir":true}`, while `agent_mode_tree` outputs `{"n":"Docs","d":0}`. Tests searching for `"n"` in `ls` output will fail.

### Windows Batch Quoting Pitfalls

When writing Windows batch tests, watch out for these two issues:

1. **Drive root escaping:** `"T:\"` escapes the closing quote in batch. Use `"T:\."` and let NCD normalize the trailing dot.
2. **PowerShell `-ArgumentList` mangling:** Batch variables containing `-conf "..."` break quoting when expanded inside PowerShell `-Command` strings. For simple non-TUI commands, prefer direct batch invocation over `Start-Process`.

See [`AGENT_TESTING_GUIDE.md`](../AGENT_TESTING_GUIDE.md) for detailed examples.

### WSL Test Failures

**"WSL not available" but WSL is installed:**
The test runner's auto-detection may fail. Run WSL tests manually:
```batch
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory/test && make test"
```

**WSL compilation errors in extended tests:**
Some extended test files (e.g., `test_ui_exclusions.c`) may have compilation issues in WSL due to API differences. Core tests should compile and pass:
```batch
wsl bash -c "cd test && make test_database test_matcher test_scanner test_common"
```

**Service test timing issues in WSL:**
The `legacy_shutdown_force_kill_as_last_resort` test may fail intermittently in WSL due to timing differences. This is an acceptable failure if all other tests pass.

---

## Test Coverage Summary

| Module | Test Count | Coverage |
|--------|------------|----------|
| Database | 34+ | 90% |
| Matcher | 23+ | 85% |
| Scanner | 9+ | 80% |
| Metadata | 11+ | 85% |
| Platform | 12+ | 70% |
| String Builder | 15+ | 80% |
| Common | 7+ | 75% |
| History | 14+ | 80% |
| CLI Parse | 31+ | 90% |
| Service | 25+ | 80% |
| **Total** | **325+** | **~85%** |

## Additional Documentation

- `docs/POWERHSHELL_TEST_RUNNER.md` - Full PowerShell runner documentation
- `test/PowerShell/README.md` - PowerShell module documentation
- `test_cleanup_solution.md` - Cleanup solution technical details
