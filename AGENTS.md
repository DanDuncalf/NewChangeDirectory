# NewChangeDirectory (NCD) - Agent Documentation

## Project Overview

NewChangeDirectory (NCD) is a cross-platform command-line directory navigation tool inspired by the classic Norton Commander (Norton Utilities) CD command. It maintains a database of directory paths across drives/mounts and provides fast, fuzzy search-based navigation.

**Key Features:**
- Fast directory search using a pre-built binary database for speed
- Interactive TUI for selecting from multiple matches (arrow keys, Enter to select)
- Filesystem navigator for browsing (launched with `ncd .` or `ncd C:`)
- Cross-platform support: Windows (x64) and Linux (x64, including WSL)
- Multi-threaded directory scanning for performance
- Heuristics-based search result ranking (learns from user choices)
- Per-drive/per-mount database files for incremental updates
- Command-line history tracking (last 20 unique searches)

**Communication Model:**
NCD cannot directly change the parent shell's working directory (OS limitation). Instead:
1. The executable writes a result file with environment variables
2. A wrapper script (`ncd.bat` on Windows, `ncd` bash script on Linux) sources the result
3. The wrapper performs the actual `cd` command

## Technology Stack

- **Language:** C11
- **Platforms:** Windows x64, Linux x64 (native and WSL compatible)
- **Threading:** Windows threads (Win32 API) / POSIX threads (pthreads)
- **UI:** Platform-native console I/O with ANSI escape sequences
- **Build Tools:**
  - Windows: MSVC (`cl.exe`) via `build.bat`, or MinGW-w64 (`gcc`) via `Makefile`
  - Linux: GCC or Clang via `build.sh`

## Project Structure

```
NewChangeDirectory/
├── src/                       # Source code
│   ├── ncd.h                 # Core types, constants, platform detection macros
│   ├── main.c                # Entry point, CLI parsing, heuristics management
│   ├── database.c/.h         # Database load/save (binary format + legacy JSON)
│   ├── scanner.c/.h          # Multi-threaded directory enumeration
│   ├── matcher.c/.h          # Chain-matching algorithm for partial path queries
│   ├── ui.c/.h               # Interactive terminal UI (selector + navigator)
│   ├── platform.c/.h         # Platform abstraction layer (Windows/Linux)
│   ├── strbuilder.c/.h       # Dynamic string builder for JSON output
│   ├── common.c              # Memory allocation utilities
│   └── ncd.vcxproj           # Visual Studio project file
├── ncd.bat                   # Windows wrapper script (CMD) - REQUIRED for use
├── ncd                       # Linux wrapper script (Bash) - REQUIRED for use
├── build.bat                 # Windows build script (MSVC, auto-detects VS)
├── build.sh                  # Linux build script (GCC/Clang)
├── Makefile                  # Cross-platform build (MinGW on Windows)
├── deploy.bat                # Windows deployment script
├── deploy.sh                 # Linux deployment script (installs to /usr/local/bin)
├── NewChangeDirectory.exe    # Built Windows binary
├── NewChangeDirectory        # Built Linux binary
├── LINUX_PORT_CHANGELIST.md  # Linux porting implementation notes
└── AGENTS.md                 # This file
```

## Module Overview

| Module | Purpose |
|--------|---------|
| `main.c` | CLI argument parsing, heuristics management, result file generation, orchestration |
| `database.c` | Binary and JSON database formats, atomic file operations, memory management |
| `scanner.c` | Thread pool management, recursive directory scanning with progress callbacks |
| `matcher.c` | Chain-matching algorithm for partial path queries (case-insensitive, substring-based) |
| `ui.c` | Interactive selection list and filesystem navigator (platform-specific I/O layer) |
| `platform.c` | OS abstraction: console I/O, filesystem, threading, environment, atomic operations |
| `strbuilder.c` | Dynamic string builder for JSON serialization |
| `common.c` | Memory allocation wrappers with error handling |

## Build Instructions

### Windows (MSVC - Recommended)

Requirements: Visual Studio 2019/2022 with C++ workload

