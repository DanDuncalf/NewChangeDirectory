# Agent Testing Guide for NCD

**Last Updated:** 2026-04-12

This guide provides step-by-step instructions for AI agents to run comprehensive tests on the NewChangeDirectory (NCD) project.

---

## ⚠️ CRITICAL: ALWAYS Use the Test Harness

**NEVER run test executables or batch files directly.** Always use the PowerShell test harness (`Run-Tests-Safe.bat` or `Run-NcdTests.ps1`) which provides:

1. **Environment Isolation** - Tests run with isolated `LOCALAPPDATA` and `NCD_TEST_MODE` variables
2. **Temporary/Virtual Disk Usage** - Integration tests use VHD (Windows) or ramdisk (WSL) for complete isolation
3. **Guaranteed Cleanup** - PowerShell's `try/finally` blocks ensure environment is restored even if tests crash or are interrupted with Ctrl+C
4. **No User Data Pollution** - Tests cannot accidentally scan or modify user's real drives or metadata

### Dangers of Running Tests Outside the Harness

| Risk | Consequence |
|------|-------------|
| Environment variables not restored | `LOCALAPPDATA` points to deleted temp directory, NCD appears broken |
| Real drives scanned | Test data pollutes user's database |
| User metadata modified | Groups, exclusions, history corrupted with test data |
| Service left running | Orphaned processes, resource leaks |
| No VHD isolation | Tests may fail due to permission issues on real filesystem |

### ✅ Correct Way to Run Tests
```batch
:: ALWAYS use the harness
cd /d E:\llama\NewChangeDirectory
Run-Tests-Safe.bat
```

### ⚠️ AI Agent Notes

**Timeout Limits:** Foreground shell commands have a 300-second timeout limit. The full test suite takes approximately 3-4 minutes. Use `run_in_background=true` with a longer timeout (e.g., 600s) when invoking the full suite.

**Prefer Run-All-Tests.bat when binaries exist:** If `test\*.exe` files are already present, use `cmd /c Run-All-Tests.bat` instead of `Build-And-Run-All-Tests.bat` to avoid unnecessary rebuild time. Only build if binaries are missing or the user explicitly requests a rebuild.

**Proper PowerShell syntax:** The Shell tool runs PowerShell. Do NOT use CMD redirection syntax like `2>nul` or pipes to `findstr`/`head` directly in PowerShell. Use native PowerShell cmdlets instead:
```powershell
# ❌ WRONG - PowerShell does not understand `2>nul` or `head`
dir test\*.exe 2>nul | findstr /i "\.exe" | head -20

# ✅ CORRECT - Use Get-ChildItem and Select-Object
Get-ChildItem -Path "test" -Filter "*.exe" | Select-Object -First 20 Name
```

### ❌ NEVER Do This
```batch
:: WRONG - Environment will not be restored!
cd test
test_database.exe
Test-NCD-Windows-Standalone.bat
```

---

## Quick Test Commands

### Run All Tests (Windows + WSL)
```batch
cd /d E:\llama\NewChangeDirectory
Run-Tests-Safe.bat
```

### Run Windows-Only Tests
```batch
cd /d E:\llama\NewChangeDirectory
Run-Tests-Safe.bat --windows-only
```

### Run Specific Test Suites
```batch
Run-Tests-Safe.bat unit              :: Unit tests only
Run-Tests-Safe.bat integration       :: Integration tests
Run-Tests-Safe.bat service           :: Service tests
Run-Tests-Safe.bat windows           :: All Windows tests
Run-Tests-Safe.bat wsl               :: All WSL tests
```

### Run Integration Tests Directly (Not Recommended)

> ⚠️ **WARNING:** Only run directly if you need detailed output AND can ensure cleanup. Prefer the harness.

```batch
:: Windows integration tests - run directly at your own risk
cd /d E:\llama\NewChangeDirectory\test
Test-Service-Windows.bat
Test-NCD-Windows-Standalone.bat
Test-NCD-Windows-With-Service.bat

:: If interrupted, repair:
Run-Tests-Safe.bat --repair
```

