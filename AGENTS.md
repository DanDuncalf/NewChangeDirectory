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
│   ├── test_service_win.bat       # [NEW] Windows Service tests (isolated)
│   ├── test_service_wsl.sh        # [NEW] WSL Service tests (isolated)
│   ├── test_ncd_win_standalone.bat   # [NEW] NCD Windows - standalone
│   ├── test_ncd_win_with_service.bat # [NEW] NCD Windows - with service
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

```batch
:: Automatic Visual Studio detection and build
build.bat

:: Or manually from Visual Studio x64 Native Tools Command Prompt:
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

### Test Isolation (CRITICAL)

All tests must be **completely isolated** from the user's real data:

| Resource | User Data | Test Data |
|----------|-----------|-----------|
| **Drives/Mounts** | `C:`, `D:`, etc. | VHD (Windows) or ramdisk (WSL) |
| **Metadata** | `%LOCALAPPDATA%\NCD\ncd.metadata` | Temp directory via `-conf` or env override |
| **Databases** | `%LOCALAPPDATA%\NCD\ncd_*.database` | Temp location via `/d <path>` |

**Rules for writing tests:**
1. **Never use bare `/r`** - Always use `/r.` (subdirectory) or `/rDRIVE` (specific drive)
2. **Never scan user drives** - Only scan the test VHD/ramdisk/temp directory
3. **Never modify user metadata** - Use `-conf <temp_path>` for custom metadata location
4. **Clean up after tests** - Remove all test files and databases
5. **Use `NCD_TEST_MODE=1`** - Set this environment variable to disable background rescans during testing

### Comprehensive Test Runner (6 Test Suites)

The comprehensive test runner executes **six test suites** organized by platform and whether testing the service in isolation or the NCD client with/without service:

| # | Test Script | Platform | Description |
|---|-------------|----------|-------------|
| 1 | `test_service_win.bat` | Windows | Service tests without client (isolated) |
| 2 | `test_service_wsl.sh` | WSL | Service tests without client (isolated) |
| 3 | `test_ncd_win_standalone.bat` | Windows | NCD client tests - standalone mode |
| 4 | `test_ncd_win_with_service.bat` | Windows | NCD client tests - with service |
| 5 | `test_ncd_wsl_standalone.sh` | WSL | NCD client tests - standalone mode |
| 6 | `test_ncd_wsl_with_service.sh` | WSL | NCD client tests - with service |

**Quick Run (All Tests from Root):**
```batch
:: Builds and runs all 6 test suites
build_and_run_alltests.bat
```

**Run Individual Test Suites:**
```batch
:: Windows Service tests (isolated)
test\test_service_win.bat

:: Windows NCD without service
test\test_ncd_win_standalone.bat

:: Windows NCD with service
test\test_ncd_win_with_service.bat
```

```bash
# WSL Service tests (isolated)
bash test/test_service_wsl.sh

# WSL NCD without service
bash test/test_ncd_wsl_standalone.sh

# WSL NCD with service
bash test/test_ncd_wsl_with_service.sh
```

**Alternative Runners:**

*PowerShell (Windows or WSL):*
```powershell
cd test
powershell -ExecutionPolicy Bypass -File run_all_tests.ps1
```

*Bash (WSL):*
```bash
cd test
./run_all_tests.sh
```

*Makefile (WSL):*
```bash
cd test
make all-environments          # All environments
make all-environments-standalone # Standalone only (no service)
```

**Options:**
```bash
--windows-only       # Run only Windows tests
--wsl-only           # Run only WSL tests
--no-service         # Skip tests with service running
--skip-unit-tests    # Skip unit tests
--skip-feature-tests # Skip feature tests
--verbose            # Show detailed test output
```

This ensures all functionality works correctly regardless of platform or whether the optional resident service is running.

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

The NCD test suite contains **325+ unit tests** across multiple test files. See `test/README.md` for comprehensive documentation.

**Coverage by Category:**

| Category | Tests | Coverage % | Notes |
|----------|-------|------------|-------|
| Database operations | 52 | 90% | Excellent |
| Search/matching | 23 | 85% | Very good |
| CLI parsing | 62 | 90% | Good (extended tests added) |
| Agent mode args | 47 | 75% | Good (parsing + integration) |
| Platform abstraction | 46 | 70% | Good (extended tests added) |
| Shared library | 61 | 80% | Good (string builder + memory) |
| Service lifecycle | 46 | 80% | Good |
| Integration/feature | 119+ | 85% | Good (exit codes + behavior) |
| **Overall** | **325+** | **~85%** | **Very good for C project** |

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
build-and-run-tests.bat
```

