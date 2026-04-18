# NewChangeDirectory (NCD) - Agent Documentation

## Project Overview

NewChangeDirectory (NCD) is a cross-platform command-line directory navigation tool inspired by the classic Norton Commander (Norton Utilities) CD command. It maintains a database of directory paths across drives/mounts and provides fast, fuzzy search-based navigation with an interactive TUI.

**Key Features:**
- Fast directory search using a pre-built binary database format
- Interactive TUI with live filtering for selecting from multiple matches (selector mode)
- Filesystem navigator for browsing directories (navigator mode, launched with `ncd .`)
- Cross-platform support: Windows (x64) and Linux (x64, including WSL)
- Multi-threaded directory scanning for performance
- Heuristics-based search result ranking (learns from user choices)
- Per-drive/per-mount database files for incremental updates
- Group/tag system for bookmarking frequently used directories (supports multiple dirs per group)
- Fuzzy matching with Damerau-Levenshtein distance
- Interactive history browser with delete support
- Agent mode API for LLM integration
- Shell tab completion for Bash, Zsh, and PowerShell
- Exclusion list for filtering unwanted directories
- Optional resident service for shared-memory state access
- Directory history for quick navigation between recent locations

## Technology Stack

- **Language:** C11
- **Platforms:** Windows x64, Linux x64 (WSL compatible)
- **Build Tools:**
  - Windows: MSVC (`cl.exe`) via `build.bat`, or MinGW-w64 (`gcc`) via `Makefile`
  - Linux: GCC or Clang via `build.sh`
- **Threading:** Windows threads (Win32 API) / POSIX threads (pthreads)
- **UI:** Platform-native console I/O with ANSI escape sequences (Linux) / Win32 Console API (Windows)
- **IPC:** Named pipes (Windows) / Unix domain sockets (Linux)
- **Shared Memory:** `CreateFileMapping` (Windows) / `shm_open` (Linux)

## Project Structure

```
NewChangeDirectory/
├── src/                       # Source code
│   ├── ncd.h                 # Core types, constants, platform detection macros
│   ├── main.c                # Entry point, CLI parsing, orchestration, heuristics
│   ├── database.c/.h         # Database load/save (binary format), groups, config, metadata
│   ├── scanner.c/.h          # Multi-threaded directory enumeration
│   ├── matcher.c/.h          # Search matching algorithm (chain + fuzzy)
│   ├── ui.c/.h               # Interactive terminal UI (selector + navigator + history)
│   ├── platform.c/.h         # NCD-specific platform wrappers (includes ../shared/)
│   ├── strbuilder.h          # Compatibility shim to ../shared/strbuilder.h
│   ├── state_backend.h       # State access abstraction (local or service)
│   ├── state_backend_local.c # Local disk state implementation
│   ├── state_backend_service.c # Service-backed state implementation
│   ├── shared_state.h/.c     # Shared memory snapshot format
│   ├── shm_platform.h        # Shared memory platform abstraction
│   ├── shm_platform_win.c    # Windows shared memory implementation
│   ├── shm_platform_posix.c  # Linux/POSIX shared memory implementation
│   ├── control_ipc.h         # IPC control channel interface
│   ├── control_ipc_win.c     # Windows named pipe IPC
│   ├── control_ipc_posix.c   # Linux Unix socket IPC
│   ├── service_main.c        # Service executable entry point
│   ├── service_state.c/.h    # Service state management
│   ├── service_publish.c/.h  # Snapshot publication
│   ├── ncd.vcxproj           # Visual Studio project file
│   └── ncd.sln               # Visual Studio solution file
├── completions/              # Shell completion scripts
│   ├── ncd.bash              # Bash completion
│   ├── _ncd                  # Zsh completion
│   └── ncd.ps1               # PowerShell completion
├── test/                     # Test suite
│   ├── Test-Service-Windows.bat   # Windows Service tests (isolated)
│   ├── test_service_wsl.sh        # [NEW] WSL Service tests (isolated)
│   ├── Test-NCD-Windows-Standalone.bat # NCD Windows - standalone
│   ├── Test-NCD-Windows-With-Service.bat # NCD Windows - with service
│   ├── test_ncd_wsl_standalone.sh    # [NEW] NCD WSL - standalone
│   ├── test_ncd_wsl_with_service.sh  # [NEW] NCD WSL - with service
│   ├── test_framework.h/.c   # Minimal unit testing framework
│   ├── test_database.c       # Database module unit tests
│   ├── test_matcher.c        # Matcher module unit tests
│   ├── test_db_corruption.c  # Database corruption handling tests
│   ├── test_bugs.c           # Known bug detection tests
│   ├── test_shared_state.c   # Shared state tests
│   ├── test_service_lazy_load.c   # Service lazy loading tests
│   ├── test_service_parity.c      # Service vs standalone parity tests
│   ├── test_service_lifecycle.c   # Service lifecycle (start/stop) tests
│   ├── test_service_integration.c # NCD client service integration tests
│   ├── fuzz_database.c       # Fuzz testing for database loading
│   ├── bench_matcher.c       # Performance benchmarks
│   ├── Makefile              # Test build system
│   ├── TESTING.md            # Testing documentation
│   ├── Win/                  # Windows-specific tests
│   ├── Wsl/                  # WSL/Linux-specific tests
│   └── PowerShell/           # PowerShell-specific tests
├── tasks/                    # Implementation tracking
│   ├── todo.md               # Task checklist
│   ├── lessons.md            # Lessons learned
│   └── baseline.md           # Performance baseline
├── ncd.bat                   # Windows wrapper script (CMD)
├── ncd                       # Linux wrapper script (Bash)
├── ncd_service.bat           # Windows service launcher
├── ncd_service               # Linux service launcher
├── build.bat                 # Windows build (MSVC)
├── build.sh                  # Linux build (GCC/Clang)
├── Makefile                  # Cross-platform build (MinGW)
├── deploy.bat                # Windows deployment script
├── deploy.sh                 # Linux deployment script
└── LINUX_PORT_CHANGELIST.md  # Linux porting documentation
```

**External Dependency:**
The project requires a shared platform abstraction library located at `../shared/` (sibling directory). This library provides cross-platform utilities for filesystem, console I/O, threading, and string building. The build scripts reference files like `../shared/platform.c`, `../shared/platform.h`, `../shared/strbuilder.c`, and `../shared/common.c`.

## Module Organization

| Module | Purpose | Key Files |
|--------|---------|-----------|
| Core Types | Platform detection, shared structures, constants | `ncd.h` |
| Main | CLI parsing, heuristics, result generation, orchestration | `main.c` |
| Database | Binary DB load/save, path helpers, groups, config, metadata, exclusions, history | `database.c/.h` |
| Scanner | Thread pool management, recursive directory scanning | `scanner.c/.h` |
| Matcher | Chain-matching algorithm, name index, fuzzy matching with DL distance | `matcher.c/.h` |
| UI | Interactive selection list, filesystem navigator, history browser, dialogs, config editor | `ui.c/.h` |
| Platform | NCD-specific wrappers around shared platform library | `platform.c/.h` |
| State Backend | Abstraction for local disk vs service-backed state | `state_backend.h`, `state_backend_local.c`, `state_backend_service.c` |
| Shared State | Shared memory snapshot format and validation | `shared_state.h/.c` |
| SHM Platform | Cross-platform shared memory wrapper | `shm_platform.h`, `shm_platform_win.c`, `shm_platform_posix.c` |
| Control IPC | Inter-process communication for service/client | `control_ipc.h`, `control_ipc_win.c`, `control_ipc_posix.c` |
| Service | Resident state service implementation | `service_main.c`, `service_state.c/.h`, `service_publish.c/.h` |
| String Builder | Dynamic string construction, JSON escaping | `../shared/strbuilder.c` + `src/strbuilder.h` |
| Common | Memory allocation wrappers that exit on OOM | `../shared/common.c` |

## Build Commands

### Windows (MSVC)

```powershell
# Automatic Visual Studio detection and build
# (Use cmd /c because the Shell tool runs PowerShell by default)
cmd /c build.bat

# Or manually from Visual Studio x64 Native Tools Command Prompt:
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /Isrc /I../shared ^
   src\main.c src\database.c src\scanner.c src\matcher.c src\ui.c ^
   src\platform.c ../shared/platform.c ../shared/strbuilder.c ../shared/common.c ^
   /Fe:NewChangeDirectory.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib
```

### Windows (MinGW-w64)

```batch
make                    # Release build
make debug             # Debug build with symbols
clean                  # Remove build artifacts
make install           # Install to INSTALL_DIR (default: C:/Windows/System32)
```

### Linux (GCC)

```bash
chmod +x build.sh
./build.sh             # Default build with gcc

CC=clang ./build.sh    # Build with Clang

# Deploy to /usr/local/bin:
./deploy.sh            # May require sudo
```

### Visual Studio

Open `src/ncd.sln` in Visual Studio 2019/2022 and build from IDE.

## Testing

> **📋 For AI Agents:** See [`AGENT_TESTING_GUIDE.md`](AGENT_TESTING_GUIDE.md) for comprehensive agent-specific testing instructions.

### Quick Start (Recommended Test Runners)

NCD provides **three safe test runners** that all use PowerShell internally for guaranteed cleanup. Even if you press Ctrl+C during tests, your environment will be restored.

