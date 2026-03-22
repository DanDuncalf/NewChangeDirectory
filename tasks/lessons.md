# NCD Shared Memory Service - Lessons Learned

## Phase 0 - Baseline

### Build System Notes
- Windows build uses MSVC with `vcvars64.bat` environment setup
- Shared files from `../shared/` are compiled with `sh_` prefix to avoid collisions
- Original project has `src/strbuilder.c` and `src/common.c` that duplicate shared files
- Build script needed `advapi32.lib` for Windows SID functions

### Test Results
- All 7 database tests pass
- All 15 matcher tests pass
- No regressions introduced by adding new files

---

## Phase 1 - State Access Boundary

### Design Decision: Unified Interface
Creating `state_backend.h` with `NcdStateView` abstraction allows main.c to remain mostly unchanged while enabling the service path. The interface is intentionally narrow:

```c
int state_backend_open_best_effort(NcdStateView **out, NcdStateSourceInfo *info);
const NcdMetadata *state_view_metadata(const NcdStateView *view);
const NcdDatabase *state_view_database(const NcdStateView *view);
```

### Memory Management
- `ncd_malloc`, `ncd_calloc` exist but `free()` is used directly for deallocation
- State backend follows same pattern: use wrappers for allocation, standard free for deallocation

### Integration Pattern
The local implementation (`state_backend_local.c`) demonstrates how to:
1. Load all per-drive databases and merge them
2. Manage metadata lifecycle with dirty tracking
3. Provide fallback when individual drives fail to load

