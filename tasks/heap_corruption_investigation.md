# Plan: Find and Fix HEAP_CORRUPTION in NewChangeDirectory.exe

## Problem Statement

NewChangeDirectory.exe crashes with `0xC0000374` (HEAP_CORRUPTION) during multi-drive scans (`ncd -r`). The crash is detected inside `ntdll.dll`, but the root cause is in NCD code. All three recent crash dumps (4/23/2026) show the identical crash at the same address, indicating a deterministic heap corruption bug.

**Crash signature:**
- Exception: `0xC0000374` (HEAP_CORRUPTION)
- Crash address: `0x7ff966d2f489` in `ntdll.dll` (offset `0xFF489`)
- Thread: worker thread during `scan_mounts()`
- Symptom: G: scan shows progress but never reaches COMPLETE; process exits before save loop

---

## Phase 1: Reproduce the Crash Reliably

### 1.1 Set up a reproducible test environment
- [ ] Create a clean test environment with `NCD_TEST_MODE=1` disabled
- [ ] Run `ncd -r` and verify the crash reproduces on the current build
- [ ] If it doesn't crash immediately, try multiple runs (the crash may be timing-dependent)
- [ ] Document the exact drive/mount that triggers the crash (likely G: based on output)

### 1.2 Use Application Verifier to catch the corruption at source
- [ ] Enable Application Verifier for `NewChangeDirectory.exe`:
  ```cmd
  appverif -enable Heaps -for NewChangeDirectory.exe
  ```
- [ ] Run `ncd -r` under AppVerifier
- [ ] The crash should now break **at the exact instruction** that corrupts the heap (not in ntdll)
- [ ] Capture the call stack and the offending source line
- [ ] Disable AppVerifier when done:
  ```cmd
  appverif -disable Heaps -for NewChangeDirectory.exe
  ```

### 1.3 Alternative: Run under WinDbg
- [ ] Launch WinDbg and attach to `NewChangeDirectory.exe`
- [ ] Enable page heap:
  ```
  !gflag +hpa
  ```
- [ ] Run `ncd -r`
- [ ] When the crash hits, capture:
  - Full stack trace (`k`)
  - Heap block info (`!heap -p -a <address>`)
  - Register state (`r`)
  - Source line info if PDBs are available

---

## Phase 2: Identify the Root Cause

### 2.1 Primary suspect areas (multi-threaded scanner code)

The crash occurs during `scan_mounts()` with multiple worker threads. Focus on:

**A. `src/scanner.c` — `worker_thread()` and `platform_scan_directory()`**
- [ ] Check all `malloc`/`free` calls in the scanning path
- [ ] Check for buffer overflows in path construction:
  ```c
  char path_buf[256];
  snprintf(path_buf, sizeof(path_buf), ...)
  ```
- [ ] Verify `ScanFrame` stack usage doesn't overflow `MAX_PATH`
- [ ] Check the `ctx.visited` set (Linux) for double-free or use-after-free

**B. `src/scanner.c` — `scan_mounts()` shared state**
- [ ] `DriveStatus statuses[26]` — accessed by both worker threads and main thread
- [ ] `prev_dir_count[]` and `statuses[].last_active_ms` — race conditions?
- [ ] `platform_atomic_read` / `platform_atomic_exchange` — verify correct usage
- [ ] The `td[]` array is stack-allocated; verify worker threads don't access it after `scan_mounts()` returns

**C. `src/database.c` — `db_add_drive()` and `db_add_dir()`**
- [ ] `db_add_drive()` expands `db->drives` array via `realloc` — is this thread-safe?
  - **CRITICAL:** Worker threads call `db_add_dir()` which may trigger `realloc` of `drv->dirs` and `drv->name_pool`. If `db_add_drive()` also reallocates `db->drives`, other threads may hold stale pointers.
- [ ] `db_add_dir()` string pool growth — check for off-by-one or buffer overflow
- [ ] Verify DirEntry parent indices remain valid during reallocation

**D. `src/platform.c` / `../shared/platform.c` — Windows-specific scanning**
- [ ] `FindFirstFileA` / `FindNextFileA` buffer handling
- [ ] `platform_scan_directory()` recursive frame management
- [ ] Path concatenation with `MAX_PATH` limits
- [ ] Wide-character to ANSI conversion overflow (if applicable)