**Why PowerShell?** Pure batch files cannot trap Ctrl+C interrupts, which can leave your environment corrupted. These runners delegate to PowerShell which uses `try/finally` blocks that CAN trap Ctrl+C.

> **⚠️ AI Agent Note - Running `.bat` files via the Shell tool:** The Shell tool runs PowerShell by default. PowerShell does **not** execute `.bat` files by bare name (e.g., `Build-And-Run-All-Tests.bat` fails with "not recognized as a cmdlet"). Always invoke batch files using one of these forms:
> ```powershell
> # Option A: Explicitly run via cmd.exe (recommended)
> cmd /c Build-And-Run-All-Tests.bat
>
> # Option B: Use .\ prefix so PowerShell resolves the path
> .\Build-And-Run-All-Tests.bat
> ```
>
> **⚠️ AI Agent Note - Timeout Limits:** Foreground shell commands have a 300-second timeout limit. The full test suite takes approximately 3-4 minutes. Use `run_in_background=true` with a longer timeout (e.g., 600s) when invoking the full suite, or run `Run-All-Tests.bat` if binaries are already built (faster).

#### Option 1: Build and Run All Tests (Recommended for CI/Full Validation)

```powershell
# Full build + test cycle - builds everything then runs all tests
# (Use cmd /c because the Shell tool runs PowerShell by default)
cmd /c Build-And-Run-All-Tests.bat

# Options
cmd /c Build-And-Run-All-Tests.bat --windows-only    # Windows only
cmd /c Build-And-Run-All-Tests.bat --quick           # Skip fuzz/benchmarks
cmd /c Build-And-Run-All-Tests.bat --no-service      # Skip service tests
```

#### Option 2: Run All Tests (Pre-built Binaries)

```powershell
# Run all tests WITHOUT building (binaries must already exist)
cmd /c Run-All-Tests.bat

# Options
cmd /c Run-All-Tests.bat --windows-only    # Windows only
cmd /c Run-All-Tests.bat --quick           # Skip fuzz/benchmarks
cmd /c Run-All-Tests.bat --skip-unit-tests # Skip unit tests
```

#### Option 3: Run Specific Test Suites (Development)

```powershell
# Run specific test suites
cmd /c Run-Tests-Safe.bat unit              # Unit tests only
cmd /c Run-Tests-Safe.bat integration       # Integration tests (6 suites)
cmd /c Run-Tests-Safe.bat service           # Service tests only
cmd /c Run-Tests-Safe.bat ncd               # NCD standalone tests
cmd /c Run-Tests-Safe.bat windows           # All Windows tests
cmd /c Run-Tests-Safe.bat wsl               # All WSL tests

# Environment management
cmd /c Run-Tests-Safe.bat --check           # Check if environment is clean
cmd /c Run-Tests-Safe.bat --repair          # Repair corrupted environment
```

**All three runners are Ctrl+C safe and restore your environment.**

### Complete Build and Test Process

This section describes the complete process to build NCD and run all tests on all platforms.

#### Step 1: Build NCD

**Windows (MSVC):**
```powershell
# From project root - auto-detects Visual Studio
# (Use cmd /c because the Shell tool runs PowerShell by default)
cmd /c build.bat

# Verify binaries exist
dir NewChangeDirectory.exe
dir NCDService.exe
```

**Windows (MinGW):**
```batch
make
dir NewChangeDirectory.exe
dir NCDService.exe
```

**Linux/WSL:**
```bash
chmod +x build.sh
./build.sh
ls -la NewChangeDirectory
ls -la ncd_service
```

#### Step 2: Build Test Executables

**Windows:**
```powershell
cd test
# (Use cmd /c because the Shell tool runs PowerShell by default)
cmd /c build-tests.bat
```

**Linux/WSL:**
```bash
cd test
make all
```

#### Step 3: Run Tests

**Option A: Run All Tests (Complete Test Suite)**

Runs both unit tests and integration tests across all platforms:

```powershell
# From project root - Run all tests (pre-built binaries) - FASTEST
# Use this when test executables already exist in test\ directory
cmd /c Run-All-Tests.bat

# From project root - Build then run all tests - SLOWER
# Use this only if binaries are missing or you need a fresh build
cmd /c Build-And-Run-All-Tests.bat

# Windows only
cmd /c Run-All-Tests.bat --windows-only
cmd /c Build-And-Run-All-Tests.bat --windows-only

# Quick mode (skip fuzz/benchmarks)
cmd /c Run-All-Tests.bat --quick
cmd /c Build-And-Run-All-Tests.bat --quick
```

**Agent Decision Rule:** If `test\*.exe` files exist, prefer `Run-All-Tests.bat`. Only use `Build-And-Run-All-Tests.bat` if binaries are missing or the user explicitly requests a rebuild.

**Option B: Run Only Unit Tests**

```powershell
# Windows - from test directory
cd test
cmd /c Run-All-Unit-Tests.bat

# Skip fuzz tests (faster)
cmd /c Run-All-Unit-Tests.bat --skip-fuzz

# Linux/WSL
cd test
make test
```

**Option C: Run Only Integration Tests (6 Test Suites)**

```powershell
# All 6 test suites (use the main runners for safety)
cmd /c Run-All-Tests.bat --skip-unit-tests
cmd /c Build-And-Run-All-Tests.bat --skip-unit-tests

# Individual suites (for development only)
cmd /c Test-Service-Windows.bat              # Service isolated (Windows)
cmd /c Test-NCD-Windows-Standalone.bat       # NCD standalone (Windows)
cmd /c Test-NCD-Windows-With-Service.bat     # NCD with service (Windows)

# WSL (from project root)
bash test/Test-Service-WSL.sh
bash test/Test-NCD-WSL-Standalone.sh
bash test/Test-NCD-WSL-With-Service.sh
```

**Note:** The individual test scripts should only be used during development when working on specific test scenarios. For normal testing, always use `Run-All-Tests.bat` or `Build-And-Run-All-Tests.bat`.

### Test Runner Reference

#### Primary Runners (Safe - Use These)

| Script | Purpose | When to Use |
|--------|---------|-------------|
| `Build-And-Run-All-Tests.bat` | **Build + test all** - Full build and test cycle, Ctrl+C safe | **RECOMMENDED** - CI builds, full validation |
| `Run-All-Tests.bat` | **Run all tests** - Run pre-built tests, Ctrl+C safe | **RECOMMENDED** - When binaries already built |
| `Run-Tests-Safe.bat` | **Specific suites** - Run individual test suites, Ctrl+C safe | Development work on specific areas |
| `Run-NcdTests.ps1` | PowerShell native with full control | When you need maximum flexibility |

#### Development Helpers (Use with Caution)

| Script | Purpose | When to Use |
|--------|---------|-------------|
| `test\Run-All-Unit-Tests.bat` | Runs unit tests only | Quick validation during development |
| `test\build-and-run-tests.bat` | Builds and runs unit tests | Unit test development |
| `Check-Environment.ps1` | Environment diagnostics and repair | When tests leave environment corrupted |
| `test\Test-Service-Windows.bat` | Single integration test suite | **Development only** - Working on service tests |
| `test\Test-NCD-Windows-Standalone.bat` | Single integration test suite | **Development only** - Working on NCD tests |
| `test\Test-NCD-Windows-With-Service.bat` | Single integration test suite | **Development only** - Working on integration |