### Check/Repair Environment
```batch
Run-Tests-Safe.bat --check           :: Check environment only
Run-Tests-Safe.bat --repair          :: Repair corrupted environment
```

---

## Complete Test Suite Breakdown

When running "all tests", the following 11 suites execute. Report results using this format:

| # | Suite | Platform | Category | Typical Duration | Checks | Expected |
|---|-------|----------|----------|------------------|--------|----------|
| 1 | Unit Tests | Windows | Unit | < 1s | 356+ | PASS |
| 2 | Service Tests (Isolated) | Windows | Integration | ~5s | ~10 | PASS |
| 3 | NCD Standalone | Windows | Integration | ~3s | ~10 | PASS |
| 4 | NCD with Service | Windows | Integration | ~5s | ~10 | PASS |
| 5 | Windows Feature Tests | Windows | Integration | ~60s | ~68 | PASS |
| 6 | Windows Agent Command Tests | Windows | Integration | ~10s | ~40 | PASS |
| 7 | WSL Service Tests | WSL | Integration | ~25s | ~10 | PASS |
| 8 | WSL NCD Standalone | WSL | Integration | ~3s | ~15 | PASS |
| 9 | WSL NCD with Service | WSL | Integration | ~25s | ~16 | PASS |
| 10 | WSL Feature Tests | WSL | Integration | ~46s | ~49 | PASS |
| 11 | WSL Agent Command Tests | WSL | Integration | < 1s | ~31 | PASS |
| | **Total** | | | **~3:00-4:00** | **~615** | **11/11 PASS** |

### Expected Output Format — MANDATORY

When reporting results to the user, the **ONLY** acceptable format is a **complete per-test listing** with **check counts and ratios per suite**.

**Preferred method:** Run `python test/generate_report.py` from the project root.

This script is self-isolating (safe to run standalone):
- Creates a temp directory and redirects `LOCALAPPDATA` / `XDG_DATA_HOME`
- Sets `NCD_TEST_MODE=1`
- Stops any running NCD services before testing
- Restores environment and cleans up on exit (even on Ctrl+C)

It then produces the complete per-test report with check counts and ratios.

**Full build + test + report (recommended for CI):**
1. Run `cmd /c Build-And-Run-All-Tests.bat` for the safe full test cycle
2. Then run `python test/generate_report.py` for the detailed per-test breakdown

**Manual method (fallback):**
1. **Unit tests:** Run each `test_*.exe` directly and present a per-test table for every executable (test name + status).
2. **Integration tests:** List each suite with platform, duration, PASS/FAIL, and the **number of individual checks** performed in that suite.
3. **Ratios:** For every suite, report `X/Y passed | Z failed | W skipped`.
4. **Summaries:** Provide module-level and overall totals **only after** the full per-test listing.

**What counts as a "check":**
- Unit tests: each `TEST(name)` function is one check
- Integration suites: each `pass()`/`fail()` call, `call :test_*` helper, or `[TEST N]` label is one check
- Use `grep -cE 'pass|fail|TEST' <script>` to approximate check counts

**Do NOT** present only:
- `Total Suites: X | Passed: X | Failed: X`
- Aggregate counts without the individual test names
- Executable-level totals without breaking out each test inside
- Integration suites without check counts and pass/fail ratios

**Notes:**
- Some WSL suites may emit bash warnings (e.g., "ignored null byte in input") or show aborted processes. These are typically expected for negative test cases and do **not** indicate failures as long as the harness reports `[PASS]`.
- The total duration varies based on system load and WSL performance.

---

## Detailed Test Procedures

### 1. Unit Tests (Windows)

> ⚠️ **WARNING:** Do NOT run test executables directly (e.g., `test_database.exe`). Use the PowerShell runner which handles environment isolation and cleanup.

