# NewChangeDirectory (NCD) - Agent Documentation

## Project Overview

NewChangeDirectory (NCD) is a cross-platform command-line directory navigation tool inspired by the classic Norton Commander (Norton Utilities) CD command. It maintains a database of directory paths across drives/mounts and provides fast, fuzzy search-based navigation with an interactive TUI.

**Key Features:**
- Fast directory search using a pre-built binary database format
- Interactive TUI for selecting from multiple matches (selector mode)
- Filesystem navigator for browsing directories (navigator mode, launched with `ncd .`)
- Cross-platform support: Windows (x64) and Linux (x64, including WSL)
- Multi-threaded directory scanning for performance
- Heuristics-based search result ranking (learns from user choices)
- Per-drive/per-mount database files for incremental updates
- Group/tag system for bookmarking frequently used directories
- Fuzzy matching with Damerau-Levenshtein distance
- Agent mode API for LLM integration

## Technology Stack

- **Language:** C11
- **Platforms:** Windows x64, Linux x64 (WSL compatible)
- **Build Tools:**
  - Windows: MSVC (`cl.exe`) via `build.bat`, or MinGW-w64 (`gcc`) via `Makefile`
  - Linux: GCC or Clang via `build.sh`
- **Threading:** Windows threads (Win32 API) / POSIX threads (pthreads)
- **UI:** Platform-native console I/O with ANSI escape sequences (Linux) / Win32 Console API (Windows)

## Project Structure

```
NewChangeDirectory/
├── src/                       # Source code
│   ├── ncd.h                 # Core types, constants, platform detection macros
│   ├── main.c                # Entry point, CLI parsing, orchestration, heuristics
│   ├── database.c/.h         # Database load/save (binary format), groups, config
│   ├── scanner.c/.h          # Multi-threaded directory enumeration
│   ├── matcher.c/.h          # Search matching algorithm (chain + fuzzy)
│   ├── ui.c/.h               # Interactive terminal UI (selector + navigator)
│   ├── platform.c/.h         # Platform abstraction layer (Windows/Linux)
│   ├── strbuilder.c/.h       # Dynamic string builder for JSON output
│   └── common.c              # Memory allocation wrappers
├── test/                     # Test suite
│   ├── test_framework.h/.c   # Minimal unit testing framework
│   ├── test_database.c       # Database module unit tests
│   ├── test_matcher.c        # Matcher module unit tests
│   ├── test_db_corruption.c  # Database corruption handling tests
│   ├── fuzz_database.c       # Fuzz testing for database loading
│   ├── bench_matcher.c       # Performance benchmarks
│   ├── Makefile              # Test build system
│   └── TESTING.md            # Testing documentation
├── ncd.bat                   # Windows wrapper script (CMD)
├── ncd                       # Linux wrapper script (Bash)
├── build.bat                 # Windows build (MSVC)
├── build.sh                  # Linux build (GCC/Clang)
├── Makefile                  # Cross-platform build (MinGW)
├── deploy.bat                # Windows deployment script
├── deploy.sh                 # Linux deployment script
├── src/ncd.vcxproj           # Visual Studio project file
├── src/ncd.sln               # Visual Studio solution file
├── NewChangeDirectory.exe    # Built Windows binary
├── LINUX_PORT_CHANGELIST.md  # Linux porting documentation
└── AGENTS.md                 # This file
```

## Module Organization

| Module | Purpose | Key Files |
|--------|---------|-----------|
| Core Types | Platform detection, shared structures, constants | `ncd.h` |
| Main | CLI parsing, heuristics, result generation, orchestration | `main.c` |
| Database | Binary DB load/save, path helpers, groups, config | `database.c/.h` |
| Scanner | Thread pool management, recursive directory scanning | `scanner.c/.h` |
| Matcher | Chain-matching algorithm, fuzzy matching with DL distance | `matcher.c/.h` |
| UI | Interactive selection list, filesystem navigator, dialogs | `ui.c/.h` |
| Platform | OS abstraction: console I/O, filesystem, threading, environment | `platform.c/.h` |
| String Builder | Dynamic string construction, JSON escaping | `strbuilder.c/.h` |
| Common | Memory allocation wrappers that exit on OOM | `common.c` |

## Build Commands

### Windows (MSVC)

```batch
:: Automatic Visual Studio detection and build
build.bat

:: Or manually from Visual Studio x64 Native Tools Command Prompt:
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /Isrc ^
   src\main.c src\database.c src\scanner.c src\matcher.c src\ui.c ^
   src\platform.c src\strbuilder.c src\common.c ^
   /Fe:NewChangeDirectory.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib
```

