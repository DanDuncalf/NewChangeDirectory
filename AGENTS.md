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
│   ├── strbuilder.c/.h       # Dynamic string builder for JSON output
│   ├── common.c              # Memory allocation wrappers
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
│   ├── test_framework.h/.c   # Minimal unit testing framework
│   ├── test_database.c       # Database module unit tests
│   ├── test_matcher.c        # Matcher module unit tests
│   ├── test_db_corruption.c  # Database corruption handling tests
│   ├── test_bugs.c           # Known bug detection tests
│   ├── test_shared_state.c   # Shared state tests
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
| String Builder | Dynamic string construction, JSON escaping | `strbuilder.c/.h` |
| Common | Memory allocation wrappers that exit on OOM | `common.c` |

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

**Database Tests:**
- `create_and_free` - Database creation and cleanup
- `add_drive` - Adding drives to database
- `add_directory` - Adding directories with parent relationships
- `full_path_reconstruction` - Reconstructing full paths from parent chain
- `binary_save_load_roundtrip` - Binary format serialization/deserialization
- `binary_load_corrupted_rejected` - Rejection of corrupted binary files
- `binary_load_truncated_rejected` - Rejection of truncated binary files

**Matcher Tests:**
- `match_single_component` - Single-component search matching
- `match_two_components` - Multi-component path matching
- `match_case_insensitive` - Case-insensitive matching
- `match_with_hidden_filter` - Hidden directory filtering
- `match_no_results` - Empty result handling
- `match_empty_search` - Empty search handling

**Shared State Tests:**
- `header_validation` - Snapshot header validation
- `checksum_validation` - CRC64 checksum verification
- `section_lookup` - Section table navigation
- `bounds_checking` - Memory bounds validation

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
ncd -x C:Windows          # Add exclusion pattern
ncd -x- C:Windows         # Remove exclusion pattern
ncd -xl                   # List all exclusions

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

# Service management
ncd_service start         # Start resident service (faster startup)
ncd_service stop          # Stop resident service
ncd /agent check --service-status  # Check if service is running

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

The default is 24 hours. When the database is older than the configured interval, NCD will automatically spawn a background rescan after a successful navigation.

### Why two UI modes?

1. **Selector:** List of search results with live filtering (arrow keys to select, type to filter)
2. **Navigator:** Browse filesystem hierarchy (Tab to enter, Backspace to exit)

This mirrors Norton Commander's behavior for both search-driven and browse-driven workflows.

### Why an optional resident service?

- Keeps metadata and database hot in shared memory
- Eliminates disk I/O on client startup when service is running
- Client falls back to standalone mode if service unavailable
- Service handles persistence and snapshot publication

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
    code_quality.md
    linux_port.md
    test_strategy.md
  history
    baseline.md
    final_summary.md
```

**2. Flat Format with Full Paths (`--flat`)**
Displays directories as relative paths with platform-specific separators:
```bash
$ ncd /agent tree /home/user --depth 2 --flat
architecture/code_quality.md
architecture/linux_port.md
architecture/test_strategy.md
history/baseline.md
history/final_summary.md
```

**3. JSON Format (`--json`)**
Returns structured JSON with name and depth fields:
```bash
$ ncd /agent tree /home/user --depth 2 --json
{"v":1,"tree":[
  {"n":"architecture","d":0},
  {"n":"code_quality.md","d":1},
  {"n":"linux_port.md","d":1},
  {"n":"test_strategy.md","d":1},
  {"n":"history","d":0},
  {"n":"baseline.md","d":1},
  {"n":"final_summary.md","d":1}
]}
```

**4. JSON Flat Format (`--json --flat`)**
Combines JSON structure with full relative paths:
```bash
$ ncd /agent tree /home/user --depth 2 --json --flat
{"v":1,"tree":[
  {"n":"architecture/code_quality.md","d":1},
  {"n":"architecture/linux_port.md","d":1},
  {"n":"architecture/test_strategy.md","d":1},
  {"n":"history/baseline.md","d":1},
  {"n":"history/final_summary.md","d":1}
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
```

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

## Known Limitations

1. No symlink cycle detection on Linux
2. Windows-only: No Unicode/wide-char support (ANSI only)
3. History limited to 100 entries (NCD_HEUR_MAX_ENTRIES)
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
| `src/strbuilder.c` | Dynamic string builder with JSON escaping |
| `src/common.c` | Memory allocation wrappers with OOM handling |
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

## Version History

- Current binary version: 2 (added CRC64 checksum)
- Current metadata version: 1
- Current heuristics version: 1
- Current config version: 3 (added rescan_interval_hours)
- Current build version: 1.2

When changing database format:
1. Increment `NCD_BIN_VERSION` in `ncd.h`
2. Update `BinFileHdr` structure if needed
3. Ensure backward compatibility or provide migration path
4. Update CRC64 calculation if header changes

### Config Format Changes

**Version 3** - Added `rescan_interval_hours` field to support configurable auto-rescan intervals (1-168 hours, or -1 for never).
