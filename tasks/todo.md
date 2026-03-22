# NCD Shared Memory Service - Implementation Tasks

## ✅ ALL PHASES COMPLETE

---

## Phase 0 - Baseline and Branch Preparation ✅

- [x] Create `tasks/todo.md` with checkable items
- [x] Create `tasks/lessons.md`
- [x] Build current project on Windows toolchain
- [x] Run current tests to establish baseline
- [x] Capture startup timings for representative scenarios
- [x] Record baseline results in `tasks/baseline.md`

**Exit Criteria:**
- [x] Current tree builds cleanly
- [x] Existing tests pass or known failures are documented
- [x] Baseline timings captured

---

## Phase 1 - State Access Boundary Refactor ✅

**New Files:**
- [x] `src/state_backend.h` - Interface definition
- [x] `src/state_backend_local.c` - Local disk implementation

**Exit Criteria:**
- [x] NCD builds
- [x] Behavior is unchanged
- [x] Existing tests still pass

---

## Phase 2 - Shared Snapshot Format Definition ✅

**New Files:**
- [x] `src/shared_state.h` - Snapshot format definitions
- [x] `src/shared_state.c` - Validation and checksum implementation
- [x] `test/test_shared_state.c` - Unit tests

**Exit Criteria:**
- [x] Snapshot structs defined
- [x] Unit tests validate header parsing and bounds checking
- [x] No client integration yet

---

## Phase 3 - Platform Shared Memory Layer ✅

**New Files:**
- [x] `src/shm_platform.h` - Platform abstraction interface
- [x] `src/shm_platform_win.c` - Windows implementation
- [x] `src/shm_platform_posix.c` - Linux/POSIX implementation

**Exit Criteria:**
- [x] Small test can create, map, read, unmap shared memory on both platforms
- [x] Name generation is deterministic and user-scoped

---

## Phase 4 - Control IPC Layer ✅

**New Files:**
- [x] `src/control_ipc.h` - Message enums and payload structs
- [x] `src/control_ipc_win.c` - Named pipe implementation
- [x] `src/control_ipc_posix.c` - Unix socket implementation

**Exit Criteria:**
- [x] Client can detect if service exists
- [x] Client can request current generation + mapping names
- [x] Service can receive a trivial mutation message

---

## Phase 5 - Resident Service Skeleton ✅

**New Files:**
- [x] `src/service_main.c` - Service executable entry point
- [x] `src/service_state.c/h` - Live state management
- [x] `src/service_publish.c/h` - Snapshot publication

**Exit Criteria:**
- [x] Service starts
- [x] Service publishes valid snapshots
- [x] Client can detect service and map snapshots

---

## Phase 6 - Client Read-Only Shared-State Integration ✅

**New Files:**
- [x] `src/state_backend_service.c` - Service-backed implementation

**Exit Criteria:**
- [x] `ncd <query>` works with service running
- [x] Same query works with service absent
- [x] Result ordering and TUI behavior match baseline

---

## Phase 7 - Update Path Back to Service ✅

**Implementation:**
- [x] `ipc_client_submit_heuristic()` - Submit heuristic updates
- [x] `ipc_client_submit_metadata()` - Submit metadata changes
- [x] `ipc_client_request_rescan()` - Request rescan
- [x] `ipc_client_request_flush()` - Request flush

**Exit Criteria:**
- [x] Picking a path updates heuristics via service
- [x] Metadata-changing commands work through service
- [x] New snapshot generation visible to subsequent clients

---

## Phase 8 - Lazy Flush and Snapshot Republishing ✅

**Implementation:**
- [x] Dirty flags in service_state.c
- [x] Debounce timers in service_main.c
- [x] Atomic temp-file-rename persistence
- [x] Separate metadata and DB republish

**Exit Criteria:**
- [x] Updates survive service restart
- [x] No partial/corrupt on-disk writes
- [x] Snapshot generations advance predictably

---

## Phase 9 - Standalone / Service Parity Verification ✅

**New Files:**
- [x] `test/test_service_parity.c` - Parity tests

**Exit Criteria:**
- [x] Output parity demonstrated
- [x] Any intentional differences documented

---

## Phase 10 - Packaging and Deployment ✅

**New Files:**
- [x] `ncd_service.bat` - Windows service launcher
- [x] `ncd_service` - Linux service launcher
- [x] Build script updated for service executable

**Exit Criteria:**
- [x] User can run NCD with or without service
- [x] Setup docs exist for both platforms

---

## Final Acceptance Criteria ✅

- [x] Running `ncd <query>` with no service behaves as before
- [x] Running `ncd <query>` with service active avoids reloading metadata/database from disk
- [x] Matching, UI, agent mode remain client-side and match standalone
- [x] Heuristic and metadata updates flow back to service and persist
- [x] Service publication is safe, versioned, validated, pointer-free
- [x] Both Linux and Windows implementations work with the same high-level design
- [x] Tests and timing data prove correctness and value

---

## Build Verification

```batch
> build.bat
Build successful: NewChangeDirectory.exe and NCDService.exe

> test\test_database.exe
Tests: 7 run, 7 passed, 0 failed

> test\test_matcher.exe  
Tests: 15 run, 15 passed, 0 failed
```

---

## Files Created

**Source (16 files):**
- src/state_backend.h
- src/state_backend_local.c
- src/state_backend_service.c
- src/shared_state.h
- src/shared_state.c
- src/shm_platform.h
- src/shm_platform_win.c
- src/shm_platform_posix.c
- src/control_ipc.h
- src/control_ipc_win.c
- src/control_ipc_posix.c
- src/service_state.h
- src/service_state.c
- src/service_publish.h
- src/service_publish.c
- src/service_main.c

**Tests (2 files):**
- test/test_shared_state.c
- test/test_service_parity.c

**Deployment (2 files):**
- ncd_service.bat
- ncd_service

**Documentation (4 files):**
- tasks/todo.md
- tasks/lessons.md
- tasks/baseline.md
- tasks/FINAL_SUMMARY.md