### Windows (MinGW-w64)

```batch
make                    # Release build
make debug             # Debug build with symbols
make clean             # Remove build artifacts
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
cl /nologo /W3 /O2 /Isrc /I. test_database.c test_framework.c src\database.c src\common.c /Fe:test_database.exe
cl /nologo /W3 /O2 /Isrc /I. test_matcher.c test_framework.c src\matcher.c src\database.c /Fe:test_matcher.exe
test_database.exe
test_matcher.exe
```

**Windows (MinGW):**
```batch
cd test
gcc -Wall -Wextra -I../src -I. -o test_database.exe test_database.c test_framework.c ../src/database.c ../src/common.c -lpthread
gcc -Wall -Wextra -I../src -I. -o test_matcher.exe test_matcher.c test_framework.c ../src/matcher.c ../src/database.c -lpthread
test_database.exe
test_matcher.exe
```

### Running Other Tests

```bash
# Fuzz tests (10,000 iterations)
cd test && make fuzz

# Performance benchmarks
cd test && make bench

# Database corruption tests
cd test && make corruption

# Recursive mount tests (Linux, requires root)
cd test && make recursive-mount
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

## Code Style Guidelines

### Naming Conventions

- **Functions:** `module_verb_noun()` (e.g., `db_load_binary()`, `platform_get_env()`)
- **Types:** PascalCase with suffix (e.g., `NcdDatabase`, `DriveData`, `DirEntry`)
- **Constants:** `NCD_UPPER_CASE` (e.g., `NCD_MAX_PATH`, `NCD_BIN_MAGIC`)
- **Macros:** `NCD_PREFIX` for public, `static` for file-local

### Platform Abstraction

Platform-specific code lives in `platform.c` with these patterns:

```c
#if NCD_PLATFORM_WINDOWS
    // Win32 API code
#elif NCD_PLATFORM_LINUX
    // POSIX/Linux code
#else
#error "Unsupported platform"
#endif
```

Platform macros defined in `ncd.h`:
- `NCD_PLATFORM_WINDOWS` / `NCD_PLATFORM_LINUX`
- `NCD_ARCH_X64`

### Memory Management

- Use `ncd_malloc()` / `ncd_realloc()` wrappers that exit on OOM
- Use `ncd_malloc_array()` for array allocations with overflow checking
- Database uses string pools to reduce memory fragmentation
- Binary loads use single blob allocation with pointer referencing

### String Safety

- Always use platform wrappers: `platform_strncpy_s()`, `platform_strncat_s()`
- These guarantee NUL-termination and report truncation

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
} NcdDatabase;
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

# Groups (bookmarks)
ncd /g @home              # Create group for current directory
ncd /g- @home             # Remove group
ncd /gl                   # List all groups
ncd @home                 # Jump to group

# Configuration
ncd /c                    # Edit configuration (set default options)

# History
ncd /f                    # Show frequent searches
ncd /fc                   # Clear history

# Agent mode (LLM integration)
ncd /agent query <search> [--json] [--limit N] [--depth]
ncd /agent ls <path> [--json] [--depth N] [--dirs-only|--files-only]
ncd /agent tree <path> [--json] [--depth N] [--flat]
ncd /agent check <path> | --db-age | --stats

# Help
ncd /?                    # or /h
ncd /v                    # Version info
```

### Wrapper Scripts

**Windows (`ncd.bat`):**
- Must be used to call `NewChangeDirectory.exe`
- Exe writes `%TEMP%\ncd_result.bat` with environment variables
- Batch file sources the result and executes `cd /d`

**Linux (`ncd`):**
- Must be sourced, not executed: `source ncd` or use shell function
- Exe writes `/tmp/ncd_result.sh` (or `$XDG_RUNTIME_DIR`)
- Script sources and executes `cd`

## Database Storage

### Locations