**Note:** Service tests require the service executable to be built:
- Windows: `NCDService.exe` 
- Linux: `ncd_service`

Tests will skip gracefully if the service executable is not available.

**Test Infrastructure Note (Windows):**
NCD automatically detects when stdout is redirected (e.g., in tests or when piping output) and switches from direct console writes (`CONOUT$`) to standard stdout output. This allows:
- Help text output (`ncd /?`) to be captured via pipe redirection for testing
- Proper operation in both interactive (console) and batch (redirected) modes

The detection uses `GetConsoleMode()` on Windows and `isatty()` on Linux. When redirected, output goes through `fputs(stdout)`; when in a console, output uses `WriteConsoleA()` for better TUI performance.

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

## Usage

### Command Line

```bash
# Search for a directory
ncd downloads
ncd scott\downloads

# Force rescan
ncd /r                    # All drives
ncd /rBDE                 # Specific drives only (B:,D:,E:)
cd /r-b-d                 # Exclude drives B: and D:
cd /r.                    # Rescan current subdirectory only

# Options
ncd /i scott\appdata      # Include hidden directories
ncd /s                    # Include system directories
ncd /a                    # Include all (hidden + system)
ncd /z                    # Fuzzy matching with DL distance

# Interactive navigation
ncd .                     # Navigate from current directory
ncd C:                    # Navigate from drive root (Windows)

# Groups (bookmarks) - supports multiple directories per group
ncd /g @home              # Add current directory to group
ncd /g- @home             # Remove current directory from group
ncd /gl                   # List all groups
ncd @home                 # Jump to group (shows selector if multiple)

# Directory history
ncd /0                    # Ping-pong between two most recent directories
ncd /h                    # Browse history interactively (Del to remove)
ncd /hl                   # List directory history
ncd /hc                   # Clear all history
ncd /hc3                  # Remove history entry #3

# Exclusions
ncd /x <pattern>          # Add exclusion pattern (e.g., /x C:Windows)
ncd /x- <pattern>         # Remove exclusion pattern
ncd /xl                   # List all exclusions

(Note: `-x`, `-x-`, `-xl` are also accepted)

# Configuration
ncd /c                    # Edit configuration (set default options)

# Frequent searches
ncd /f                    # Show frequent searches
ncd /fc                   # Clear frequent searches

# Agent mode (LLM integration)
ncd /agent query <search> [--json] [--limit N] [--depth]
ncd /agent ls <path> [--json] [--depth N] [--dirs-only|--files-only]
ncd /agent tree <path> [--json] [--depth N] [--flat]  # Show directory tree from DB
ncd /agent check <path> | --db-age | --stats | --service-status
ncd /agent complete <partial> [--limit N]  # Shell tab-completion
ncd /agent mkdir <path>  # Create single directory
ncd /agent mkdirs [--file <path>] [--json] <content>  # Create directory tree

# Service management
ncd_service start         # Start resident service (faster startup)
ncd_service stop          # Stop resident service
ncd /agent check --service-status  # Check if service is running

# Configuration Override
ncd -conf <path>          # Use custom metadata file path
ncd_service start -conf <path>  # Service with custom metadata path

# Help
ncd /?                    # or /h
ncd /v                    # Version info
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

The interactive history browser (`ncd /h`) shows recently visited directories:

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
- Legacy (migrated to metadata): `%LOCALAPPDATA%\NCD\ncd.config`, `ncd.groups`, `ncd.history`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- Metadata: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.metadata`

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
- Per-drive database files are still loaded from their default locations or the location specified by `/d <path>`
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