### 2.2 Common heap corruption patterns to check

| Pattern | Likely Location | Detection Method |
|---------|-----------------|------------------|
| Double-free | `diridset_free()`, `db_free()`, string pool | AppVerifier / code review |
| Buffer overflow | Path construction, name pool | AppVerifier / code review |
| Use-after-free | `db->drives` realloc during scan | Static analysis / debugger |
| Race condition on shared heap | `db_add_dir()` from multiple threads | Thread sanitizer / code review |
| Stack overflow | Deep recursion in `platform_scan_directory()` | Stack trace depth check |
| Uninitialized read | `ScanFrame`, `DriveStatus` | Valgrind / code review |

---

## Phase 3: Develop and Implement the Fix

### 3.1 Fix strategy (dependent on root cause)

**If race condition on `db->drives` / `db_add_drive()`:**
- Pre-allocate all `DriveData` slots before spawning worker threads (already done in `scan_mounts()`)
- Ensure `db_add_dir()` never triggers `db->drives` reallocation during scan
- Add a per-drive mutex/lock for `db_add_dir()` if multiple threads access the same drive

**If race condition on `db_add_dir()` / string pool:**
- Each worker thread should scan into a **local temporary buffer**, then merge into the shared `DriveData` after all threads complete
- This eliminates all shared mutable state during scanning

**If buffer overflow in path handling:**
- Audit all `snprintf` calls with `%s` for unbounded input
- Add explicit truncation checks
- Use `sizeof()` consistently (already partially done)

**If double-free in `diridset` or visited tracking:**
- Add null-after-free pattern
- Use a debug-only reference count

### 3.2 Code changes
- [ ] Make minimal, targeted fix based on Phase 2 findings
- [ ] Ensure fix doesn't regress single-threaded scanning performance
- [ ] Add defensive assertions in debug builds for buffer bounds

---

## Phase 4: Verify the Fix

### 4.1 Unit testing
- [ ] Run existing unit tests: `python test\runner.py unit`
- [ ] Ensure all 785 Windows tests pass
- [ ] Ensure Linux/WSL tests still pass (1103/1124 baseline)

### 4.2 Stress testing
- [ ] Run `ncd -r` 5 times in a row without crash
- [ ] Run with Application Verifier enabled — should produce zero violations
- [ ] Run under WinDbg with page heap — should produce zero breaks

### 4.3 Multi-drive stress test
- [ ] Create a test with many small drives/mounts to stress thread concurrency
- [ ] Verify all drives show COMPLETE and database files are non-empty
- [ ] Verify database files have reasonable sizes (not just 112-byte headers)

### 4.4 Regression check
- [ ] Verify `ncd <search>` still works normally after fix
- [ ] Verify `ncd -r <subdir>` still works
- [ ] Verify service mode still works (if applicable)

---

## Phase 5: Cleanup and Documentation

- [ ] Remove old crash dumps from `%LOCALAPPDATA%\CrashDumps\`
- [ ] Update `AGENTS.md` or relevant docs if the fix changes architecture
- [ ] Add a test case for the specific bug if feasible (e.g., multi-threaded scan stress test)
- [ ] Close this task

---

## Tools Required

| Tool | Purpose | Availability |
|------|---------|--------------|
| Application Verifier (`appverif.exe`) | Catch heap corruption at source | Built into Windows SDK |
| WinDbg | Full dump analysis with symbols | Windows SDK / Store |
| `gflags.exe` | Enable page heap globally | Windows SDK |
| Python script `dump_analyze.py` | Quick minidump triage | Already created |
| Visual Studio Debugger | Live debugging with source | Installed |

---

## Hypothesis Ranking (most → least likely)

1. **Race condition in `db_add_dir()` string pool reallocation** — Multiple worker threads append to the same drive's `name_pool`. If `realloc` moves the buffer, another thread may write to freed memory.
2. **Race condition in `db_add_dir()` `dirs` array reallocation** — Same pattern as #1 but for the `dirs` array.
3. **Buffer overflow in path construction** during deep directory traversal (e.g., `G:\desktop\DESKTOP\GameSpace\...`)
4. **Use-after-free in `platform_scan_directory()`** frame management on Windows
5. **Uninitialized memory read** in `ScanFrame` or `DriveStatus`

**Recommended first step:** Run under Application Verifier. It will almost certainly point directly at the offending instruction.
