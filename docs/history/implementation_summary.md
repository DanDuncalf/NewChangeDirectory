# NCD Shared Memory Service - Implementation Summary

## Overview

This implementation adds an optional resident state service for NCD that keeps metadata/config and database data hot in memory, while preserving the current standalone execution model.

## Completed Phases

### Phase 0 - Baseline and Branch Preparation ✓
- Created `tasks/todo.md` with checkable implementation items
- Created `tasks/lessons.md` for tracking design decisions
- Established baseline build and test results
- All existing tests pass (7 database tests, 15 matcher tests)

### Phase 1 - State Access Boundary Refactor ✓

**New Files:**
- `src/state_backend.h` - State access abstraction interface
- `src/state_backend_local.c` - Local disk implementation

**Key Design:**
```c
typedef struct NcdStateView NcdStateView;
typedef struct {
    bool from_service;
    uint64_t generation;
    uint64_t db_generation;
} NcdStateSourceInfo;

int state_backend_open_best_effort(NcdStateView **out, NcdStateSourceInfo *info);
int state_backend_open_local(NcdStateView **out, NcdStateSourceInfo *info);
void state_backend_close(NcdStateView *view);

const NcdMetadata *state_view_metadata(const NcdStateView *view);
const NcdDatabase *state_view_database(const NcdStateView *view);
```

**Features:**
- Unified interface for obtaining metadata and database state
- Local implementation loads from disk (existing NCD behavior)
- Service-backed implementation will be added in Phase 6
- Mutation methods for heuristics and metadata updates

### Phase 2 - Shared Snapshot Format Definition ✓

**New Files:**
- `src/shared_state.h` - Snapshot format definitions
- `src/shared_state.c` - Validation and checksum implementation
- `test/test_shared_state.c` - Unit tests

**Snapshot Design:**
- Two separate snapshots: Metadata and Database
- Versioned, checksummed, read-only from client perspective
- Offset-based (no raw pointers in shared memory)
- Generation numbers for cache coherency

**Key Structures:**
```c
typedef struct {
    uint32_t magic;           /* NCD_SHM_META_MAGIC or NCD_SHM_DB_MAGIC */
    uint16_t version;         /* NCD_SHM_VERSION */
    uint16_t flags;
    uint32_t total_size;
    uint32_t header_size;
    uint32_t section_count;
    uint64_t generation;
    uint64_t checksum;        /* CRC64 */
} ShmSnapshotHdr;
```

**Section Types:**
- Config, Groups, Heuristics, Exclusions, Directory History
- Database with per-drive sections
- String pools for compact storage

**Features:**
- CRC64 checksum validation
- Header validation with bounds checking
- Section lookup by type
- Platform-independent format

### Phase 3 - Platform Shared Memory Layer ✓

**New Files:**
- `src/shm_platform.h` - Cross-platform shared memory interface
- `src/shm_platform_win.c` - Windows implementation
- `src/shm_platform_posix.c` - Linux/POSIX implementation

**Interface Design:**
```c
ShmResult shm_create(const char *name, size_t size, ShmHandle **out_handle);
ShmResult shm_open(const char *name, ShmAccess access, ShmHandle **out_handle);
ShmResult shm_map(ShmHandle *handle, ShmAccess access, void **out_addr, size_t *out_size);
ShmResult shm_unmap(void *addr, size_t size);
void shm_close(ShmHandle *handle);
```

**Windows Implementation:**
- Uses `CreateFileMapping` with `INVALID_HANDLE_VALUE`
- Names scoped to user/session via SID
- Functions: `Local\NCD_<SID>_metadata`, `Local\NCD_<SID>_database`

**Linux Implementation:**
- Uses POSIX `shm_open`, `mmap`, `shm_unlink`
- Names scoped to user via UID
- Functions: `/ncd_<uid>_metadata`, `/ncd_<uid>_database`

**Features:**
- Per-user naming prevents collisions
- Read-only and read-write access modes
- Automatic page size alignment
- Error code translation