The auto-rescan interval is now configurable. Use `ncd /c` to edit the configuration and set:

- **Auto-rescan hours**: Number of hours (1-168) before automatically triggering a background rescan
- **Never** (-1): Disable auto-rescan entirely

The default is -1 (never auto-rescan). When set to a positive value and the database is older than the configured interval, NCD will automatically spawn a background rescan after a successful navigation.

**⚠️ WARNING:** The background rescan uses `ncd /r` which scans **ALL drives/mounts**. If you don't want this behavior, set `rescan_interval_hours=-1` or be aware that using `ncd` manually (outside of test mode) may trigger full system scans.

### Why two UI modes?

1. **Selector:** List of search results with live filtering (arrow keys to select, type to filter)
2. **Navigator:** Browse filesystem hierarchy (Tab to enter, Backspace to exit)

This mirrors Norton Commander's behavior for both search-driven and browse-driven workflows.

### Why an optional resident service?

- Keeps metadata and database hot in shared memory
- Eliminates disk I/O on client startup when service is running
- Client falls back to standalone mode if service unavailable
- Service handles persistence and snapshot publication

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
ncd /agent query downloads --json --limit 10

# List directory contents (live filesystem)
ncd /agent ls /home/user --depth 2 --json

# Show directory tree from database
ncd /agent tree /home/user --depth 3 --json
```

### Agent Tree Output Formats

The `/agent tree` command supports multiple output formats:

**1. Indented Tree Format (default)**
Displays directories as an indented tree with 2 spaces per level:
```bash
$ ncd /agent tree /home/user --depth 2
docs
  architecture
  history
```

**2. Flat Format with Full Paths (`--flat`)**
Displays directories as relative paths with platform-specific separators:
```bash
$ ncd /agent tree /home/user --depth 2 --flat
docs/architecture
docs/history
```

**3. JSON Format (`--json`)**
Returns structured JSON with name and depth fields:
```bash
$ ncd /agent tree /home/user --depth 2 --json
{"v":1,"tree":[
  {"n":"docs","d":0},
  {"n":"architecture","d":1},
  {"n":"history","d":1}
]}
```

**4. JSON Flat Format (`--json --flat`)**
Combines JSON structure with full relative paths:
```bash
$ ncd /agent tree /home/user --depth 2 --json --flat
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
ncd /agent check /home/user/projects

# Get database age
ncd /agent check --db-age

# Get database statistics
ncd /agent check --stats

# Check service status (returns: READY, STARTING, or NOT_RUNNING)
ncd /agent check --service-status

# Shell tab-completion candidates
ncd /agent complete dow --limit 20

# Create directory (creates parent directories as needed)
ncd /agent mkdir /home/user/new/project

# Create directory tree from JSON or flat file format
ncd /agent mkdirs --file tree.txt
ncd /agent mkdirs '[{"name":"project","children":[{"name":"src"}]}]' --json
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

$ ncd /agent mkdirs --file tree.txt
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
$ ncd /agent mkdirs '["dir1", "dir2", "dir3"]'
```

*Object tree with children:*
```bash
$ ncd /agent mkdirs '[{"name":"project","children":[{"name":"src","children":[{"name":"core"}]}]}]'
```

**3. JSON Output Format (`--json`)**
Returns structured JSON with per-directory results:
```bash
$ ncd /agent mkdirs --file tree.txt --json
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

**Note:** When running NCD manually (not through tests), the auto-rescan feature may trigger a full system scan (`ncd /r`) if the database is older than the configured interval (default: -1/disabled). To enable auto-rescan, use `ncd /c rescan_interval_hours=24`. To ensure it stays disabled, either:
- Set `NCD_TEST_MODE=1` before running NCD
- Use `ncd /c rescan_interval_hours=-1` to disable auto-rescan permanently

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
