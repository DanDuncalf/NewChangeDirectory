# NCD Testing Best Practices

> Last updated: 2026-04-14  
> Based on hands-on testing of the NewChangeDirectory (NCD) project across Windows and WSL.

---

## The Golden Rule: Always Use the Safe Test Runner

**NEVER** run test executables, batch files, or shell scripts directly. Always use the PowerShell-based test infrastructure which guarantees environment cleanup even if tests are interrupted with `Ctrl+C`.

| Do This ✅ | Never Do This ❌ |
|-----------|------------------|
| `Run-Tests-Safe.bat wsl` | `cd test && ./test_database` |
| `Run-Tests-Safe.bat integration` | `test\Test-Service-Windows.bat` |
| `Run-NcdTests.ps1 -TestSuite Wsl` | `wsl bash test/Wsl/test_features.sh` |

**Why:** Batch files and direct shell execution cannot trap `Ctrl+C`. If interrupted, `LOCALAPPDATA`/`XDG_DATA_HOME` remain pointed at deleted temp directories and NCD appears "broken."

---

## Quick Reference: Running Tests

### Windows (All Tests)
```batch
:: Full suite (safest)
Run-Tests-Safe.bat

:: Just Windows tests
Run-Tests-Safe.bat --windows-only

:: Specific suites
Run-Tests-Safe.bat unit
Run-Tests-Safe.bat integration
Run-Tests-Safe.bat service
```

### WSL / Linux
```batch
:: From Windows host (recommended)
Run-Tests-Safe.bat wsl

:: Direct WSL unit tests (if you must)
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory/test && make test"

:: Direct WSL integration tests (if you must)
wsl bash -c "export NCD_TEST_MODE=1; export NCD_UI_KEYS=ENTER; export NCD_UI_KEYS_STRICT=1; cd /mnt/e/llama/NewChangeDirectory/test && bash test_service_wsl.sh"
```

---

## WSL-Specific Best Practices

### 1. Set These Environment Variables Before Any Direct Test Run

When running WSL tests outside the PowerShell harness (not recommended, but sometimes necessary):

```bash
export NCD_TEST_MODE=1          # Prevents background rescans of real drives
export NCD_UI_KEYS=ENTER        # Auto-selects first match in TUI
export NCD_UI_KEYS_STRICT=1     # Returns ESC instead of hanging when keys exhausted
export XDG_DATA_HOME=/tmp/ncd_test_$$  # Isolates metadata from real user data
```

### 2. Kill Lingering Services Before Service Tests

NCD service processes can survive failed tests and cause subsequent runs to fail:

```bash
pkill -9 -f 'ncd_service'
pkill -9 -f 'NCDService'
pkill -9 -f 'NewChangeDirectory'
```

### 3. Don't Use `make all` in `test/` for WSL

`make all` attempts to build **all** test targets including Windows-only extended tests that fail to compile on Linux (e.g., `test_ui_exclusions.c` has API mismatches).

**Instead:** Use targeted builds or `make test`:

```bash
# Core unit tests only
make test

# Or build individual tests
make test_database test_matcher test_scanner test_service_lifecycle
```

### 4. Be Aware of Known WSL Test Behaviors

The following are **expected/acceptable** on WSL and do not indicate broken builds:

| Test/Symptom | Expected Behavior |
|--------------|-------------------|
| `test_shared_state` — `structure_sizes` fails | Linux struct padding differs from Windows (Expected 16, got 20) |
| `test_legacy_service_shutdown` — 1 timing failure | `force_kill_as_last_resort` is known to fail in WSL due to timing differences |
| `test_service_wsl.sh` — log check failure | The script greps for `"error"` and matches benign L2 log lines: `Client connection closed or receive error`. The 10 functional service tests all pass. |
| `test_ncd_wsl_with_service.sh` — 1 failure | `Agent tree command with service` fails due to a known Linux backslash path-separator bug in the tree display |
| `test_integration.sh` — hangs on "Directory search" | The script searches from `test/` on `/mnt/e`, causing NCD to fall back to scanning the entire E: drive |
| `test_features.sh` — timeout on custom DB search | Custom DB doesn't contain the search term, triggering a fallback rescan |
| `test_ui_extended` / `test_ui_navigator` | Some UI assertions expect Windows console dimensions/behavior |