### Key Insight: Service-First Design
`state_backend_open_best_effort()` currently falls back to local immediately (since service isn't implemented). When Phase 6 adds service support, the integration point is already in place.

---

## Phase 2 - Shared Snapshot Format

### Why Two Snapshots?
Separated metadata and database snapshots because:
- Metadata changes more frequently (heuristics, config, groups)
- Large DB shouldn't be republished for small metadata changes
- Different lifecycles: DB changes on rescan, metadata changes on user actions

### Offset-Based Design
All shared memory structures use offsets instead of pointers:
```c
typedef struct {
    int32_t  parent;
    uint32_t name_off;    /* Offset into pool, not pointer */
} ShmDirEntry;
```

This ensures:
- Valid across different process address spaces
- Safe if service crashes
- No ASLR issues

### CRC64 Implementation
Used CRC64-ECMA polynomial (0x42F0E1EBA9EA3693):
- Consistent with existing database format's CRC64
- Precomputed lookup table for performance
- Incremental update support for streaming computation

### Section Layout
Each snapshot has a header followed by section descriptors:
```
[ShmSnapshotHdr: 32 bytes]
[ShmSectionDesc * section_count]
[Padding to 8-byte alignment]
[Section data...]
```

This allows efficient lookup by type without parsing entire structure.

---

## Phase 3 - Platform Shared Memory

### Windows Implementation
Used `CreateFileMapping` with `INVALID_HANDLE_VALUE`:
- Creates unnamed file mapping backed by paging file
- Name lives in session namespace (`Local\` prefix)
- Requires `advapi32.lib` for SID conversion functions

### Linux Implementation
Used POSIX shared memory:
- `shm_open` with `O_CREAT | O_EXCL` for atomic create
- Names must start with `/` but contain no other `/`
- Automatic cleanup on system reboot (unlike files)

### Naming Strategy
Per-user naming prevents collisions:
- Windows: `Local\NCD_<SID>_<base>` using user SID
- Linux: `/ncd_<uid>_<base>` using user ID

Without this, different users on same system would collide.

### Size Discovery
On Windows, getting the size of an existing mapping requires:
1. Map it first with `MapViewOfFile`
2. Call `VirtualQuery` to get region size
3. Store size in handle structure for later use

On Linux, `fstat()` on the file descriptor provides size directly.

---

## Integration Considerations

### main.c Refactoring Needed
Current main.c loads metadata and database separately at multiple points. To fully use state_backend:

1. At startup: Open state view once
2. Pass view down to functions needing state
3. Close view before exit

This would consolidate ~15 separate load calls into one.

### Mutation Path
The `state_backend_submit_*` functions provide unified mutation:
- Local mode: Apply directly and save
- Service mode (future): Send via IPC

This keeps main.c unchanged whether service is active or not.

---

## Remaining Challenges

### Phase 4: IPC Design Decisions
Need to decide:
- Message serialization format (binary structs? JSON?)
- Synchronous vs asynchronous requests
- Timeout handling for service unresponsiveness
- Unix socket path (XDG_RUNTIME_DIR on Linux)

### Phase 5: Service Architecture
Key decisions:
- Single-threaded with select/poll or multi-threaded?
- In-process state representation (can use pointers)
- Snapshot rebuild scheduling (immediate vs debounced)
- Signal handling for clean shutdown

### Phase 6: Client Integration
Challenge: Existing code expects modifiable NcdMetadata/NcdDatabase.
Options:
1. Copy from shared to heap on open (simpler, uses more memory)
2. Use const pointers with cast for modifications (risky)
3. Create read-only view types (more refactoring)

Recommended: Option 1 - copy for now, optimize later if needed.

### Phase 7-8: Persistence
- Dirty flag tracking per section
- Debounce timer implementation (platform-specific)
- Atomic save with temp-file-then-rename
- Recovery if save fails mid-operation

---

## Performance Expectations

### Service-Backed Mode Benefits
- No disk I/O on startup (fast warm start)
- Shared memory stays resident across client invocations
- Zero-copy read access via mmap

### Overheads
- IPC round-trip for mutations (~1-2ms local)
- Snapshot validation on map (CRC64 of potentially large data)
- Generation check on each access (negligible)

### When Service Helps Most
- Large databases (100K+ directories)
- Frequent invocations (many cd commands)
- Slow storage (network drives, spinning disks)

### When Service Doesn't Help
- First invocation (service needs to load from disk)
- Infrequent use (service may exit/idle)
- Small databases (disk cache handles it)

---

## Testing Strategy

### Unit Tests Added
- `test_shared_state.c`: Header validation, CRC64, section lookup

### Tests Needed (Future Phases)
- IPC message serialization round-trip
- Shared memory create/map/unmap on both platforms
- Service start/stop lifecycle
- Parity: service vs standalone output comparison
- Failure injection: corrupted snapshots, missing sections

### Performance Tests
- Cold start latency (service vs standalone)
- Warm query latency (service vs standalone)
- Memory usage (service process)
- Snapshot rebuild time vs database size

---

## Code Style Notes

### Naming Conventions (followed project style)
- Functions: `module_verb_noun()` (e.g., `shm_platform_init`)
- Types: PascalCase with suffix (e.g., `ShmHandle`, `NcdStateView`)
- Constants: `NCD_UPPER_CASE` (e.g., `NCD_SHM_VERSION`)

### Platform Abstraction
Used `#if NCD_PLATFORM_WINDOWS` pattern from existing code.
Separate files for platform implementations:
- `shm_platform_win.c`
- `shm_platform_posix.c`

### Error Handling
- Return codes for recoverable errors
- errno/GetLastError preservation
- Static error string buffer (not thread-safe, matches project style)

---

## Future Extensions

### Possible Enhancements
1. **Compression**: Large snapshots could be compressed before shared
2. **Incremental updates**: Delta encoding for small changes
3. **Multiple clients**: Reader-writer locks for safe concurrent access
4. **Network distribution**: Extend IPC to TCP for remote service
5. **Hot reload**: Watchdog for config file changes

### Not Doing (Out of Scope)
- Network transport (stated as non-goal)
- Service-side matching (stated as non-goal)
- C++ rewrite (stated as non-goal)
- Breaking disk format changes