**IMPORTANT:** For normal testing, always use the three primary runners at the project root. The individual test scripts in the `test\` directory should only be used when actively developing or debugging specific test scenarios, as they require proper environment setup that the primary runners handle automatically.

### Test Suite Breakdown by Platform and Category

When the user says "run tests" without specifying which tests, the default is to run all tests. Here is the complete breakdown of the 11 test suites executed by the primary runners:

| # | Suite | Platform | Category | Typical Duration | Checks | Expected Result |
|---|-------|----------|----------|------------------|--------|-----------------|
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

**How to report results — MANDATORY FORMAT:**

When reporting test results to the user, the **ONLY** acceptable format is a **complete per-test listing**. You **must** list every individual test by name with its exact status (`PASSED`, `FAILED`, or `SKIP`). Suite-level summary tables and totals alone are **NOT** acceptable.

**Quick method:** Run `python test/generate_report.py` from the project root.

This script includes its own environment isolation (mirroring the safe batch runners):
- Creates an isolated temp directory for test data
- Redirects `LOCALAPPDATA` and `XDG_DATA_HOME` to that temp dir
- Sets `NCD_TEST_MODE=1`
- Stops any running NCD services before testing
- Restores environment and cleans up on exit (even on Ctrl+C)

It then produces the complete per-test report by running each unit test executable and parsing per-test names/statuses, plus counting individual checks in integration test script sources.

**Full build + test + report (recommended for CI):**
1. Run `cmd /c Build-And-Run-All-Tests.bat` to build and run all tests safely
2. Then run `python test/generate_report.py` for the detailed per-test breakdown

**Manual method (if script is unavailable):**
1. Run each unit test executable directly to capture full per-test output.
2. For every executable, present a table showing each test name and its status.
3. Finish with a module-level summary table (executable, tests run, passed, failed, skipped).
4. For integration suites, list each suite with platform, duration, and PASS/FAIL.
5. Count individual checks in integration scripts via `grep -c 'pass\|fail\|TEST'` on script sources.
6. Report the pass/fail/not ran ratio for each suite.

**Required elements in every report:**
- Per-executable test tables with every test name and status
- Module-level summary table (executable, run, passed, failed, skipped)
- Integration suite table with **check counts** and platform
- Grand total table combining unit + integration numbers

**Example (unit tests):**
```
### test_database.exe (50 tests)
| # | Test | Status |
| 1 | create_and_free | [OK] PASSED |
...
**Ratio: 50/50 passed | 0 failed | 0 skipped**
```

**Example (integration suites):**
```
| # | Suite | Platform | Checks | Passed | Failed | Skipped | Status |
| 1 | Service Tests (Isolated) | Windows | ~10 | ~10 | 0 | 0 | [PASS] |
```

**Do NOT** present only summary lines like `Total Suites: 11 | Passed: 11` without the full per-test breakdown.

**Note:** Some WSL suites may emit bash warnings (e.g., "ignored null byte in input") or show aborted processes in agent command tests. These are typically expected for negative test cases and do **not** indicate failures as long as the harness reports `[PASS]`.

### Test Isolation (CRITICAL)

All tests must be **completely isolated** from the user's real data:

| Resource | User Data | Test Data |
|----------|-----------|-----------|
| **Drives/Mounts** | `C:`, `D:`, etc. | VHD (Windows) or ramdisk (WSL) |
| **Metadata** | `%LOCALAPPDATA%\NCD\ncd.metadata` | Temp directory via `-conf` or env override |
| **Databases** | `%LOCALAPPDATA%\NCD\ncd_*.database` | Temp location via `-d:path` |

**Rules for writing tests:**
1. **Never use bare `-r`** - Always use `-r:.` (subdirectory) or `-r:drive` (specific drive)
2. **Never scan user drives** - Only scan the test VHD/ramdisk/temp directory
3. **Never modify user metadata** - Use `-conf <temp_path>` for custom metadata location
4. **Clean up after tests** - Remove all test files and databases
5. **Use `NCD_TEST_MODE=1`** - Set this environment variable to disable background rescans during testing

**Important:** The `NCD_TEST_MODE` environment variable must be set for BOTH the NCD client AND the service. When starting the service in tests, ensure the environment variable is passed to the service process. The service checks this variable to prevent automatic background rescans that could scan user drives.

### Test Environment Variables

The test runners automatically handle these environment variables:

| Variable | Purpose | Set By |
|----------|---------|--------|
| `NCD_TEST_MODE` | Disables automatic background rescans. In debug builds, also enables headless stdio TUI mode. Optionally set dimensions as `cols,rows` (e.g., `80,25`). | All test runners |
| `LOCALAPPDATA` | Isolates metadata from user data (set to temp dir) | Windows test runners |
| `XDG_DATA_HOME` | Isolates metadata on Linux/WSL (set to temp dir) | WSL test runners |
| `NCD_UI_KEYS` | Comma-separated keys to inject into TUI. Prefix with `@` to load from a file (e.g., `@keys.txt`). | Manual testing/CI |
| `NCD_UI_KEY_TIMEOUT_MS` | Timeout for key injection (milliseconds) | Automated testing |
| `NCD_STRESS_ITERATIONS` | Reduces iteration count for stress tests | Development |

**Environment Isolation Mechanism:**

The primary test runners implement multiple layers of isolation:

1. **PowerShell Wrapper Layer:**
   - Creates isolated temp directory: `%TEMP%\ncd_test_YYYYMMDD_HHMMSS_RAND\`
   - Sets `LOCALAPPDATA` (Windows) and `XDG_DATA_HOME` (WSL) to this temp directory
   - Sets `NCD_TEST_MODE=1` to prevent background rescans

2. **Individual Test Script Layer:**
   - Each test script creates its own sub-directory within the temp location
   - Uses `-conf` option to specify custom metadata paths
   - Uses `-d:` option to specify database locations
   - Only scans test VHDs/ramdisks, never user drives

3. **Cleanup on ALL Exit Paths:**
   - Services stopped via `Stop-NcdProcesses`
   - Temp directories removed via `Remove-TestTempDirs`
   - TUI variables cleared (`NCD_UI*`, `NCD_TUI*`) via `Clear-TuiEnvironmentVariables`
   - Environment restored via `Restore-EnvironmentState`

**Important:** All `NCD_UI*` and `NCD_TUI*` environment variables are automatically cleared after tests complete. This ensures that headless mode, key injection, or TUI display settings from tests do not affect subsequent NCD usage in your shell.

| Runner | Save Method | Restore Method | Ctrl+C Safe | Isolated Temp Dir |
|--------|-------------|----------------|-------------|-------------------|
| `Build-And-Run-All-Tests.bat` | PowerShell hashtable | `finally` block | ✅ Yes | ✅ Yes |
| `Run-All-Tests.bat` | PowerShell hashtable | `finally` block | ✅ Yes | ✅ Yes |
| `Run-Tests-Safe.bat` | PowerShell hashtable | `finally` block | ✅ Yes | ✅ Yes |
| `Run-NcdTests.ps1` | PowerShell hashtable | `finally` block | ✅ Yes | ✅ Yes |
| Individual `test\*.bat` files | Various | Various | ❌ No | ⚠️ Partial |

**If your environment gets corrupted**, run:
```powershell
cmd /c Run-Tests-Safe.bat --repair
```

### Running Unit Tests

**Linux/WSL:**
```bash
cd test
make test
```

**Windows (MSVC):**
```batch
cd test
cl /nologo /W3 /O2 /Isrc /I. /I../../shared test_database.c test_framework.c ../src/database.c ../src/platform.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c /Fe:test_database.exe
cl /nologo /W3 /O2 /Isrc /I. /I../../shared test_matcher.c test_framework.c ../src/matcher.c ../src/database.c ../src/platform.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c /Fe:test_matcher.exe
test_database.exe
test_matcher.exe
```

**Windows (MinGW):**
```batch
cd test
gcc -Wall -Wextra -I../src -I../../shared -I. -o test_database.exe test_database.c test_framework.c ../src/database.c ../src/platform.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c -lpthread
gcc -Wall -Wextra -I../src -I../../shared -I. -o test_matcher.exe test_matcher.c test_framework.c ../src/matcher.c ../src/database.c ../src/platform.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c -lpthread
test_database.exe
test_matcher.exe
```

### Running Other Tests

```bash
# Fuzz tests (runs for 60 seconds max)
cd test && make fuzz

# Performance benchmarks
cd test && make bench

# Database corruption tests
cd test && make corruption

# Bug detection tests
cd test && make bugs

# Recursive mount tests (Linux, requires root)
cd test && make recursive-mount

# Comprehensive feature tests (Linux, requires root)
cd test && make features

# Feature tests without root (uses /tmp)
cd test && make features-noroot

