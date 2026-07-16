# Plan: Rescan Progress - Local Callback + IPC Streaming

## Context

NCD's rescan feature (`ncd -r`) has two modes:
1. **Subdirectory rescan** (`ncd -r C:\foo`) - scans a specific directory
2. **Full drive rescan** (`ncd -r`) - scans entire drives

**Current Problems:**
1. Subdirectory rescans (`scan_subdirectory()`) do NOT support dynamic progress - no callback parameter
2. When a client delegates to the service, there's no streaming progress - client just gets "request sent"
3. No `--silent` option to suppress progress output

**Desired Behavior:**
- Standalone client: Use progress callback directly (shows dynamic progress)
- Client with service: Stream progress via IPC (shows dynamic progress from service)
- `--silent` flag: Suppress all progress output

## Goals

1. Add `--silent` option to suppress rescan progress
2. Add progress callback to `scan_subdirectory()` 
3. Add IPC streaming progress from service to client
4. Add comprehensive tests for both paths
5. Ensure ALL tests pass (0 failures, 0 skips)

---

## Superpowers Checklist

- [x] **No voodoo/unintuitive moves** - Each change has clear rationale
- [x] **No dead ends** - Every code path leads to a working feature
- [x] **Minimal blast radius** - Changes isolated to rescan/scan subsystems
- [x] **Symmetry preserved** - Both standalone and service modes work
- [x] **No special cases without tests** - Every path has test coverage
- [x] **Tests are integration tests** - Test real behavior, not mocked internals

---

## Implementation Tasks

### Task 1: Add `--silent` CLI Option

**Files:** `src/cli.c`, `src/ncd.h`

**Changes:**
1. Add `bool silent` field to `NcdOptions` struct in `ncd.h`
2. Add `--silent` parsing in `cli.c`
3. Propagate `silent` flag through rescan code paths

**Tests:** (covered by existing CLI parse tests + new integration tests)

---

### Task 2: Add Progress Callback to `scan_subdirectory()`

**Files:** `src/scanner.h`, `src/scanner.c`, `src/main.c`

**Changes:**
1. Update `scan_subdirectory()` signature to include `ScanProgressFn progress_fn, void *user_data` parameters
2. Implement progress callback invocation in `platform_scan_directory()` during subdirectory scan
3. Update `run_requested_rescan()` in `main.c` to pass progress callback when `!opts.silent`
4. Create `rescan_progress_callback()` that writes progress to console

**Tests:**
- `test_scanner_subdir_progress.exe` - Test subdirectory scan with progress callback
- Integration test: `ncd -r <dir>` shows progress

---

### Task 3: Add IPC Streaming Progress

**Files:** `src/control_ipc.h`, `src/control_ipc_common.c`, `src/control_ipc_win.c`, `src/control_ipc_posix.c`, `src/service_main.c`, `src/state_backend_service.c`

**Changes:**

#### 3.1 Add Progress Message Types

```c
// control_ipc.h - Add new message types
NCD_MSG_RESCAN_PROGRESS = 0x100,  // Service -> Client progress update
NCD_MSG_RESCAN_COMPLETE = 0x101,  // Service -> Client completion

// NcdRescanProgressPayload
typedef struct {
    char  drive_letter;
    uint8_t pad[3];
    uint32_t dir_count;
    char current_path[256];
} NcdRescanProgressPayload;
```

#### 3.2 Client-Side: Stream Progress

```c
// state_backend_service.c - Add streaming function
int state_backend_request_rescan_with_progress(
    NcdStateView *view,
    const bool drive_mask[26],
    bool scan_root_only,
    ScanProgressFn progress_fn,
    void *user_data);
```

The client opens a dedicated IPC channel for progress streaming and waits for `NCD_MSG_RESCAN_PROGRESS` messages.

#### 3.3 Service-Side: Publish Progress

```c
// service_main.c - Add progress publisher
// During perform_rescan(), periodically publish progress to connected clients
// Use g_pending_progress queue (existing IPC connection needed)
```

**Service Main Loop Changes:**
- Store client connection reference when `handle_request_rescan()` is called
- During `perform_rescan()`, every ~100ms publish progress to client
- On completion, send `NCD_MSG_RESCAN_COMPLETE`