## Updated Build System

**Modified Files:**
- `build.bat` - Added new source files and advapi32.lib

**New Objects Linked:**
- state_backend_local.obj
- shared_state.obj
- shm_platform_win.obj

## Test Coverage

**Existing Tests (Still Passing):**
- test_database.exe: 7 tests passed
- test_matcher.exe: 15 tests passed

**New Tests:**
- test_shared_state.c: Tests for snapshot validation, CRC64, section lookup

## Architecture Decisions

### Why Two Snapshots?
Metadata changes more frequently than large DB data. Separating them:
- Simplifies invalidation behavior
- Lowers republish cost when heuristics/groups/config change
- Allows independent versioning

### Why Offset-Based Format?
Shared memory must never contain raw heap pointers valid across processes:
- Safety when service crashes
- Multiple clients can map same snapshot
- No address space collisions

### Why Per-User Naming?
Prevents collisions between different users on the same system:
- Windows: Uses user SID
- Linux: Uses user UID

## Remaining Phases (Not Implemented)

### Phase 4 - Control IPC Layer
- Unix domain sockets (Linux)
- Named pipes (Windows)
- Message types: PING, GET_STATE_INFO, SUBMIT_HEURISTIC_UPDATE, etc.

### Phase 5 - Resident Service Skeleton
- `service_main.c` - Service executable entry point
- `service_state.c/h` - Live state management
- `service_publish.c/h` - Snapshot publication with generation swap

### Phase 6 - Client Service Integration
- `state_backend_service.c` - Service-backed state access
- Service detection in main.c
- Shared memory mapping and validation

### Phase 7 - Update Path
- Mutation requests via IPC
- Service applies to private live state
- Snapshot republishing with debounce

### Phase 8 - Lazy Flush
- Dirty flags and debounce timers
- Atomic temp-file-rename persistence
- Separate metadata and DB republish

### Phase 9 - Parity Verification
- Standalone vs service-backed comparison tests
- Output diff for representative commands

### Phase 10 - Packaging
- Service launcher scripts
- Documentation for setup
- Auto-start strategies

## Files Created

```
src/
├── state_backend.h           # State access interface
├── state_backend_local.c     # Local disk implementation
├── shared_state.h            # Snapshot format
├── shared_state.c            # Validation/checksums
├── shm_platform.h            # Shared memory interface
├── shm_platform_win.c        # Windows implementation
└── shm_platform_posix.c      # Linux implementation

test/
└── test_shared_state.c       # Unit tests for snapshot format

tasks/
├── todo.md                   # Implementation tasks
├── lessons.md                # Design decisions
├── baseline.md               # Performance baseline
└── IMPLEMENTATION_SUMMARY.md # This file
```

## Build Verification

```batch
build.bat
```

**Result:** Build successful: NewChangeDirectory.exe

## Usage (Current State)

The implementation is currently in **standalone fallback mode**. The state backend abstraction exists but always uses local disk access. To use:

```c
NcdStateView *view;
NcdStateSourceInfo info;

if (state_backend_open_best_effort(&view, &info) == 0) {
    const NcdMetadata *meta = state_view_metadata(view);
    const NcdDatabase *db = state_view_database(view);
    
    // Use meta and db...
    
    state_backend_close(view);
}
```

When the service is implemented (Phases 4-6), `state_backend_open_best_effort()` will:
1. Try to connect to the service
2. Map shared memory snapshots
3. Fall back to local disk if service unavailable

## Design Principles Preserved

1. **Standalone remains first-class** - If service absent, NCD still works
2. **Client behavior remains authoritative** - Same matcher, TUI, output
3. **Only state residency moves to service** - No service-side matching
4. **Shared-memory format is versioned and self-validating**
5. **No cross-process raw pointers** - offsets only
6. **One writer, many readers** - service only writes snapshots
7. **All current disk formats continue to load**
8. **Minimal code churn in matcher/UI**