# Service parity tests
cd test && make service-parity
```

### Test Coverage

The NCD test suite contains **880+ unit tests** across multiple test files following a parallel expansion effort. See `test/README.md` and `test/COVERAGE_REPORT.md` for comprehensive documentation.

**Coverage by Category:**

| Category | Tests | Coverage % | Notes |
|----------|-------|------------|-------|
| Database operations | 110 | 95% | Excellent (extended tests added) |
| Search/matching | 53 | 97% | Excellent (extended tests added) |
| CLI parsing | 91 | 98% | Excellent (edge cases covered) |
| Agent mode args | 47 | 75% | Good (parsing + integration) |
| Platform abstraction | 61 | 90% | Excellent (extended tests added) |
| Shared library | 61 | 95% | Excellent |
| Service lifecycle | 96 | 95% | Excellent (stress tests added) |
| UI/Interaction | 103 | 92% | Excellent (extended tests added) |
| Scanner/Input | 44 | 92% | Excellent (extended tests added) |
| Stress/Fuzz | 42 | N/A | Comprehensive robustness testing |
| Integration/feature | 119+ | 85% | Good (exit codes + behavior) |
| **Overall** | **880+** | **~95%** | **Excellent coverage** |

**Parallel Expansion (430 new tests):**
Following the `PARALLEL_TEST_COVERAGE_PLAN.md`, 4 agents worked in parallel to add comprehensive test coverage:
- **Agent 1 (Data Core):** 90 tests - Database extended, odd cases, fuzzing
- **Agent 2 (Service IPC):** 100 tests - Service stress, IPC extended, SHM stress
- **Agent 3 (UI & Main):** 100 tests - UI extended, exclusions, main flow
- **Agent 4 (Input Processing):** 140 tests - CLI edge cases, scanner/matcher extended, stress

See `test/PARALLEL_INTEGRATION_REPORT.md` for detailed breakdown.

**Database Tests (test_database.c - 34 tests):**
- **Basic:** `create_and_free`, `add_drive`, `add_directory`, `full_path_reconstruction`
- **Save/Load:** `binary_save_load_roundtrip`, `json_save_load_roundtrip`, `load_auto_detects_*`
- **Corruption:** `binary_load_corrupted_rejected`, `binary_load_truncated_rejected`
- **Version:** `check_file_version_*`, `check_all_versions_with_no_databases`
- **Config:** `config_init_defaults_*`, `config_save_load_roundtrip`, `config_encoding_*`
- **Exclusions:** `exclusion_add_*`, `exclusion_remove_*`, `exclusion_check_*`
- **Heuristics:** `heur_calculate_score_*`, `heur_find_*`, `heur_find_best_*`
- **Drive:** `drive_backup_*`, `find_drive_*`
- **Text Encoding:** `text_encoding_*`, `binary_save_load_utf8_*`, `binary_save_load_utf16_*`
- **Version Consistency:** `saved_database_version_matches_current`, `saved_database_version_is_correct_binary_version`
- **Exclusion Filtering:** `filter_excluded_*`

**Matcher Tests (test_matcher.c - 23 tests):**
- **Basic:** `match_single_component`, `match_two_components`, `match_case_insensitive`, `match_with_hidden_filter`, `match_no_results`, `match_empty_search`
- **Prefix:** `match_prefix_single_component`, `match_prefix_multi_component`
- **Glob:** `match_glob_star_*`, `match_glob_question_*`, `match_glob_in_path`, `match_glob_no_match`
- **Fuzzy:** `fuzzy_match_with_typo`, `fuzzy_match_with_transposition`, `fuzzy_match_no_results_on_total_mismatch`, `fuzzy_match_case_difference`
- **Name Index:** `name_index_build_*`, `name_index_find_by_hash_*`, `name_index_free_*`

**Scanner Tests (test_scanner.c - 9 tests):**
- `scan_mount_populates_database`, `scan_mount_respects_hidden_flag`, `scan_mount_applies_exclusions`
- `scan_mount_excludes_directories_from_database`, `scan_subdirectory_merges_into_existing_db`
- `find_is_directory_*`, `find_is_hidden_*`, `find_is_reparse_*`, `scan_mounts_handles_empty_mount_list`

**Metadata Tests (test_metadata.c - 11 tests):**
- `metadata_create_*`, `metadata_save_load_roundtrip`, `metadata_exists_*`
- `metadata_preserves_config_section`, `metadata_preserves_groups_section`
- `metadata_preserves_exclusions_section`, `metadata_preserves_heuristics_section`, `metadata_preserves_history_section`
- `metadata_cleanup_legacy_*`, `metadata_free_*`, `metadata_group_remove_path`

**Platform Tests (test_platform.c - 12 tests):**
- `strcasestr_*` (3 tests), `is_drive_specifier_*` (2 tests)
- `parse_drive_from_search_*` (2 tests), `build_mount_path_*`
- `filter_available_drives_*`, `is_pseudo_fs_*`, `get_app_title_*`, `db_default_path_*`

**Additional Test Files:**
- **test_strbuilder.c** (15 tests) - String builder and JSON escaping
- **test_strbuilder_extended.c** (39 tests) - Extended string builder tests
- **test_common.c** (7 tests) - Memory allocation and overflow checking
- **test_common_extended.c** (37 tests) - Extended memory allocation tests
- **test_history.c** (14 tests) - Directory history operations
- **test_db_corruption.c** (16+ tests) - Database corruption handling
- **test_bugs.c** (20 tests) - Known bug detection
- **test_cli_parse_extended.c** (31 tests) - Extended CLI parsing tests
- **test_platform_extended.c** (34 tests) - Extended platform abstraction tests
- **test_integration_extended.c** (19 tests) - Extended integration tests

**Shared State Tests:**
- `header_validation` - Snapshot header validation
- `checksum_validation` - CRC64 checksum verification
- `section_lookup` - Section table navigation
- `bounds_checking` - Memory bounds validation

### Service Tests

The service test suite validates the NCD State Service functionality across Windows and Linux:

**Service Lifecycle Tests (`test_service_lifecycle.c` - 13 tests):**
- **Lifecycle:** `service_status_when_stopped`, `service_start_when_stopped`, `service_double_start_fails`, `service_stop_when_running`, `service_stop_when_already_stopped`, `service_restart`
- **IPC:** `service_ipc_ping`, `service_ipc_ping_when_stopped`, `service_ipc_get_version`, `service_ipc_get_state_info`, `service_ipc_shutdown_request`
- **State:** `service_state_progression` - Verify STARTING → LOADING → READY progression
- **Termination:** `service_termination_graceful_then_force`

**Service Integration Tests (`test_service_integration.c` - 12 tests):**
- **Help Output:** `help_shows_standalone_when_service_stopped`, `help_shows_service_running_when_service_active`, `help_includes_exclusion_and_agent_options`
- **Agent Status:** `agent_service_status_not_running`, `agent_service_status_json_not_running`, `agent_service_status_running`, `agent_service_status_json_running`, `agent_service_status_after_stop`
- **Operation:** `ncd_search_works_without_service`, `ncd_search_works_with_service`, `a_flag_is_not_agent_alias`

**Running Service Tests:**

```bash
# Linux/WSL - All service tests
make test_service_lifecycle
make test_service_integration
./test_service_lifecycle
./test_service_integration

# Or use the combined target
make service-test

# Windows - Service tests are included in build-and-run-tests.bat
cd test
cmd /c build-and-run-tests.bat
```

**Note:** Service tests require the service executable to be built:
- Windows: `NCDService.exe` 
- Linux: `ncd_service`

Tests will skip gracefully if the service executable is not available.

**Test Infrastructure Note (Windows):**
NCD automatically detects when stdout is redirected (e.g., in tests or when piping output) and switches from direct console writes (`CONOUT$`) to standard stdout output. This allows:
- Help text output (`ncd -?`) to be captured via pipe redirection for testing
- Proper operation in both interactive (console) and batch (redirected) modes

The detection uses `GetConsoleMode()` on Windows and `isatty()` on Linux. When redirected, output goes through `fputs(stdout)`; when in a console, output uses `WriteConsoleA()` for better TUI performance.

### Keystroke Injection for TUI Testing

NCD supports automated keystroke injection for testing and scripting the interactive TUI (config editor, selector, navigator, etc.). This allows batch files and scripts to programmatically drive the UI without human interaction.

**Environment Variables:**

| Variable | Purpose |
|----------|---------|
| `NCD_UI_KEYS` | Comma-separated list of keys to inject (e.g., `DOWN,ENTER,TEXT:hello`). Prefix with `@` to load from a file (e.g., `@keys.txt`). |
| `NCD_UI_KEY_TIMEOUT_MS` | Timeout in milliseconds to wait for next key (default: waits indefinitely) |

**Key Tokens:**

| Token | Description |
|-------|-------------|
| `UP`, `DOWN`, `LEFT`, `RIGHT` | Arrow keys |
| `PGUP`, `PGDN` | Page Up/Down |
| `HOME`, `END` | Home/End keys |
| `ENTER` | Enter/Return key |
| `ESC` | Escape key |
| `TAB` | Tab key |
| `BACKSPACE` | Backspace key |
| `DELETE` | Delete key |
| `SPACE` | Space bar |
| `TEXT:abc` | Type literal text (e.g., `TEXT:-1` types "-1") |

**Example - Disable Auto-Rescan via Config Editor:**

```batch
:: Navigate to rescan_interval field (6th item), enter -1, save
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
ncd -c
set "NCD_UI_KEYS="
```

**Key Sequence Explanation:**
1. `DOWN,DOWN,DOWN,DOWN,DOWN` - Navigate to "Auto-rescan hours" (6th config item)
2. `ENTER` - Enter input mode for the numeric field
3. `TEXT:-1` - Type "-1" (never auto-rescan)
4. `ENTER` - Confirm the value
5. `ENTER` - Save configuration and exit

**Headless TUI Testing Mode:**

For automated testing without a real console, enable the stdio backend:

```batch
:: Enable headless mode with custom dimensions, inject keystrokes
set "NCD_TEST_MODE=80,25"
set "NCD_UI_KEYS=DOWN,ENTER,TEXT:mydir,ENTER"

:: Run NCD - output goes to stdout instead of console
ncd -c > tui_output.txt 2>&1

:: Clean up
set "NCD_TEST_MODE="
set "NCD_UI_KEYS="
```

In headless mode (`NCD_TEST_MODE` is set in a debug build):
- TUI output is written to stdout as text instead of console control codes
- Keystrokes are injected from `NCD_UI_KEYS` (use `@file` to load from a file)
- Display size defaults to 80x25, or parsed from `NCD_TEST_MODE` as `cols,rows`
- Useful for CI/CD pipelines and automated testing

**Notes:**
- Key names are case-insensitive (`enter`, `ENTER`, `Enter` all work)
- Keys are consumed from the queue in order
- Once the queue is empty, the TUI returns ESC (deterministic failure) when `NCD_TEST_MODE` is set
- Use `NCD_UI_KEY_TIMEOUT_MS=5000` to set a 5-second timeout waiting for keys

## Code Style Guidelines

### Naming Conventions

- **Functions:** `module_verb_noun()` (e.g., `db_load_binary()`, `platform_get_env()`)
- **Types:** PascalCase with suffix (e.g., `NcdDatabase`, `DriveData`, `DirEntry`)
- **Constants:** `NCD_UPPER_CASE` (e.g., `NCD_MAX_PATH`, `NCD_BIN_MAGIC`)
- **Macros:** `NCD_PREFIX` for public, `static` for file-local

### Platform Abstraction

Platform-specific code is handled through the shared library at `../shared/`. NCD-specific platform code lives in `platform.c` with these patterns:

```c
#if NCD_PLATFORM_WINDOWS
    // Win32 API code
