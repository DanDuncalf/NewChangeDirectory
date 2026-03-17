# NewChangeDirectory (NCD) - Agent Documentation

## Project Overview

NewChangeDirectory (NCD) is a cross-platform command-line directory navigation tool inspired by the classic Norton Commander (Norton Utilities) CD command. It maintains a database of directory paths across drives/mounts and provides fast, fuzzy search-based navigation.

**Key Features:**
- Fast directory search using a pre-built database (binary format for speed)
- Interactive TUI for selecting from multiple matches
- Filesystem navigator for browsing (launched with `ncd .`)
- Cross-platform support: Windows (x64) and Linux (x64)
- Multi-threaded directory scanning for performance
- Heuristics-based search result ranking (learns from user choices)
- Per-drive/per-mount database files for incremental updates

## Technology Stack

- **Language:** C11
- **Platforms:** Windows x64, Linux x64 (WSL compatible)
- **Build Tools:**
  - Windows: MSVC (`cl.exe`) or MinGW-w64 (`gcc`)
  - Linux: GCC or Clang
- **Threading:** Windows threads (Win32 API) / POSIX threads (pthreads)
- **UI:** Platform-native console I/O with ANSI escape sequences

## Project Structure

```
NewChangeDirectory/
├── src/                       # Source code
│   ├── ncd.h                 # Core types, constants, platform detection
│   ├── main.c                # Entry point, CLI parsing, orchestration
│   ├── database.c/.h         # Database load/save (JSON + binary formats)
│   ├── scanner.c/.h          # Multi-threaded directory enumeration
│   ├── matcher.c/.h          # Search matching algorithm
│   ├── ui.c/.h               # Interactive terminal UI
│   ├── platform.c/.h         # Platform abstraction layer
│   ├── json.c/.h             # JSON utilities (legacy support)
│   └── platform_refactored.c # Platform code (alternate)
├── ncd.bat                   # Windows wrapper script (CMD)
├── ncd.sh                    # Linux wrapper script (Bash)
├── build.bat                 # Windows build (MSVC)
├── build.sh                  # Linux build (GCC)
├── Makefile                  # Cross-platform build (MinGW)
├── deploy.bat                # Deployment script
├── NewChangeDirectory.exe    # Built Windows binary
├── _NCD.EXE                  # Alternative build
├── LINUX_PORT_CHANGELIST.md  # Linux porting notes
└── AGENTS.md                 # This file
```

## Code Organization

### Module Overview

| Module | Purpose |
|--------|---------|
| `main.c` | CLI argument parsing, heuristics management, result file generation |
| `database.c` | Binary and JSON database formats, atomic file operations |
| `scanner.c` | Thread pool management, recursive directory scanning |
| `matcher.c` | Chain-matching algorithm for partial path queries |
| `ui.c` | Interactive selection list and filesystem navigator |
| `platform.c` | OS abstraction: console I/O, filesystem, threading, environment |

### Key Data Structures

**DirEntry** (12 bytes) - Compact directory node:
```c
typedef struct {
    int32_t  parent;      // Parent index (-1 for root)
    uint32_t name_off;    // Offset into name_pool
    uint8_t  is_hidden;
    uint8_t  is_system;
} DirEntry;
```

**NcdDatabase** - In-memory database:
```c
typedef struct {
    int        version;
    bool       default_show_hidden;
    bool       default_show_system;
    time_t     last_scan;
    DriveData *drives;
    int        drive_count;
    // ... blob support for memory-mapped binary loads
} NcdDatabase;
```

## Build Instructions

### Windows (MSVC)

Requirements: Visual Studio 2019/2022 with C++ workload

```batch
:: From Visual Studio x64 Native Tools Command Prompt
build.bat

:: Or manually:
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /Isrc ^
   src\main.c src\database.c src\scanner.c src\matcher.c src\ui.c src\platform.c ^
   /Fe:NewChangeDirectory.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib
```

### Windows (MinGW-w64)

```batch
make
:: or
make debug
:: or
make install INSTALL_DIR=C:\Tools\bin
```

### Linux (GCC)

```bash
chmod +x build.sh
./build.sh

# Or with Clang:
CC=clang ./build.sh
```

## Usage

### Command Line

```bash
# Search for a directory
ncd downloads
ncd scott\downloads

# Force rescan
ncd /r                    # All drives
ncd /rBDE                 # Specific drives only
ncd /r-b-d                # Exclude drives B: and D:

# Options
ncd /i scott\appdata      # Include hidden directories
ncd /s                    # Include system directories
ncd /a                    # Include all (hidden + system)

# Interactive navigation
ncd .                     # Navigate from current directory
ncd C:                    # Navigate from drive root

# History
ncd /f                    # Show frequent searches
ncd /fc                   # Clear history

# Help
ncd /?                    # or /h
ncd /v                    # Version info
```

### Wrapper Scripts

**Windows (`ncd.bat`):**
- Must be used to call `NewChangeDirectory.exe`
- Exe writes `%TEMP%\ncd_result.bat` with environment variables
- Batch file sources the result and executes `cd /d`

**Linux (`ncd.sh`):**
- Must be sourced, not executed: `source ncd.sh` or `alias ncd='source /path/to/ncd.sh'`
- Exe writes `/tmp/ncd_result.sh` (or `$XDG_RUNTIME_DIR`)
- Script sources and executes `cd`

## Database Storage

### Locations

**Windows:**
- Per-drive databases: `%LOCALAPPDATA%\NCD\ncd_X.database`
- History: `%LOCALAPPDATA%\ncd.history`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- History: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.history`

### Binary Format

Magic: `NCDB` (0x4244434E)
Layout:
```
[BinFileHdr: 24 bytes]
[BinDriveHdr × drive_count: 80 bytes each]
[DirEntry[] + name_pool per drive]
```

### Atomic Writes

All database writes use temp-file-then-rename pattern:
1. Write to `.tmp` file
2. Move current to `.old` (backup)
3. Move `.tmp` to final name
4. On failure: restore from `.old`

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

- Use `xmalloc()` / `xrealloc()` wrappers that exit on OOM
- Database uses string pools to reduce memory fragmentation
- Binary loads use single blob allocation with pointer referencing

### String Safety

- Always use platform wrappers: `platform_strncpy_s()`, `platform_strncat_s()`
- These guarantee NUL-termination and report truncation

## Testing Strategy

No automated test suite is included. Manual testing matrix:

1. **Build verification:**
   - Windows x64 (MSVC)
   - Windows x64 (MinGW)
   - Linux x64 (GCC)

2. **Functional tests:**
   - Search exact/prefix chain
   - Multi-match selector UI
   - Selector → Navigator → Enter/Backspace/Esc flow
   - Rescan all/include/exclude forms
   - Frequent history promotion
   - Missing path handling

## Security Considerations

1. **Path escaping:** Result files escape special characters for batch/shell safety
2. **No elevated privileges:** Tool runs as regular user
3. **Database integrity:** Magic number and version checks on load
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

## Known Limitations

1. No automated tests - relies on manual verification
2. No symlink cycle detection on Linux
3. Windows-only: No Unicode/wide-char support (ANSI only)
4. History limited to 20 entries
5. Database refresh only triggered manually or after 24 hours

## Future Work (from LINUX_PORT_CHANGELIST.md)

The Linux port is mostly complete per the changelist. Remaining items:
- Full test matrix execution
- Performance benchmarking vs Windows build
- WSL-specific mount handling optimization

## Contact / Contributing

This is a personal tool. Changes should maintain:
- Cross-platform compatibility
- Minimal dependencies
- Single-file deployment model
- Norton Commander UI fidelity