Unit tests are compiled as individual executables and test specific modules in isolation.

#### Option A: PowerShell Safe Runner (Recommended)
The PowerShell runner guarantees cleanup even if interrupted with Ctrl+C.

```batch
cd /d E:\llama\NewChangeDirectory
powershell -ExecutionPolicy Bypass -File "Run-NcdTests.ps1" -TestSuite All
```

**Test Suites Available:**
- `All` - Run everything
- `Unit` - Unit tests only
- `Integration` - Integration tests
- `Service` - Service lifecycle tests
- `Windows` - All Windows-specific tests
- `WSL` - WSL/Linux tests

#### Option B: Direct Unit Test Execution
Run individual test executables for detailed output:

```batch
cd /d E:\llama\NewChangeDirectory\test

test_database.exe          :: 50 database tests
test_matcher.exe           :: 23 matcher tests
test_scanner.exe           :: 9 scanner tests
test_strbuilder.exe        :: 15 string builder tests
test_common.exe            :: 7 common utility tests
test_platform.exe          :: 12 platform tests
test_cli_parse.exe         :: 31 CLI parsing tests
test_metadata.exe          :: 10 metadata tests
test_shared_state.exe      :: 10 shared state tests
test_bugs.exe              :: 15 bug regression tests
test_db_corruption.exe     :: 13 corruption handling tests
test_service_lifecycle.exe :: 13 service lifecycle tests
test_service_integration.exe :: 11 service integration tests
test_service_lazy_load.exe :: 22 service lazy load tests
test_service_version_compat.exe :: 6 version compat tests
test_legacy_service_shutdown.exe :: 7 legacy shutdown tests
```

#### Option C: Batch Test Runners
```batch
cd /d E:\llama\NewChangeDirectory\test
Run-All-Unit-Tests.bat           :: Run all unit tests
Run-All-Unit-Tests.bat --skip-fuzz :: Skip fuzz tests (faster)
```

### 2. Integration Tests (Windows)

> ⚠️ **WARNING:** Integration tests create VHD (virtual hard disk) for complete isolation. Running them directly will work but won't have the harness's guaranteed cleanup. Use the PowerShell runner when possible.

Integration tests are batch files in the `test\` directory that test end-to-end functionality:

#### Windows Integration Test Files
| Test File | Description | Typical Duration |
|-----------|-------------|------------------|
| `Test-Service-Windows.bat` | Service isolation tests (start/stop/status) | ~5s |
| `Test-NCD-Windows-Standalone.bat` | NCD client without service (12 tests) | ~15s |
| `Test-NCD-Windows-With-Service.bat` | NCD client with service running | ~15s |

#### Running Windows Integration Tests (Via Harness - Recommended)
```batch
cd /d E:\llama\NewChangeDirectory
Run-Tests-Safe.bat integration
:: OR
Run-Tests-Safe.bat windows
```

#### Running Windows Integration Tests (Direct - Not Recommended)
Only if you need detailed output and accept the risk:
```batch
cd /d E:\llama\NewChangeDirectory\test

:: These batch files DO have their own setlocal/endlocal but Ctrl+C will skip cleanup
Test-Service-Windows.bat
Test-NCD-Windows-Standalone.bat
Test-NCD-Windows-With-Service.bat

:: If interrupted, repair environment:
Run-Tests-Safe.bat --repair
```

#### Windows Integration Test Results (Expected)
| Test | Tests | Status |
|------|-------|--------|
| Service Tests | 1 suite | PASS |
| NCD Standalone | 12/12 | PASS |
| NCD With Service | 12/12 | PASS |

**Sample Output:**
```
--- Test Suite: Standalone Mode Operations ---
[TEST 1] Help with /h
[PASS] Help with /h
[TEST 2] Version with /v
[PASS] Version with /v
...
[TEST 12] Database override /d
[PASS] Database override /d

========================================
Test Summary
  Total:   12
  Passed:  12
  Failed:  0