**Windows:**
- Per-drive databases: `%LOCALAPPDATA%\NCD\ncd_X.database`
- History: `%LOCALAPPDATA%\ncd.history`
- Config: `%LOCALAPPDATA%\NCD\ncd.config`
- Groups: `%LOCALAPPDATA%\NCD\ncd.groups`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- History: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.history`
- Config: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.config`
- Groups: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.groups`

### Binary Format

Magic: `NCDB` (0x4244434E)
Layout:
```
[BinFileHdr: 32 bytes]
[BinDriveHdr x drive_count: 80 bytes each]
[DirEntry[] + name_pool per drive]
[CRC64 checksum]
```

### Atomic Writes

All database writes use temp-file-then-rename pattern:
1. Write to `.tmp` file
2. Move current to `.old` (backup)
3. Move `.tmp` to final name
4. On failure: restore from `.old`

## Testing Strategy

The project includes a comprehensive test suite:

1. **Unit Tests:** Database and matcher module testing using custom framework
2. **Fuzz Tests:** Random binary file loading and JSON parsing
3. **Benchmarks:** Performance testing on databases of varying sizes
4. **Corruption Tests:** Handling of malformed/truncated database files

Manual testing matrix:
- Build verification: Windows x64 (MSVC), Windows x64 (MinGW), Linux x64 (GCC)
- Functional tests: Search exact/prefix chain matching, Multi-match selector UI, Selector to Navigator flow, Rescan forms, Frequent history promotion, Missing path handling, Fuzzy matching, Groups, Configuration editor

## Security Considerations

1. **Path escaping:** Result files escape special characters for batch/shell safety
2. **No elevated privileges:** Tool runs as regular user
3. **Database integrity:** Magic number, version checks, and CRC64 checksum on load
4. **Buffer safety:** All string operations use bounded copies
5. **Symlink handling:** On Linux, symlinks are followed; no cycle detection currently

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

### Why two UI modes?

1. **Selector:** List of search results (arrow keys to select)
2. **Navigator:** Browse filesystem hierarchy (Tab to enter, Backspace to exit)

This mirrors Norton Commander's behavior for both search-driven and browse-driven workflows.

## Agent Mode

NCD includes an "agent mode" API for LLM integration:

```bash
# Query the database
ncd /agent query downloads --json --limit 10

# List directory contents (live filesystem)
ncd /agent ls /home/user --depth 2 --json

# Show directory tree from database
ncd /agent tree /home/user --depth 3 --json

# Check if path exists in database
ncd /agent check /home/user/projects

# Get database age
ncd /agent check --db-age

# Get database statistics
ncd /agent check --stats
```

Exit codes:
- `0` - Success / Found
- `1` - Not found / Error

## Development Notes

### Adding a New Module

1. Create `src/newmodule.h` with include guards and `extern "C"` wrapper
2. Create `src/newmodule.c` with `#include "ncd.h"` at top
3. Add to build scripts (`build.bat`, `build.sh`, `Makefile`)
4. Add to `src/ncd.vcxproj` if using Visual Studio

### Platform-Specific Code

When adding platform-specific functionality:
1. Add declaration to `platform.h` with cross-platform signature
2. Implement in `platform.c` within appropriate `#if NCD_PLATFORM_XXX` block
3. Keep the API surface minimal and consistent across platforms

### Version Updates

When changing database format:
1. Increment `NCD_BIN_VERSION` in `ncd.h`
2. Update `BinFileHdr` structure if needed
3. Ensure backward compatibility or provide migration path
4. Update CRC64 calculation if header changes

## Known Limitations

1. No symlink cycle detection on Linux
2. Windows-only: No Unicode/wide-char support (ANSI only)
3. History limited to 20 entries
4. Database refresh only triggered manually or after 24 hours
5. No network drive support on Linux (only local filesystems)

## File Descriptions

| File | Description |
|------|-------------|
| `src/ncd.h` | Core types, platform detection, limits, utility macros |
| `src/main.c` | CLI parsing, heuristics (history), result generation, main loop |
| `src/database.c` | Binary DB load/save, path helpers, groups, config |
| `src/scanner.c` | Multi-threaded filesystem scanning, mount enumeration |
| `src/matcher.c` | Chain matching, name index, fuzzy matching |
| `src/ui.c` | TUI implementation (selector, navigator, dialogs) |
| `src/platform.c` | Platform abstraction (Windows/Linux implementations) |
| `src/strbuilder.c` | Dynamic string builder with JSON escaping |
| `src/common.c` | Memory allocation wrappers |
| `test/test_framework.h` | Minimal unit testing framework macros |
| `test/test_framework.c` | Test framework implementation |
| `test/test_database.c` | Database module unit tests |
| `test/test_matcher.c` | Matcher module unit tests |
| `test/fuzz_database.c` | Fuzz testing for database loading |
| `test/bench_matcher.c` | Performance benchmarks |