```batch
:: Auto-detects Visual Studio and builds
build.bat

:: Or from Visual Studio x64 Native Tools Command Prompt:
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /Isrc ^
   src\main.c src\database.c src\scanner.c src\matcher.c src\ui.c src\platform.c ^
   src\strbuilder.c src\common.c ^
   /Fe:NewChangeDirectory.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib
```

### Windows (MinGW-w64)

```batch
make                    # Build release binary
make debug             # Build with debug symbols
make clean             # Remove build artifacts
make install           # Copy ncd.bat + exe to INSTALL_DIR (default: C:/Windows/System32)
```

### Linux (GCC/Clang)

```bash
chmod +x build.sh
./build.sh

# Or with Clang:
CC=clang ./build.sh

# Deploy to /usr/local/bin:
./deploy.sh
```

## Usage

**IMPORTANT:** Always use the wrapper script - never call the executable directly.

### Windows
```batch
ncd downloads           # Search for directories matching "downloads"
ncd scott\downloads     # Search for chain: scott -> downloads
ncd /r                  # Force rescan of all drives
ncd /rBDE               # Rescan specific drives only (B:, D:, E:)
ncd /r-b-d              # Rescan all drives EXCEPT B: and D:
ncd /i scott\appdata    # Include hidden directories
ncd /s                  # Include system directories
ncd /a                  # Include all (hidden + system)
ncd .                   # Open navigator from current directory
ncd C:                  # Open navigator from drive root
ncd /f                  # Show frequent search history
ncd /fc                 # Clear history
ncd /v                  # Version info
ncd /?                  # Help
```

### Linux
```bash
ncd downloads           # Search for directories matching "downloads"
ncd /r                  # Force rescan of all mounts
ncd /r /                # Scan only root filesystem (skip /mnt/*)
ncd .                   # Open navigator from current directory
# Other options same as Windows
```

**Note:** The Linux wrapper must be sourced, not executed:
```bash
source ncd <search>     # Or use the function added by deploy.sh
```

## Database Storage

### Locations

**Windows:**
- Per-drive databases: `%LOCALAPPDATA%\NCD\ncd_X.database` (X = drive letter)
- History: `%LOCALAPPDATA%\ncd.history`