RESULT: PASSED
```

---

### 3. Integration Tests (WSL/Linux)

WSL integration tests are shell scripts in `test/Wsl/`:

| Test File | Description |
|-----------|-------------|
| `test_integration.sh` | Basic integration (version, help, scan, search) |
| `test_agent_commands.sh` | Agent mode command tests |
| `test_features.sh` | Feature tests (groups, exclusions, history) |
| `test_recursive_mount.sh` | Mount point handling tests |

#### Running WSL Integration Tests
```batch
:: Must be run from project root
cd /d E:\llama\NewChangeDirectory

:: Basic integration test
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory && bash test/Wsl/test_integration.sh"

:: Agent commands test
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory && bash test/Wsl/test_agent_commands.sh"

:: Feature tests
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory && bash test/Wsl/test_features.sh"
```

#### WSL Integration Test Results (Expected)
| Test | Tests | Status | Notes |
|------|-------|--------|-------|
| test_integration.sh | 6/7 | Mostly PASS | 1 scan failure acceptable |
| test_agent_commands.sh | varies | Check output | May need service |
| test_features.sh | varies | Check output | May need root for mounts |

**Sample Output:**
```
=== NCD Integration Tests (Linux/WSL) ===

Test 1: Version display...
  PASS: Version displayed
Test 2: Help display...
  PASS: Help displayed
Test 3: Database scan...
  FAIL: Database scan failed
Test 4: History display...
  PASS: History displayed
Test 5: Clear history...
  PASS: History cleared
Test 6: Directory search...
  PASS: Search executed without crash
