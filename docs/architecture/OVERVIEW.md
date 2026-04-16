# NCD Architecture Overview

**NewChangeDirectory (NCD)** - A cross-platform command-line directory navigation tool with optional resident service for fast, fuzzy search-based navigation.

---

## Table of Contents

1. [System Architecture](#system-architecture)
2. [Component Overview](#component-overview)
3. [Data Flow](#data-flow)
4. [Key Design Patterns](#key-design-patterns)
5. [Platform Abstraction](#platform-abstraction)
6. [Build System](#build-system)
7. [Extension Points](#extension-points)

---

## System Architecture

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              NCD CLIENT                                      │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │   CLI       │  │   Matcher   │  │     UI      │  │   Agent     │         │
│  │   Parser    │──│   Engine    │──│  (TUI)      │  │   Mode      │         │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘         │
│         │                │                │                │                │
│         └────────────────┴────────────────┴────────────────┘                │
│                              │                                              │
│                    ┌─────────┴─────────┐                                    │
│                    │  State Backend    │                                    │
│                    │  (Abstraction)    │                                    │
│                    └─────────┬─────────┘                                    │
└──────────────────────────────┼──────────────────────────────────────────────┘
                               │
              ┌────────────────┴────────────────┐
              │                                 │
     ┌────────▼─────────┐            ┌──────────▼──────────┐
     │  Service Mode    │            │  Standalone Mode    │
     │  (Shared Memory) │            │  (Disk I/O)         │
     │                  │            │                     │
     │  ┌────────────┐  │            │  ┌───────────────┐  │
     │  │   IPC      │  │            │  │  Load from    │  │
     │  │  Control   │  │            │  │  Disk (per-   │  │
     │  └────────────┘  │            │  │  drive DBs)   │  │
     │         │        │            │  └───────────────┘  │
     │  ┌──────▼──────┐ │            └─────────────────────┘
     │  │   Shared    │ │
     │  │   Memory    │ │
     │  │  Snapshots  │ │
     │  └─────────────┘ │
     └──────────────────┘
              │
     ┌────────▼─────────┐
     │  NCD SERVICE     │
     │  (Resident)      │
     │                  │
     │  ┌────────────┐  │
     │  │  Scanner   │  │  ←── Multi-threaded directory scan
     │  └────────────┘  │
     │  ┌────────────┐  │
     │  │  State     │  │  ←── In-memory database cache
     │  │  Manager   │  │
     │  └────────────┘  │
     │  ┌────────────┐  │
     │  │ Publisher  │  │  ←── Snapshot publication
     │  └────────────┘  │
     └──────────────────┘
```

### Two Modes of Operation

#### 1. Standalone Mode (Default)
- NCD loads databases directly from disk
- Each execution loads per-drive database files
- No service required - works out of the box
- Slightly slower cold start (~100ms-1s)

#### 2. Service-Backed Mode (Optional)
- Resident service keeps databases hot in memory
- Client connects via IPC, maps shared memory
- Faster cold start (~10-100ms)
- Zero-copy reads from shared memory
- Automatic fallback to standalone if service unavailable

---

## Component Overview

### 1. CLI Module (`src/cli.c`)

**Responsibility:** Parse command-line arguments

```c
typedef struct {
    bool force_rescan;          // /r flag
    bool show_hidden;           // /i flag
    bool show_system;           // /s flag
    bool fuzzy_match;           // /z flag
    char search[NCD_MAX_PATH];  // Search term
    // ... more options
} NcdOptions;
```

**Key Functions:**
- `parse_args()` - Parse standard command-line arguments
- `parse_agent_args()` - Parse /agent subcommands

### 2. Matcher Module (`src/matcher.c`)

**Responsibility:** Search directory database and rank results

**Algorithm:**
1. Chain matching - Exact match on path components
2. Name index lookup - Fast hash-based name search
3. Fuzzy matching - Damerau-Levenshtein distance for typos

**Ranking:** Uses heuristics (frequency of use + recency) to prioritize results

### 3. Database Module (`src/database.c`)

**Responsibility:** Load/save directory databases

**Storage Format:**
- Binary format with magic `NCDB` and CRC64 checksum
- Per-drive files: `ncd_X.database` (Windows), `ncd_XX.database` (Linux)
- String pool for compact storage (DirEntry: 12 bytes)

**Metadata:** Consolidated in `ncd.metadata`:
- Configuration
- Groups/bookmarks
- Heuristics (search history)
- Exclusions
- Directory history

### 4. Scanner Module (`src/scanner.c`)

**Responsibility:** Multi-threaded directory enumeration

**Features:**
- Thread pool for parallel scanning
- Respects hidden/system flags
- Applies exclusion patterns
- Progress callbacks

### 5. UI Module (`src/ui.c`)

**Responsibility:** Interactive terminal interface

**Two UIs:**
1. **Selector** - List of search results with live filtering
2. **Navigator** - Browse filesystem hierarchy

**Input Methods:**
- Console input (Windows: Win32 Console API, Linux: ANSI sequences)
- Keystroke injection (for testing): `NCD_UI_KEYS` env var

### 6. State Backend (`src/state_backend_*.c`)

**Responsibility:** Abstract state access (service vs local)

```c
// Unified interface
int state_backend_open_best_effort(NcdStateView **out, NcdStateSourceInfo *info);
const NcdMetadata *state_view_metadata(const NcdStateView *view);
const NcdDatabase *state_view_database(const NcdStateView *view);
```

**Implementations:**
- `state_backend_local.c` - Load from disk files
- `state_backend_service.c` - Connect to service via IPC

### 7. Service (`src/service_*.c`)

**Responsibility:** Resident state management

**Files:**
- `service_main.c` - Service executable entry point
- `service_state.c` - State machine (STOPPED → STARTING → LOADING → READY)
- `service_publish.c` - Snapshot publication to shared memory

**IPC Protocol:**
- Windows: Named pipes (`\\.\pipe\NCDService`)
- Linux: Unix domain sockets (`/tmp/ncd_service.sock`)

### 8. Shared Memory (`src/shm_platform_*.c`)

**Responsibility:** Cross-platform shared memory implementation

**Platform Mapping:**
- Windows: `CreateFileMapping` / `MapViewOfFile`
- Linux: `shm_open` / `mmap`

**Format:**
- Pointer-free design (offsets only)
- CRC64 checksum validation
- Generation-based versioning

### 9. Agent Mode (`src/main.c` - agent functions)

**Responsibility:** LLM-friendly API for integration

**Commands:**
- `query` - Search with JSON output
- `ls` - List directory contents
- `tree` - Show directory tree
- `check` - Health checks
- `mkdir/mkdirs` - Create directories

---

## Data Flow

### Typical User Session

```
1. User types: ncd downloads

2. CLI Parser (main.c)
   └── Extracts search term "downloads"
   └── Sets default options

3. State Backend
   └── Try service connection (IPC ping)
   └── If available: map shared memory
   └── If unavailable: load from disk

4. Matcher
   └── Search database for "downloads"
   └── Apply fuzzy matching if /z flag
   └── Rank by heuristics (frequent/recent)
   └── Return sorted results

5. Result Handling
   └── Single match: Navigate directly
   └── Multiple matches: Show Selector UI
   └── No matches: Suggest rescan

6. Navigation (ncd.bat wrapper)
   └── NCD writes result to %TEMP%\ncd_result.bat
   └── Wrapper sources result
   └── Changes directory

7. Heuristics Update
   └── Increment search frequency
   └── Update recency timestamp
   └── Save to metadata
```

### Service Lifecycle

```
1. Service Start
   └── Create IPC endpoint (pipe/socket)
   └── Load metadata from disk
   └── Start background loader thread
   └── State: STOPPED → STARTING → LOADING

2. Background Loader
   └── Scan configured drives/mounts
   └── Build in-memory database
   └── Publish snapshot to shared memory
   └── State: LOADING → READY

3. Client Connection
   └── Client sends IPC request
   └── Service responds with generation info
   └── Client maps shared memory

4. Rescan Request
   └── Client sends REQUEST_RESCAN
   └── Service spawns background scanner
   └── Old snapshot remains valid during scan
   └── New snapshot published atomically
   └── Clients detect generation change

5. Service Shutdown
   └── Receive shutdown signal (IPC or Ctrl+C)
   └── Save dirty metadata to disk
   └── Unmap shared memory
   └── Exit
```

---

## Key Design Patterns

### 1. State Abstraction Pattern

All state access goes through `NcdStateView`, allowing seamless switching between:
- Service-backed (fast, shared)
- Local disk (always available)

### 2. Zero-Copy Shared Memory

- Service maintains single copy in shared memory
- Clients map read-only, no data copying
- Generation numbers for cache coherency

### 3. Atomic Updates

Service publishes new snapshots by:
1. Build in private memory
2. Create new shared memory object
3. Copy data
4. Atomically update generation counter

### 4. Fallback Chain

```
Service? ──Yes──→ Connect ──Success──→ Use Service
   │                     │
   No                    Fail
   │                     │
   └───────────→ Load from Disk ──Success──→ Standalone Mode
                        │
                        Fail
                        │
               Show Error / Request Rescan
```

### 5. Platform Abstraction

All platform-specific code isolated:
- `platform_ncd.c` - NCD-specific platform wrappers
- `shm_platform_*.c` - Shared memory per platform
- `control_ipc_*.c` - IPC per platform

### 6. String Pool

Database uses string pool for compact storage:
```c
typedef struct {
    int32_t  parent;       // Parent directory index
    uint32_t name_off;     // Offset into name_pool (not pointer!)
    uint8_t  is_hidden;
    uint8_t  is_system;
    uint8_t  pad[2];
} DirEntry;  // 12 bytes total
```

---

## Platform Abstraction

### Platform Detection

```c
// src/ncd.h
#include "../shared/platform_detect.h"

#if NCD_PLATFORM_WINDOWS
    // Win32 API code
#elif NCD_PLATFORM_LINUX
    // POSIX/Linux code
#endif
```

### Text Encoding

| Platform | Path Encoding | SHM Format |
|----------|---------------|------------|
| Windows | UTF-8 (configurable UTF-16) | UTF-16LE |
| Linux | UTF-8 | UTF-8 |

### Path Handling

| Platform | Separator | Drive Concept |
|----------|-----------|---------------|
| Windows | `\` | Drive letters (C:, D:) |
| Linux | `/` | Mount points (/mnt/c) |

---

## Build System

### Windows
```batch
build.bat          # MSVC build (x64, ARM64)
make               # MinGW build
```

### Linux
```bash
./build.sh         # GCC/Clang build
```

### Output Files
```
NewChangeDirectory.exe    # Main client (Windows)
NCDService.exe            # Service (Windows)
ncd                       # Main client (Linux)
ncd_service               # Service (Linux)
```

### External Dependency
```
../shared/                # Shared platform library
├── platform.c/.h         # Platform detection
├── strbuilder.c/.h       # String building
└── common.c/.h           # Memory utilities
```

---

## Extension Points

### Adding New Agent Commands

1. Add parser in `src/cli.c` `parse_agent_args()`
2. Add handler in `src/main.c` `agent_mode_*()`
3. Update `agent_print_usage()` in `src/main.c`

### Adding New CLI Options

1. Add field to `NcdOptions` in `src/cli.h`
2. Add parser in `parse_args()` in `src/cli.c`
3. Add usage text in `print_usage()` in `src/main.c`

### Adding New IPC Commands

1. Add message type to `src/control_ipc.h`
2. Add client-side handler in `src/state_backend_service.c`
3. Add service-side handler in `src/service_*.c`

### Platform Ports

1. Add platform detection in `../shared/platform_detect.h`
2. Implement `shm_platform_<new>.c`
3. Implement `control_ipc_<new>.c`
4. Update `Makefile` / `build.sh`

---

## Related Documentation

- **AGENTS.md** - Detailed module documentation
- **docs/architecture/shared_memory_multiple_segments.md** - SHM details
- **docs/architecture/shared_memory_pointers_explained.md** - Offset vs pointer
- **test/README.md** - Testing architecture

---

*This document provides a high-level overview. For detailed API documentation, see individual header files and AGENTS.md.*