**Linux:**
- Per-mount databases: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd_XX.database`
- History: `${XDG_DATA_HOME:-$HOME/.local/share}/ncd/ncd.history`

### Binary Format

Magic: `NCDB` (0x4244434E little-endian)

Layout:
```
[BinFileHdr: 32 bytes]           # magic, version, flags, timestamp, drive_count, checksum
[BinDriveHdr × N: 80 bytes each] # letter, type, label, dir_count, pool_size
[DirEntry[] + name_pool per drive]
```

**DirEntry Structure** (12 bytes):
```c
typedef struct {
    int32_t  parent;       # Parent index (-1 for root)
    uint32_t name_off;     # Offset into name_pool
    uint8_t  is_hidden;
    uint8_t  is_system;
} DirEntry;
```

**Key Design Decisions:**
- Names stored in per-drive string pools (not inline in DirEntry)
- Reduced DirEntry size from 268 bytes to 12 bytes
- Single heap buffer allocation for binary blob loading
- CRC64 checksum for corruption detection (ECMA-182 polynomial)

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

- Use `ncd_malloc()` / `ncd_realloc()` wrappers that exit on OOM
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
   - Search exact/prefix chain matching
   - Multi-match selector UI (arrow keys, Enter, Escape)
   - Selector → Navigator → Enter/Backspace/Esc flow
   - Rescan all/include/exclude forms (`/r`, `/rBDE`, `/r-b-d`)
   - Frequent history promotion (`/f`, `/fc`)
   - Missing path handling (triggers rescan behavior)
   - Direct navigation mode (`ncd .` and `ncd C:`)

## Security Considerations

1. **Path escaping:** Result files escape special characters for batch/shell safety
2. **No elevated privileges:** Tool runs as regular user
3. **Database integrity:** Magic number and version checks on load
4. **Buffer safety:** All string operations use bounded copies
5. **Symlink handling:** On Linux, symlinks are followed; cycle detection via (dev,ino) hash set
6. **Input validation:** Search strings sanitized before use in heuristics
7. **CRC64 checksum:** Binary database files include integrity verification

## Architecture Decisions

### Why a wrapper script?

A child process cannot change the working directory of its parent shell (OS limitation). The exe writes a result file; the wrapper sources it and calls `cd`.

### Why binary database format?

- JSON parsing is slow for 100K+ directories
- Binary format allows memory-mapping (single allocation)
- DirEntry shrunk from 268 bytes to 12 bytes using string pools
- CRC64 checksum detects file corruption

### Why per-drive databases?

- Allows incremental updates without rescanning everything
- Enables fast fallback search across all drives
- Supports removable media that may be absent
- Reduces memory footprint for large filesystems

### Why two UI modes?

1. **Selector:** List of search results (arrow keys to select)
2. **Navigator:** Browse filesystem hierarchy (Tab to enter, Backspace to exit)

This mirrors Norton Commander's behavior for both search-driven and browse-driven workflows.

## Known Limitations

1. No automated tests - relies on manual verification
2. Windows: No Unicode/wide-char support (ANSI only)
3. History limited to 20 entries (`NCD_HEUR_MAX`)
4. Database refresh only triggered manually or after 24 hours
5. Linux: Drive letter mapping only works for WSL `/mnt/X` pattern
6. Large database files (>2GB) not supported

## Implementation Notes

### Heuristics System

The heuristics system learns user preferences:
- Stores search term → chosen path mappings in `ncd.history`
- Promotes frequently chosen paths to top of match list
- Auto-updates when user selects different path for same search
- Limited to 20 most recent unique searches (LRU eviction)

### Scanner Threading

- One thread per drive/mount
- Main thread displays progress (status line updates every 100ms)
- Uses atomic operations for thread-safe counters
- Configurable inactivity timeout (default: 5 minutes)
- Heap-allocated stack for iterative directory traversal (prevents stack overflow)

### Linux Port Specifics

- Uses `/proc/mounts` to enumerate filesystems
- Filters pseudo-filesystems (proc, sysfs, tmpfs, etc.)
- Maps WSL `/mnt/X` mounts to drive letters for compatibility
- Uses termios raw mode + ANSI sequences for TUI
- Result file uses shell export syntax instead of batch `set`
- Bind mount cycle detection via (device, inode) hash set

### Matcher Algorithm

- Uses FNV-1a hash for fast name lookup
- Hash-based bucket index for O(1) candidate selection
- Chain matching for multi-component paths (parent→child relationships)
- Prefix matching (not substring) to reduce false positives
- Case-insensitive comparison throughout

## File Dependencies

```
ncd.h (base types)
  ├── main.c
  ├── database.h → database.c
  ├── scanner.h → scanner.c
  ├── matcher.h → matcher.c
  ├── ui.h → ui.c
  └── platform.h → platform.c
      └── strbuilder.h → strbuilder.c
```

**Build artifacts:**
- Source: `src/*.c`, `src/*.h`
- Output: `NewChangeDirectory.exe` (Windows) or `NewChangeDirectory` (Linux)
- Required runtime: `ncd.bat` (Windows) or `ncd` (Linux) wrapper script

## Constants Reference

**Limits:**
- `NCD_MAX_NAME` = 256 (max directory name length)
- `NCD_MAX_PATH` = MAX_PATH (platform-defined)
- `NCD_MAX_DRIVES` = 64
- `NCD_MAX_DIRS_PER_DRIVE` = 10,000,000
- `NCD_MAX_TOTAL_DIRS` = 50,000,000
- `NCD_MAX_FILE_SIZE` = 2GB

**Timeouts:**
- `NCD_SCAN_TIMEOUT_MS` = 300000 (5 minutes)
- `NCD_SCAN_INACTIVITY_MS` = 60000 (1 minute)
- `NCD_RESCAN_HOURS` = 24

**UI:**
- `NCD_UI_MAX_LIST_ITEMS` = 20
- `NCD_UI_BOX_MAX_WIDTH` = 120
- `NCD_UI_BOX_MIN_WIDTH` = 40

**Matcher:**
- `NCD_MATCHER_MAX_PARTS` = 32 (max search path components)
- `NCD_MATCHER_INITIAL_CAP` = 16 (initial match array capacity)