```

---

### 5. WSL/Linux Unit Tests

> ⚠️ **WARNING:** WSL tests run outside the PowerShell harness. Ensure you set `NCD_TEST_MODE=1` and isolate `XDG_DATA_HOME` when running directly, or use `Run-Tests-Safe.bat wsl` which handles this automatically.

#### Build in WSL
```batch
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory && ./build.sh"
```

#### Run Core Tests in WSL (Via Harness - Recommended)
```batch
Run-Tests-Safe.bat wsl
```

#### Run Core Tests in WSL (Direct - Not Recommended)
```batch
:: Isolated environment
wsl bash -c "export NCD_TEST_MODE=1; export XDG_DATA_HOME=/tmp/ncd_test_$$; cd /mnt/e/llama/NewChangeDirectory/test && make test; rm -rf /tmp/ncd_test_$$"
```

#### Run Individual Tests in WSL (Direct)
```batch
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory/test && ./test_database"
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory/test && ./test_matcher"
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory/test && ./test_scanner"
```

#### Build All Tests in WSL (may have some failures)
```batch
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory/test && make all 2>&1 | tail -100"
```

**Note:** Some extended test files may have compilation issues in WSL due to API differences. Core tests (test_database, test_matcher, test_scanner, etc.) should pass.

---

## Expected Test Results

### Windows Unit Tests (All Should Pass)
| Test Suite | Expected | Typical Duration |
|------------|----------|------------------|
| Unit Tests | 254+ pass | ~30 seconds |
| **Total** | **100% pass rate** | **~30 seconds** |

### Windows Integration Tests (All Should Pass)
| Test Suite | Tests | Expected | Typical Duration |
|------------|-------|----------|------------------|
| Service Tests (Isolated) | 1 suite | PASS | ~5s |
| NCD Standalone | 12/12 | PASS | ~3s |
| NCD with Service | 12/12 | PASS | ~5s |
| Windows Feature Tests | varies | PASS | ~60s |
| Windows Agent Command Tests | varies | PASS | ~10s |
| **Total** | **100% pass rate** | **~83 seconds** |

### WSL Integration Tests (All Should Pass)
| Test Suite | Tests | Expected | Typical Duration | Notes |
|------------|-------|----------|------------------|-------|
| WSL Service Tests | 1 suite | PASS | ~25s | |
| WSL NCD Standalone | varies | PASS | ~3s | |
| WSL NCD with Service | varies | PASS | ~25s | |
| WSL Feature Tests | varies | PASS | ~46s | |
| WSL Agent Command Tests | varies | PASS | < 1s | May show aborted processes (expected) |
| **Total** | **100% pass rate** | **~100 seconds** | |

### WSL Unit Tests (Most Should Pass)
| Test Suite | Expected | Notes |
|------------|----------|-------|
| Core tests | PASS | test_database, test_matcher, etc. |
| test_legacy_service_shutdown | 6/7 pass | 1 timing failure acceptable |
| Extended tests | May fail | Compilation issues with some files |

### Known Test Counts by Module
| Module | Test Count | Status |
|--------|------------|--------|
| Database | 50 | Should pass |
| Matcher | 23 | Should pass |
| Scanner | 9 | Should pass |
| CLI Parse | 31 | Should pass |
| Service Lifecycle | 13 | Should pass |
| Service Integration | 11 | Should pass |
| Service Lazy Load | 22 | Should pass |
| Platform | 12 | Should pass |
| Metadata | 10 | Should pass |
| Shared State | 10 | Should pass |
| String Builder | 15 | Should pass |
| Common | 7 | Should pass |
| Bug Regression | 15 | Should pass |
| DB Corruption | 13 | Should pass |
| Legacy Shutdown | 7 | 6 pass, 1 may fail (timing) |

---

## Test Output Interpretation

### Success Output
```
========================================
Tests: XX run, XX passed, 0 failed
Assertions: XXX total, 0 failed
```

### Failure Output
```
Tests: XX run, XX passed, X failed
Assertions: XXX total, X failed
```

### Service Test Output
Service tests may show colored output:
- `[0;32m...` = Green (success)
- `[1;33m...` = Yellow (warning/info)
- Red text indicates failures

---

## Troubleshooting

### "WSL not available" Warning
If the PowerShell runner reports "WSL not available" but WSL is installed:
- This is a detection issue in the test runner
- Run WSL tests manually using `wsl bash -c` commands

### Test Compilation Failures in WSL
Some extended test files may fail to compile in WSL due to:
- Different function signatures between Windows/Linux
- Missing headers
- API differences

**Solution:** Run core tests only: `make test_database test_matcher test_scanner ...`

### Service Test Failures
- Ensure no previous NCD service is running: `taskkill /F /IM NCDService.exe`
- Check environment: `Run-Tests-Safe.bat --check`
- Repair if needed: `Run-Tests-Safe.bat --repair`

### Environment Corruption
Symptoms: "LOCALAPPDATA points to test temp directory"
```batch
Run-Tests-Safe.bat --repair
:: Or manually:
set LOCALAPPDATA=%USERPROFILE%\AppData\Local
set NCD_TEST_MODE=
```

---

## Test Coverage Summary

| Category | Windows | WSL | Notes |
|----------|---------|-----|-------|
| Database | ✅ | ✅ | 50 tests |
| Matcher | ✅ | ✅ | 23 tests |
| Scanner | ✅ | ✅ | 9 tests |
| CLI | ✅ | ✅ | 31 tests |
| Service | ✅ | ✅ | 13 lifecycle + 11 integration |
| Platform | ✅ | ✅ | 12 tests |
| UI | ✅ | ⚠️ | Extended tests may fail to compile in WSL |
| Corruption | ✅ | ✅ | 13 tests |
| Bugs | ✅ | ✅ | 15 tests |

**Unit Tests Total:** 254+ tests across all modules
**Integration Suites:** 11 suites (5 Windows + 5 WSL + 1 combined Unit Tests)
**Overall:** 880+ tests total when including extended and integration tests

---

## Continuous Integration Checklist

Before marking changes as complete:

### Build Verification
- [ ] Windows build succeeds: `build.bat`
- [ ] WSL build succeeds: `wsl bash -c "./build.sh"`

### Unit Tests (Via Harness)
- [ ] Windows unit tests pass: `Run-Tests-Safe.bat --windows-only`
- [ ] WSL core tests pass: `Run-Tests-Safe.bat wsl`

### Integration Tests (Via Harness)
- [ ] Windows Integration: `Run-Tests-Safe.bat integration`
- [ ] Windows All: `Run-Tests-Safe.bat windows`
- [ ] WSL Integration: `Run-Tests-Safe.bat wsl`

### Final Verification
- [ ] No memory leaks (run under Valgrind in WSL if available)
- [ ] Service starts/stops correctly in both platforms

---

## Command Reference Summary

| Task | Windows Command (Via Harness) | WSL Command |
|------|-------------------------------|-------------|
| Build | `build.bat` | `wsl ./build.sh` |
| Full Test | `Run-Tests-Safe.bat` | `wsl make test` |
| Unit Tests | `Run-Tests-Safe.bat unit` | `wsl make test` |
| Integration | `Run-Tests-Safe.bat integration` | `Run-Tests-Safe.bat wsl` |
| Service Tests | `Run-Tests-Safe.bat service` | `wsl ./test_service_lifecycle` |
| Check Env | `Run-Tests-Safe.bat --check` | N/A |
| Repair Env | `Run-Tests-Safe.bat --repair` | N/A |

> ⚠️ **NOTE:** The harness uses PowerShell's `try/finally` to guarantee environment restoration. Direct execution of test files (e.g., `test_database.exe`, `Test-*.bat`) works but lacks guaranteed cleanup on interruption.

---

## Known Windows Batch Pitfalls

When writing or debugging Windows batch tests, be aware of these two common issues:

### 1. Drive-root batch quoting (`"T:\"` escapes the closing quote)
In batch, `"T:\"` is parsed as an opening quote followed by an escaped closing quote, which causes the rest of the command line to be misinterpreted. This breaks any test that passes a drive root like `T:\` as a quoted argument.

**Workaround:** Append a dot to the path in the batch file (`"T:\."`) and let the NCD executable normalize `T:\.` back to `T:\`.

```batch
:: WRONG - breaks command-line parsing
"%NCD%" /agent tree "%TESTROOT%" --depth 1

:: CORRECT - NCD normalizes T:\. to T:\
"%NCD%" /agent tree "%TESTROOT%." --depth 1
```

### 2. PowerShell `Start-Process -ArgumentList` strips arguments starting with `-`
When batch expands a variable such as `CONF_OVERRIDE=-conf "C:\...\ncd.metadata"` inside a PowerShell `-Command` string that is itself wrapped in batch double quotes, the `"` characters in the variable value terminate the outer batch quoting. This causes the `-conf` token to be stripped or mangled, making NCD fall back to the real user metadata location instead of the isolated test metadata.

**Symptoms:** Exclusions, groups, or database overrides appear to be ignored because NCD loads the wrong metadata file.

**Workaround:** For simple commands that do not open the TUI (e.g., `/r.`, `-x`, unique searches with a single match), invoke NCD directly from batch instead of via PowerShell `Start-Process`. Keep PowerShell only for commands that genuinely need a timeout kill switch (config editor, navigator, etc.).

```batch
:: WRONG - PowerShell ArgumentList may drop -conf
powershell -Command "... -ArgumentList '%CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10' ..."

:: CORRECT - direct batch call for a unique/no-match search
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10 >nul 2>&1
```

---

## Emergency Repair

If you accidentally ran tests outside the harness and the environment is corrupted:

```batch
:: Automatic repair
Run-Tests-Safe.bat --repair

:: Manual repair
set LOCALAPPDATA=%USERPROFILE%\AppData\Local
set NCD_TEST_MODE=
taskkill /F /IM NCDService.exe 2>nul
```

---

*This guide ensures comprehensive testing coverage across both Windows and WSL platforms with guaranteed environment safety.*