#elif NCD_PLATFORM_LINUX
    // POSIX/Linux code
#else
#error "Unsupported platform"
#endif
```

Platform macros defined in `ncd.h` via `../shared/platform_detect.h`:
- `NCD_PLATFORM_WINDOWS` / `NCD_PLATFORM_LINUX`
- `NCD_ARCH_X64`

### Memory Management

- Use `ncd_malloc()` / `ncd_realloc()` wrappers that exit on OOM
- Use `ncd_malloc_array()` for array allocations with overflow checking
- Use `ncd_calloc()` for zero-initialized allocations
- Database uses string pools to reduce memory fragmentation
- Binary loads use single blob allocation with pointer referencing

### String Safety

- All string operations should use bounded copies
- Path manipulation uses `path_join()`, `path_parent()`, `path_leaf()` utilities
- Output escaping is handled in `write_result()` for batch/shell safety

## Key Data Structures

### DirEntry (12 bytes)
Compact directory node with string pool indexing:
```c
typedef struct {
    int32_t  parent;       // Parent index (-1 for root)
    uint32_t name_off;     // Offset into name_pool
    uint8_t  is_hidden;
    uint8_t  is_system;
    uint8_t  pad[2];       // Explicit padding for 4-byte alignment
} DirEntry;
```

### NcdDatabase
In-memory database with blob support for fast binary loads:
```c
typedef struct {
    int        version;
    bool       default_show_hidden;
    bool       default_show_system;
    time_t     last_scan;
    DriveData *drives;
    int        drive_count;
    void      *blob_buf;       // NULL unless is_blob
    bool       is_blob;        // true => dirs/pools point into blob
    void      *name_index;     // Cached name index for fast searches
} NcdDatabase;
```

### NcdMetadata (Consolidated)
Single file containing config, groups, heuristics, exclusions, and directory history:
```c
typedef struct {
    NcdConfig cfg;
    NcdGroupDb groups;
    NcdHeuristicsV2 heuristics;
    NcdExclusionList exclusions;
    NcdDirHistory dir_history;
    char file_path[NCD_MAX_PATH];
    bool config_dirty;
    bool groups_dirty;
    bool heuristics_dirty;
    bool exclusions_dirty;
    bool dir_history_dirty;
} NcdMetadata;
```

### Troubleshooting Test Issues

#### "LOCALAPPDATA points to test temp directory"

**Symptoms:**
- NCD reports "No database found"
- `ncd --agent:check --db-age` shows wrong location
- Environment check reports corruption

**Cause:** Previous test was interrupted with Ctrl+C (batch file limitation)

**Fix:**
```powershell
# Quick repair
cmd /c Run-Tests-Safe.bat --repair

# Or manually
$env:LOCALAPPDATA="$env:USERPROFILE\AppData\Local"
$env:NCD_TEST_MODE=""
```

#### "Tests interrupted and environment not cleaned up"

**Cause:** Batch files cannot trap Ctrl+C

**Prevention:**
```powershell
# Use PowerShell runner (Ctrl+C safe!)
cmd /c Run-Tests-Safe.bat

# Or use isolated mode (runs in subprocess)
cmd /c test\Run-Isolated.bat test\Test-Service-Windows.bat
```

**Repair:**
```powershell
# Comprehensive repair
.\Check-Environment.ps1 -Repair

# Or use the batch wrapper
cmd /c Run-Tests-Safe.bat --repair
```

#### "Cannot run PowerShell scripts"

**Error:** "Execution of scripts is disabled on this system"

**Fix:**
```powershell
# Run as Administrator
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser

# Or use the batch wrapper (bypasses policy)
cmd /c Run-Tests-Safe.bat
```

#### "Service tests timeout or hang"

**Cause:** Previous service instance still running

**Fix:**
```batch
:: Kill all NCD processes
taskkill /F /IM NCDService.exe
taskkill /F /IM NewChangeDirectory.exe

:: Or use PowerShell module
powershell -Command "Import-Module .\test\PowerShell\NcdTestUtils.psm1; Stop-NcdService"
```

## Usage

### Command Line

```bash
# Search for a directory
ncd downloads
ncd scott\downloads

# Force rescan
ncd -r                    # All drives
ncd -r:bde                 # Specific drives only (B:,D:,E:)
cd -r:-b-d                 # Exclude drives B: and D:
cd -r:.                    # Rescan current subdirectory only

# Options
ncd -i scott\appdata      # Include hidden directories
ncd -s                    # Include system directories
ncd -a                    # Include all (hidden + system)
ncd -z                    # Fuzzy matching with DL distance

# Interactive navigation
ncd .                     # Navigate from current directory
ncd C:                    # Navigate from drive root (Windows)

# Groups (bookmarks) - supports multiple directories per group
ncd -g:@home              # Add current directory to group
ncd -G:@home             # Remove current directory from group
ncd -g:l                   # List all groups
ncd @home                 # Jump to group (shows selector if multiple)

# Directory history
ncd -0                    # Ping-pong between two most recent directories
ncd -h                    # Browse history interactively (Del to remove)
ncd -h:l                   # List directory history
ncd -h:c                   # Clear all history
ncd -h:c3                  # Remove history entry #3

# Exclusions
ncd -x:<pattern>          # Add exclusion pattern (e.g., -x:C:Windows)
ncd -X:<pattern>         # Remove exclusion pattern
ncd -x:l                   # List all exclusions

(Note: `-x`, `-X`, `-x:l` are also accepted)

# Configuration
ncd -c                    # Edit configuration (set default options)

# Frequent searches
ncd -f                    # Show frequent searches
ncd -f:c                   # Clear frequent searches

# Agent mode (LLM integration)
ncd --agent:query <search> [--json] [--limit N] [--depth]
ncd --agent:ls <path> [--json] [--depth N] [--dirs-only|--files-only]
ncd --agent:tree <path> [--json] [--depth N] [--flat]  # Show directory tree from DB
ncd --agent:check <path> | --db-age | --stats | --service-status
ncd --agent:complete <partial> [--limit N]  # Shell tab-completion
ncd --agent:mkdir <path>  # Create single directory
ncd --agent:mkdirs [--file <path>] [--json] <content>  # Create directory tree

# Service management
ncd_service start         # Start resident service (faster startup)
ncd_service stop          # Stop resident service (waits up to 5s, returns 0 on success, 1 on error)
ncd_service stop block N  # Stop service and wait N seconds (-1 if timeout)
ncd --agent:check --service-status  # Check if service is running

# Service Logging (for debugging crashes)
# Log file location: %LOCALAPPDATA%\NCD\ncd_service.log (Windows)
#                    ~/.local/share/ncd/ncd_service.log (Linux)
ncd_service start -log0   # Log service start, rescan requests, client requests
ncd_service start -log1   # Level 0 + responses sent to clients  
ncd_service start -log2   # Level 1 + detailed startup/shutdown (crash diagnosis)
ncd_service start -log3   # Reserved for future debugging
ncd_service start -log4   # Reserved for future debugging
ncd_service start -log5   # Reserved for future debugging

# Configuration Override
ncd -conf <path>          # Use custom metadata file path
ncd_service start -conf <path>  # Service with custom metadata path

# Help
ncd -?                    # or -h
ncd -v                    # Version info
```

### Interactive Selection UI

When multiple directories match, NCD shows an interactive list with **live filtering**:

**Navigation:**
- **Arrow keys** - Navigate up/down
- **Page Up/Down** - Scroll a page at a time
- **Home/End** - Jump to first/last item
- **Enter** - Select highlighted entry
- **Escape** - Cancel (or clear filter if active)
- **Tab** - Enter navigator mode on selected item

**Live Filtering:**
- **Type characters** - Filter the list in real-time (e.g., type "src" to narrow)
- **Backspace** - Remove last filter character
- **Escape (when filtering)** - Clear filter, restore full list

Filter matching:
- Case-insensitive substring match
- If filter contains `\` or `/`, matches against full path
- Otherwise, matches against last path component (directory name)

### History Browser

The interactive history browser (`ncd -h`) shows recently visited directories:

- **Arrow keys** - Navigate
- **Delete** - Remove entry from history
- **Enter** - Navigate to selected directory
- **Escape** - Cancel

### Wrapper Scripts

**Windows (`ncd.bat`):**
- Must be used to call `NewChangeDirectory.exe`
- Exe writes `%TEMP%\ncd_result.bat` with environment variables
- Batch file sources the result and executes `cd /d`

**Linux (`ncd`):**
- Must be sourced, not executed: `source ncd` or use shell function
- Exe writes `/tmp/ncd_result.sh` (or `$XDG_RUNTIME_DIR`)
- Script sources and executes `cd`

## Shell Tab Completion

NCD supports tab-completion for directory and group names.

### Bash
```bash
source completions/ncd.bash
ncd dow<Tab>    # Completes to "downloads", "documents", etc.
ncd @h<Tab>     # Completes to "@home", "@htdocs", etc.
```

### Zsh
```zsh
fpath+=(/path/to/completions)
autoload -U compinit && compinit
```

### PowerShell
```powershell
. /path/to/completions/ncd.ps1
```

## Database Storage

### Locations

**Windows:**
- Per-drive databases: `%LOCALAPPDATA%\NCD\ncd_X.database`
- Metadata: `%LOCALAPPDATA%\NCD\ncd.metadata`
- Service log: `%LOCALAPPDATA%\NCD\ncd_service.log`
- Legacy (migrated to metadata): `%LOCALAPPDATA%\NCD\ncd.config`, `ncd.groups`, `ncd.history`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- Metadata: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.metadata`
- Service log: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_service.log`

### Binary Format

Magic: `NCDB` (0x4244434E)
Version: 2 (with CRC64 checksum)
Layout:
```
[BinFileHdr: 32 bytes]
[BinDriveHdr x drive_count: 80 bytes each]
[DirEntry[] + name_pool per drive]
[CRC64 checksum]
```

### Metadata Format

Magic: `NCMD` (0x444D434E)
Version: 1
Sections: Config (0x01), Groups (0x02), Heuristics (0x03), Exclusions (0x04), DirHistory (0x05)

### Atomic Writes

All database writes use temp-file-then-rename pattern:
1. Write to `.tmp` file
2. Move current to `.old` (backup)
3. Move `.tmp` to final name
4. On failure: restore from `.old`

### Configuration Override (`-conf` option)

The `-conf <path>` option allows specifying a custom metadata file location instead of using the default platform-specific paths. This is useful for:

- **Testing:** Use isolated configuration during test runs
- **Multiple profiles:** Maintain separate configurations for different workflows
- **Portable installations:** Keep configuration alongside the executable
- **Shared environments:** Use a common configuration across user accounts

**Usage:**
```bash
# Use custom metadata file
ncd -conf C:\MyConfig\ncd.metadata downloads