#### 3.4 Client Main Loop Changes

```c
// main.c - Polling loop for rescan progress
// After state_backend_request_rescan_with_progress() returns
// Poll for progress messages until RESCAN_COMPLETE
// Display progress using same callback mechanism as local scans
```

**Tests:**
- `test_ipc_rescan_progress.exe` - Test IPC progress streaming
- Integration test: Service rescan shows live progress in client

---

### Task 4: Wire Everything Together

**Files:** `src/main.c`

**Changes:**

1. When `opts.force_rescan && opts.has_search` (rescan with search):
   - Check if service available via `g_state_info.from_service`
   - If service: call `state_backend_request_rescan_with_progress()`
   - If standalone: call `run_requested_rescan()` with progress callback

2. Respect `opts.silent` flag:
   - If `silent`: pass NULL callback (no progress)
   - If not silent: pass actual callback

---

### Task 5: Add Comprehensive Tests

**Test Files:** `test/test_scanner_subdir.c`, `test/test_ipc_rescan_progress.c`

**Test Scenarios:**

#### Subdirectory Progress (Standalone)
1. Create test directory tree with known structure
2. Call `scan_subdirectory()` with progress callback
3. Verify callback invoked with correct paths
4. Verify `--silent` suppresses output

#### IPC Progress Streaming
1. Start service
2. Client requests rescan with progress callback
3. Verify progress messages received
4. Verify final completion message
5. Cleanup

**Integration Test Coverage:**
- `ncd -r <path>` with progress (standalone)
- `ncd -r` with progress (service)
- `ncd -r --silent` suppresses output
- `ncd -r <path> --silent` suppresses output

---

### Task 6: Update Python Test Runner

**File:** `test/runner.py`

**Changes:**
1. Add new test executables to test list
2. Ensure `generate_report.py` includes new tests
3. Add `--repair` handling for new test environment dependencies

---

## File Manifest

### Modified Files

| File | Changes |
|------|---------|
| `src/ncd.h` | Add `bool silent` to `NcdOptions` |
| `src/cli.c` | Parse `--silent` flag |
| `src/scanner.h` | Add callback params to `scan_subdirectory()` |
| `src/scanner.c` | Implement callback in `platform_scan_directory()` |
| `src/control_ipc.h` | Add `NCD_MSG_RESCAN_PROGRESS`, payload struct |
| `src/control_ipc_common.c` | Add progress message handling |
| `src/control_ipc_win.c` | Implement progress streaming (Windows) |
| `src/control_ipc_posix.c` | Implement progress streaming (POSIX) |
| `src/service_main.c` | Publish progress during `perform_rescan()` |
| `src/state_backend_service.c` | Add `state_backend_request_rescan_with_progress()` |
| `src/main.c` | Wire progress, respect `--silent` |
| `test/runner.py` | Add new tests to runner |

### New Files

| File | Purpose |
|------|---------|
| `test/test_scanner_subdir_progress.c` | Subdirectory progress test |
| `test/test_ipc_rescan_progress.c` | IPC streaming progress test |

---

## Verification Checklist

- [ ] `scan_subdirectory()` accepts progress callback
- [ ] `--silent` flag suppresses all rescan output
- [ ] Standalone rescan shows dynamic progress
- [ ] Service rescan streams progress to client
- [ ] Both Windows and POSIX builds succeed
- [ ] All existing tests pass (0 failures, 0 skips)
- [ ] New tests pass
- [ ] `python test/runner.py` shows 0 failures, 0 skips

---

## Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| IPC complexity | Use existing connection, add new message type only |
| Progress callback threading | Use existing `DriveStatus` pattern (already thread-safe) |
| Service crash during progress | Client timeout returns gracefully |
| Performance impact | Progress updates throttled to ~200ms intervals (5 fps) |

---

## Success Criteria

1. User types `ncd -r C:\projects` → sees live progress like `ncd -r`
2. User types `ncd -r --silent` → no progress output
3. User with service: `ncd -r` → progress streams from service to client
4. All tests pass: `python test/runner.py` → 0 failures, 0 skips