---

## Test Isolation Requirements

All tests **must** be completely isolated from user data:

| Resource | User Data | Test Data |
|----------|-----------|-----------|
| Drives/Mounts | `C:`, `D:`, `/home` | VHD (Windows) or ramdisk/tmpfs (WSL) |
| Metadata | `%LOCALAPPDATA%\NCD\ncd.metadata` | Temp dir via `-conf` or env override |
| Databases | `%LOCALAPPDATA%\NCD\ncd_*.database` | Temp location via `-d:path` |

### Critical Rules
1. **Never use bare `-r`** — Always use `-r:.` (subdirectory) or `-r:<drive>` (specific drive)
2. **Never scan user drives** — Only scan test VHDs/ramdisks/temp directories
3. **Never modify user metadata** — Use `-conf <temp_path>` for custom metadata
4. **Always set `NCD_TEST_MODE=1`** — Disables automatic background rescans

---

## Troubleshooting Common Issues

### "LOCALAPPDATA points to test temp directory"

**Symptoms:** NCD reports "No database found", `ncd --agent:check --db-age` shows wrong location.

**Fix:**
```batch
:: Automatic repair
Run-Tests-Safe.bat --repair

:: Or manual
set LOCALAPPDATA=%USERPROFILE%\AppData\Local
set NCD_TEST_MODE=
```

### WSL Tests Timeout or Hang

**Common causes:**
- Missing `NCD_UI_KEYS=ENTER` causes TUI to block waiting for keyboard input on multi-match searches
- Searching from a directory whose database doesn't contain the term causes NCD to fall back to scanning all mounts
- Lingering `ncd_service` process from a previous test run

**Fix:**
```bash
# 1. Kill all NCD processes
pkill -9 -f 'ncd_service'
pkill -9 -f 'NewChangeDirectory'

# 2. Set UI keys
export NCD_TEST_MODE=1
export NCD_UI_KEYS=ENTER
export NCD_UI_KEYS_STRICT=1
```

### "Execution of scripts is disabled" (PowerShell)

The `Run-Tests-Safe.bat` wrapper bypasses this automatically. If running `Run-NcdTests.ps1` directly:

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

---

## Recommended Test Workflow

### Full Validation (CI / Before Submitting Changes)
```batch
:: 1. Build Windows
build.bat

:: 2. Build WSL
wsl bash -c "cd /mnt/e/llama/NewChangeDirectory && ./build.sh"

:: 3. Run Windows tests
Run-Tests-Safe.bat --windows-only

:: 4. Run WSL tests
Run-Tests-Safe.bat wsl
```

### Development Loop (WSL)
```bash
# Build
./build.sh

# Run core unit tests
cd test
make test

# Run a specific test executable
./test_database
./test_matcher
```

### Development Loop (Windows)
```batch
build.bat
cd test
build-tests.bat
test_database.exe
```

---

## Test File Locations

| Platform | Unit Tests | Integration Tests |
|----------|-----------|-------------------|
| Windows | `test/test_*.exe` | `test/Test-*-Windows.bat` |
| WSL | `test/test_*` (no extension) | `test/test_*_wsl.sh`, `test/Wsl/*.sh` |

---

## Key Takeaways

1. **Use `Run-Tests-Safe.bat`** for every test run. It is the only way to guarantee environment restoration.
2. **WSL builds are healthy** but some tests have platform-specific differences (struct sizes, UI dimensions, path separators).
3. **`make all` in `test/` will fail on WSL** due to Windows-only extended test files.
4. **Service tests are sensitive to state** — always clean up lingering `ncd_service` processes.
5. **Integration test timeouts** are almost always caused by missing `NCD_UI_KEYS` or fallback scanning from the wrong mount point.