# Service with custom configuration
ncd_service start -conf /opt/ncd/config/ncd.metadata
```

**Behavior:**
- When `-conf` is specified, all metadata operations (config, groups, exclusions, heuristics, history) use the specified file
- Per-drive database files are still loaded from their default locations or the location specified by `-d:path`
- The override applies to both NCD client and NCD Service when specified
- If the file doesn't exist, it will be created with default settings

**Platform Notes:**
- Windows: Use full paths with drive letters (e.g., `C:\path\to\ncd.metadata`)
- Linux: Use absolute paths (e.g., `/home/user/.config/ncd/ncd.metadata`)

## Architecture Decisions

### Why a wrapper script?

A child process cannot change the working directory of its parent shell (OS limitation). The exe writes a result file; the wrapper sources it and calls `cd`.

### Why binary database format?

- JSON parsing is slow for 100K+ directories
- Binary format allows memory-mapping (single allocation)
- DirEntry shrunk from 268 bytes to 12 bytes using string pools

### Why per-drive databases?

- Allows incremental updates without rescanning everything
- Enables fast fallback search across all drives
- Supports removable media that may be absent

### Why consolidated metadata?

- Single file for config, groups, heuristics, exclusions, and directory history
- Atomic updates (all or nothing)
- Reduces file I/O and simplifies backup

### Auto-Rescan Configuration

The auto-rescan interval is now configurable. Use `ncd -c` to edit the configuration and set:

- **Auto-rescan hours**: Number of hours (1-168) before automatically triggering a background rescan
- **Never** (-1): Disable auto-rescan entirely

The default is -1 (never auto-rescan). When set to a positive value and the database is older than the configured interval, NCD will automatically spawn a background rescan after a successful navigation.

**⚠️ WARNING:** The background rescan uses `ncd -r` which scans **ALL drives/mounts**. If you don't want this behavior, set `rescan_interval_hours=-1` or be aware that using `ncd` manually (outside of test mode) may trigger full system scans.

### Why two UI modes?

1. **Selector:** List of search results with live filtering (arrow keys to select, type to filter)
2. **Navigator:** Browse filesystem hierarchy (Tab to enter, Backspace to exit)

This mirrors Norton Commander's behavior for both search-driven and browse-driven workflows.

### Why an optional resident service?

- Keeps metadata and database hot in shared memory
- Eliminates disk I/O on client startup when service is running
- Client falls back to standalone mode if service unavailable
- Service handles persistence and snapshot publication

### Service Shutdown Behavior

When stopping the service, the following occurs:

1. **Graceful shutdown is always attempted first**: The `stop` command sends an IPC `REQUEST_SHUTDOWN` message to the service, which sets the shutdown flag. The service then exits its main loop and performs cleanup.

2. **Automatic flush on exit**: During cleanup, the service checks if there are any dirty (unsaved) changes:
   - Metadata changes (config, groups, heuristics, exclusions, directory history)
   - Database changes (from rescans)
   - All dirty data is flushed to disk before the process exits

3. **Blocking with timeout**: The `stop` command blocks and waits for the service to stop:
   - Default wait: 5 seconds
   - `stop block N`: Wait N seconds (1-300)
   - Returns `0` if stopped successfully
   - Returns `-1` if timeout exceeded (service may still be stopping)
   - Returns `1` if service not running or IPC error

4. **Force termination only as last resort**: Tests and scripts should always try graceful stop first. Force kill (e.g., `TerminateProcess` on Windows, `kill -9` on Linux) should only be used if graceful shutdown times out.

**Example - Graceful shutdown with custom timeout:**
```bash
# Stop with default 5-second timeout
ncd_service stop

# Stop with 10-second timeout (good for systems with large databases)
ncd_service stop block 10

# Check exit code to determine if force kill is needed
if [ $? -eq -1 ]; then
    echo "Service did not stop in time, may need force kill"
fi
```

### Service Version Compatibility

NCD maintains version compatibility between the client (`NewChangeDirectory.exe`/`ncd`) and the service (`NCDService.exe`/`ncd_service`). The IPC protocol includes version negotiation to ensure compatibility.

**Version Check Behavior:**
- When a client starts and detects a running service, it performs a version check via IPC
- If the service version is older than the minimum required by the client:
  1. The client attempts to gracefully stop the old service
  2. The client informs the user that a new service version is needed
  3. The user must manually start the new service version
- If version check fails (IPC error), the client falls back to standalone mode

**Compatibility Matrix:**

| Client Version | Min Service Version | Compatible Service Versions |
|----------------|---------------------|----------------------------|
| 1.3.x | 1.3.0 | 1.3.0 - 1.3.x |
| 1.2.x | 1.2.0 | 1.2.0 - 1.3.x (forward compatible) |

**Upgrade/Downgrade Procedures:**

**Upgrading (Client + Service):**
```bash
# 1. Stop the old service
ncd_service stop

# 2. Replace both binaries (ncd and ncd_service)
#    - Windows: Copy NewChangeDirectory.exe and NCDService.exe
#    - Linux: Copy ncd and ncd_service

# 3. Start the new service
ncd_service start

# 4. Verify version
ncd -v
```

**Downgrading (if needed):**
```bash
# 1. Stop the current service
ncd_service stop

# 2. Restore previous version binaries

# 3. Start the previous service version
ncd_service start
```

**Force Override:**
Use `--force` flag to bypass version check (not recommended):
```bash
ncd --force <search_term>
```

### Service Logging System

The NCD Service includes a comprehensive logging system for debugging crashes and monitoring service behavior. The logging system is **thread-safe** and writes to a log file with proper locking.

**Log File Location:**
- **Windows:** `%LOCALAPPDATA%\NCD\ncd_service.log` (e.g., `C:\Users\<username>\AppData\Local\NCD\ncd_service.log`)
- **Linux:** `~/.local/share/ncd/ncd_service.log` (or `$XDG_DATA_HOME/ncd/ncd_service.log`)

The log file is created in the same directory as the database files and is opened in append mode.

**Log Levels:**

| Level | Flag | Description |
|-------|------|-------------|
| -1 | (none) | Logging disabled (default) |
| 0 | `-log0` | Service start/stop, rescan requests, all client requests |
| 1 | `-log1` | Level 0 + all responses sent to clients |
| 2 | `-log2` | Level 1 + detailed startup/shutdown steps, intermediate operations |
| 3-5 | `-log3` to `-log5` | Reserved for future debugging use |

**Usage Examples:**

```bash
# Start service with basic logging (high-level events only)
ncd_service start -log0

# Start service with detailed logging (recommended for crash diagnosis)
ncd_service start -log2

# Combine with other options
ncd_service start -log2 -conf C:\NCD\custom.metadata
```

**Log Format:**
```
[HH:MM:SS] [thread_id] [L<level>] message
```

Example log output:
```
[14:32:15] [1234] [L0] === Service Starting at 2026-04-07 14:32:15 ===
[14:32:15] [1234] [L0] Version: 1.3 (Build: Apr 7 2026 14:30:00)
[14:32:15] [1234] [L0] Log Level: 2 (+ detailed operations)
[14:32:15] [1234] [L2] Initializing service state...
[14:32:15] [1234] [L2] Service state initialized successfully
[14:32:15] [1234] [L0] Metadata snapshot published
[14:32:15] [1234] [L2] Starting background loader thread...
[14:32:16] [5678] [L2] Background loader thread started
[14:32:16] [5678] [L2] About to load databases...
[14:32:18] [5678] [L0] Service state changed to READY
```

**For Crash Diagnosis:**
- Use `-log2` to capture detailed startup/shutdown sequences
- The log file is flushed after each write to ensure data is preserved on crash
- Thread IDs help track which thread was active during a crash
- Each log entry is timestamped with millisecond precision

**Testing with Logging:**

When testing the service, logging should be enabled at level 2 (`-log2`) to verify correct operation:

```bash
# Start service with level 2 logging for testing
ncd_service start -log2

# Or in debug mode (automatically enables -log2)
ncd_service /agdb
```

**Log Verification:**
- **Level 1 (Error Logging):** All error conditions in the service must be logged at level 1 or higher. Any log entry containing "ERROR" indicates a failure.
- **Level 2 (Success Logging):** When a potential error is checked and the operation succeeds, a success message is logged at level 2.
- **Test Verification:** Tests should verify that no "ERROR" entries appear in the log file after service operations.

Example error-check logging pattern:
```
[L1] ERROR - Failed to initialize snapshot publisher    # Error occurred
[L2] Snapshot publisher initialized successfully         # Success path (no error)
```

To verify no errors in automated tests:
```bash
# After running service tests, check log for errors
if grep -i "ERROR" ~/.local/share/ncd/ncd_service.log; then
    echo "Test FAILED: Errors found in service log"
    exit 1
fi
```

### Service-Side Rescan

When the NCD service is running, `-r` (rescan) requests are handled by the service rather than the client. This provides several benefits:

- **Non-blocking operation**: The service continues serving queries from the old snapshot while building a new database
- **Atomic updates**: The new database is published atomically once scanning completes
- **Consistent state**: All clients see the same database generation after rescan completes

**How It Works:**

1. Client sends `REQUEST_RESCAN` IPC message to the service
2. Service transitions to `SCANNING` state (logged at level 0)
3. Service builds new `NcdDatabase` in memory (old snapshot remains available)
4. On completion, service atomically publishes new snapshot and bumps generation
5. Service returns to `READY` state

**IPC Protocol:**

| Message | Direction | Purpose |
|---------|-----------|---------|
| `NCD_MSG_REQUEST_RESCAN` | Client → Service | Request filesystem rescan |
| `NCD_REQUEST_RESCAN_OK` | Service → Client | Rescan accepted/started |
| `NCD_REQUEST_RESCAN_BUSY` | Service → Client | Service already scanning |

**Log Events:**

With `-log0` or higher, the service logs rescan operations:
```
[L0] REQUEST_RESCAN received (seq=42, drive_mask=0xFFFFFFFF, partial=0)
[L0] Service state changed to SCANNING
[L0] Starting filesystem rescan...
[L0] Service state changed back to READY after rescan
[L0] Database snapshot published after rescan
```

**Test Mode:**

When `NCD_TEST_MODE=1` is set, the service skips rescans to prevent scanning user drives during testing:
```
[L0] Rescan skipped: NCD_TEST_MODE is set
```

### IPC Diagnostic Tools

The NCD distribution includes standalone command-line tools for testing and debugging the service IPC protocol. These tools do not require the full NCD client and can be used for diagnostics, load testing, and protocol validation.

**Available Tools:**

| Tool | Purpose |
|------|---------|
| `ipc_ping_test` | Test service liveness and measure latency |
| `ipc_state_test` | Query service state and version info |
| `ipc_metadata_test` | Submit metadata updates (groups, exclusions, config) |
| `ipc_heuristic_test` | Submit heuristic search mappings |
| `ipc_rescan_test` | Request filesystem rescans |
| `ipc_flush_test` | Request immediate persistence to disk |
| `ipc_shutdown_test` | Request graceful service shutdown |
| `ipc_fuzzer` | Fuzz test the IPC protocol |
| `ipc_stress_test` | Load test with concurrent connections |
| `ipc_cli` | Interactive CLI for manual testing |

**Usage Examples:**

```bash
# Check if service is reachable
ipc_ping_test --once

# Continuous ping with latency stats
ipc_ping_test --continuous --interval 1000

# Get service state information
ipc_state_test --info

# Get JSON-formatted state
ipc_state_test --info --json

# Add a group via IPC
ipc_metadata_test --group-add @projects /home/user/projects

# Submit a heuristic mapping
ipc_heuristic_test --search "downloads" --path "/home/user/Downloads"

# Request full rescan
ipc_rescan_test --full

# Request rescan and wait for completion
ipc_rescan_test --full --wait --timeout 60

# Force immediate flush to disk
ipc_flush_test --verify

# Graceful shutdown
ipc_shutdown_test --timeout 10 --verify

# Fuzz test with bit flipping
ipc_fuzzer --bitflip --count 10000

# Stress test with 100 concurrent connections
ipc_stress_test --connections 100 --duration 60 --operation ping

# Interactive CLI
ipc_cli
> ping
> state
> version
> quit
```

**Exit Codes:**

All IPC tools use consistent exit codes:
- `0` - Success / Service running / Operation completed
- `1` - Service not running / Connection failed
- `2` - Timeout / Service busy
- `3` - Invalid parameter / Invalid response

**Build Location:**

The tools are built in the `test/` directory:
- Windows: `test\ipc_ping_test.exe`, etc.
- Linux: `test/ipc_ping_test`, etc.

## Shared Memory Architecture

NCD uses a platform-native shared memory (SHM) architecture for zero-copy state sharing between the resident service and clients.

### Design Principles

1. **No hardcoded offsets** - All offsets calculated using `sizeof()` and `offsetof()`
2. **Variable mount points** - Not limited to 26 drive letters; supports any mount path
3. **Platform-native text encoding**:
   - **Windows**: UTF-16 (`wchar_t`) - No conversion, full international character support
   - **Linux**: UTF-8 (`char`) - Native format
4. **Zero-copy client access** - Clients map SHM directly, no data copying

### Platform Abstraction Types

```c
/* src/shm_types.h */

#ifdef NCD_PLATFORM_WINDOWS
/* Windows: UTF-16 (WCHAR) for all paths */
typedef wchar_t NcdShmChar;
#define NCD_SHM_PATH_MAX NCD_MAX_PATH
#define NCD_SHM_TEXT_ENCODING NCD_TEXT_UTF16LE
#define shm_strlen wcslen
#define shm_strcpy wcscpy
#define shm_snprintf snwprintf
#define SHM_PATH_FMT "%ls"
#else
/* Linux: UTF-8 (char) for all paths */
typedef char NcdShmChar;
#define NCD_SHM_PATH_MAX NCD_MAX_PATH
#define NCD_SHM_TEXT_ENCODING NCD_TEXT_UTF8
#define shm_strlen strlen
#define shm_strcpy strcpy
#define shm_snprintf snprintf
#define SHM_PATH_FMT "%s"
#endif
```

### SHM Database Format

```
ShmDatabaseHeader (32 bytes)
  magic = SHM_DATABASE_MAGIC
  version = 1
  mount_count = N
  header_size = sizeof(ShmDatabaseHeader) + N*sizeof(ShmMountEntry)
  text_encoding = NCD_TEXT_UTF16LE or NCD_TEXT_UTF8
  
ShmMountEntry[N] (variable size)
  mount_point[NCD_SHM_PATH_MAX]  - Full mount path in platform encoding
  volume_label[64]               - Human-readable label
  dir_count                      - Number of directories
  dirs_offset                    - Byte offset to DirEntry array
  pool_offset                    - Byte offset to name pool
  pool_size                      - Size of name pool

DirEntry arrays                  - Directory entries
Name pools                       - String data in platform encoding
```

### Key Structures

```c
/* Platform-specific mount entry */
typedef struct {
    NcdShmChar mount_point[NCD_SHM_PATH_MAX];   /* Full mount path */
    NcdShmChar volume_label[64];                /* Human-readable name */
    uint32_t   dir_count;
    uint32_t   dirs_offset;      /* Offset from ShmDatabase start */
    uint32_t   pool_offset;      /* Offset from ShmDatabase start */
    uint32_t   pool_size;
} ShmMountEntry;

/* Database header */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    uint64_t checksum;
    uint32_t total_size;
    uint32_t header_size;
    uint32_t mount_count;
    uint32_t text_encoding;      /* NCD_TEXT_UTF8 or NCD_TEXT_UTF16LE */
} ShmDatabaseHeader;
```

### Size Calculation (No Hardcoded Offsets)

```c
size_t shm_database_compute_size(const NcdDatabase *db) {
    size_t size = sizeof(ShmDatabaseHeader);
    
    /* Mount entry array */
    size += db->drive_count * sizeof(ShmMountEntry);
    
    /* Align before first mount's data */
    size = (size + 7) & ~7;
    
    /* Data for each mount */
    for (int i = 0; i < db->drive_count; i++) {
        DriveData *drv = &db->drives[i];
        
        /* DirEntry array */
        size += drv->dir_count * sizeof(DirEntry);
        
        /* Align before pool */
        size = (size + 7) & ~7;
        
        /* Name pool - size depends on encoding! */
        size += drv->name_pool_len;
    }
    
    return size;
}
```

### Client Access Pattern

```c
/* Map and validate */
int state_backend_open_service(NcdStateView **out, NcdStateSourceInfo *info) {
    /* ... map database SHM ... */
    ShmDatabase *db = shm_map(db_shm);
    
    /* Validate encoding matches platform */
    if (db->hdr.text_encoding != NCD_SHM_TEXT_ENCODING) {
        return -1;  /* Encoding mismatch - service/client version skew */
    }
    
    /* Store pointer - NO COPYING! */
    view->data.service.db = db;
    *out = view;
    return 0;
}
```

### Benefits

| Metric | Before | After |
|--------|--------|-------|
| NcdStateView size | 45,456 bytes | ~48-64 bytes |
| International paths | Corrupted on Windows | Preserved (UTF-16) |
| Path encoding | ANSI code page | UTF-16 (Win) / UTF-8 (Linux) |
| Max path length | 64 chars (label) | 4096 chars (NCD_MAX_PATH) |
| Drive/mount limit | 64 fixed | Variable (FAM) |

## Agent Mode

NCD includes an "agent mode" API for LLM integration:

```bash
# Query the database
ncd --agent:query downloads --json --limit 10

# List directory contents (live filesystem)
ncd --agent:ls /home/user --depth 2 --json

# Show directory tree from database
ncd --agent:tree /home/user --depth 3 --json
```

### Agent Tree Output Formats

The `/agent tree` command supports multiple output formats:

**1. Indented Tree Format (default)**
Displays directories as an indented tree with 2 spaces per level:
```bash
$ ncd --agent:tree /home/user --depth 2
docs
  architecture
  history
```

**2. Flat Format with Full Paths (`--flat`)**
Displays directories as relative paths with platform-specific separators:
```bash
$ ncd --agent:tree /home/user --depth 2 --flat
docs/architecture
docs/history
```

**3. JSON Format (`--json`)**
Returns structured JSON with name and depth fields:
```bash
$ ncd --agent:tree /home/user --depth 2 --json
{"v":1,"tree":[
  {"n":"docs","d":0},
  {"n":"architecture","d":1},
  {"n":"history","d":1}
]}
```

**4. JSON Flat Format (`--json --flat`)**
Combines JSON structure with full relative paths:
```bash
$ ncd --agent:tree /home/user --depth 2 --json --flat
{"v":1,"tree":[
  {"n":"docs/architecture","d":1},
  {"n":"docs/history","d":1}
]}
```

| Option | Description |
|--------|-------------|
| `--depth N` | Maximum depth to traverse (default: 3) |
| `--json` | Output as JSON |
| `--flat` | Show full relative paths instead of just names |

**Exit Codes:**
- `0` - Path found, tree displayed
- `1` - Path not found in database or error

# Check if path exists in database
ncd --agent:check /home/user/projects

# Get database age
ncd --agent:check --db-age

# Get database statistics
ncd --agent:check --stats

# Check service status (returns: READY, STARTING, or NOT_RUNNING)
ncd --agent:check --service-status

# Shell tab-completion candidates
ncd --agent:complete dow --limit 20

# Create directory (creates parent directories as needed)
ncd --agent:mkdir /home/user/new/project

# Create directory tree from JSON or flat file format
ncd --agent:mkdirs --file tree.txt
ncd --agent:mkdirs '[{"name":"project","children":[{"name":"src"}]}]' --json
```

### Agent Mkdirs Input Formats

The `/agent mkdirs` command creates a complete directory tree structure from either a flat file or JSON input.

**1. Flat File Format (default)**
Uses 2-space indentation to indicate directory nesting:
```
$ cat tree.txt
project
  src
    core
    ui
  docs
  tests

$ ncd --agent:mkdirs --file tree.txt
Creating directory tree...

project: Directory created
project\src: Directory created
project\src\core: Directory created
project\src\ui: Directory created
project\docs: Directory created
project\tests: Directory created

Created 6 directories
```

**2. JSON Format**
Supports two JSON structures:

*Simple string array:*
```bash
$ ncd --agent:mkdirs '["dir1", "dir2", "dir3"]'
```

*Object tree with children:*
```bash
$ ncd --agent:mkdirs '[{"name":"project","children":[{"name":"src","children":[{"name":"core"}]}]}]'
```

**3. JSON Output Format (`--json`)**
Returns structured JSON with per-directory results:
```bash
$ ncd --agent:mkdirs --file tree.txt --json
{"v":1,"dirs":[
  {"path":"project","result":"created","message":"Directory created"},
  {"path":"project\\src","result":"created","message":"Directory created"}
]}
```

Result codes:
- `created` - Directory was created
- `exists` - Directory already exists
- `error_perms` - Permission denied
- `error_parent` - Failed to create parent directory
- `error_path` - Invalid path
- `error` - Other error

Exit codes:
- `0` - Success / Found
- `1` - Not found / Error

## Security Considerations

1. **Path escaping:** Result files escape special characters for batch/shell safety
   - Windows: Replaces `%`, `!`, quotes, control characters
   - Linux: Rejects shell metacharacters (`$`, `` ` ``, `|`, `&`, etc.)
2. **No elevated privileges:** Tool runs as regular user
3. **Database integrity:** Magic number, version checks, and CRC64 checksum on load
4. **Buffer safety:** All string operations use bounded copies
5. **Overflow checking:** Multiplication/addition operations check for overflow
6. **Symlink handling:** On Linux, symlinks are followed; no cycle detection currently
7. **Shared memory:** User-scoped naming prevents cross-user collisions

## Environment Variables

### `NCD_TEST_MODE`
When set to any non-empty value (e.g., `NCD_TEST_MODE=1`), disables automatic background rescans. This is used by the test suite to prevent tests from scanning user drives when the database is stale.

**Usage:**
```bash
# Disable background rescans during testing
export NCD_TEST_MODE=1
ncd some_search
```

**Note:** When running NCD manually (not through tests), the auto-rescan feature may trigger a full system scan (`ncd -r`) if the database is older than the configured interval (default: -1/disabled). To enable auto-rescan, use `ncd -c rescan_interval_hours=24`. To ensure it stays disabled, either:
- Set `NCD_TEST_MODE=1` before running NCD
- Use `ncd -c rescan_interval_hours=-1` to disable auto-rescan permanently

## Known Limitations

1. No symlink cycle detection on Linux
2. Windows-only: No Unicode/wide-char support (ANSI only)
3. Directory history limited to 9 entries (NCD_DIR_HISTORY_MAX); frequent search history limited to 100 entries (NCD_HEUR_MAX_ENTRIES)
4. Database refresh triggered manually, by configurable interval, or disabled
5. No network drive support on Linux (only local filesystems)
6. Requires external `../shared/` library for building

## File Descriptions

| File | Description |
|------|-------------|
| `src/ncd.h` | Core types, platform detection, limits, utility macros, binary format headers |
| `src/main.c` | CLI parsing, heuristics (history), result generation, main loop, version info |
| `src/database.c` | Binary DB load/save, path helpers, groups, config, metadata, exclusions, history |
| `src/scanner.c` | Multi-threaded filesystem scanning, mount enumeration, exclusion filtering |
| `src/matcher.c` | Chain matching, name index, fuzzy matching with Damerau-Levenshtein |
| `src/ui.c` | TUI implementation (selector with live filter, navigator, history browser, dialogs) |
| `src/platform.c` | NCD-specific platform wrappers (delegates to ../shared/) |
| `src/strbuilder.h` | Compatibility shim to shared StringBuilder API |
| `../shared/strbuilder.c` | Dynamic string builder with JSON escaping |
| `../shared/common.c` | Memory allocation wrappers with OOM handling |
| `src/state_backend.h` | State access abstraction interface |
| `src/state_backend_local.c` | Local disk state implementation |
| `src/state_backend_service.c` | Service-backed state via shared memory |
| `src/shared_state.h/.c` | Shared memory snapshot format and validation |
| `src/shm_platform.h` | Shared memory platform abstraction |
| `src/control_ipc.h` | IPC message protocol for service/client |
| `src/service_main.c` | Service executable entry point |
| `src/service_state.c/.h` | Service state management and dirty tracking |
| `src/service_publish.c/.h` | Snapshot publication and generation management |
| `test/test_framework.h` | Minimal unit testing framework macros |
| `test/test_database.c` | Database module unit tests |
| `test/test_matcher.c` | Matcher module unit tests |
| `test/test_shared_state.c` | Shared state validation tests |
| `test/test_service_lazy_load.c` | Service lazy loading and state machine tests |
| `test/test_service_parity.c` | Service vs standalone parity tests |
| `test/test_service_lifecycle.c` | Service start/stop/restart lifecycle tests |
| `test/test_service_integration.c` | NCD client service status integration tests |
| `test/test_service_database.c` | Service database loading and query tests |
| `test/test_service_ipc.c` | Service IPC communication protocol tests |

## Version History

- Current binary version: 2 (added CRC64 checksum)
- Current metadata format version: 1 (container format)
- Current heuristics version: 1
- Current config version: 3 (added rescan_interval_hours)
- Current build version: 1.3

Note: The metadata file (ncd.metadata) has its own format version (1), while the
config section inside it uses config version 3. These are independent version
numbers for different layers of the data structure.

When changing database format:
1. Increment `NCD_BIN_VERSION` in `ncd.h`
2. Update `BinFileHdr` structure if needed
3. Ensure backward compatibility or provide migration path
4. Update CRC64 calculation if header changes

### Config Format Changes

**Version 3** - Added `rescan_interval_hours` field to support configurable auto-rescan intervals (1-168 hours, or -1 for never).
