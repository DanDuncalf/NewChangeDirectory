# Staff Engineering Code Quality Review Plan

Date: 2026-05-12

Scope reviewed:
- `graphify-out/GRAPH_REPORT.md` was ingested first, per project rules.
- Markdown docs were reviewed for architecture, testing, and historical context.
- Source review covered `src/`, `test/` context where relevant, and sibling shared code in `../shared`.
- The local `graphify` CLI was not available in this environment, so graph follow-up used the generated report plus targeted source inspection.

Goal:
- Preserve the current green build/test state while tightening correctness, platform boundaries, DRYness, safety, and long-term maintainability.
- Keep platform-specific code small and explicit. Large `#if NCD_PLATFORM_WINDOWS` / `#elif NCD_PLATFORM_LINUX` bodies should move behind narrow platform APIs in `platform.h` or `../shared/platform.h`.
- Favor simple, reviewable commercial-quality code over broad rewrites.

## P0 - Correctness and Safety

### 1. Validate IPC payloads before dispatch or queueing

Files:
- `src/service_main.c`
- `src/control_ipc.h`

Finding:
- `handle_client_connection()` only checks that some metadata and heuristic payloads are at least the fixed header size before dispatching.
- `handle_submit_heuristic()` treats variable-length payload slices as NUL-terminated strings.
- `apply_metadata_update()` casts payloads to structures, strings, and integers without complete `data_len` validation.
- `try_queue_mutation()` allocates and copies based on embedded lengths before proving that the full payload is present.

Risk:
- A malformed local IPC client can trigger out-of-bounds reads, huge allocations, invalid string logging, or service crashes.

Plan:
- Add one validator per IPC payload type, located beside the protocol definitions or in a small `service_ipc_validate.c` helper.
- Use checked addition for `sizeof(header) + declared_len` calculations.
- Require NUL termination for string payloads when code later uses C string APIs.
- Reject unknown or malformed payloads before queueing background mutations.
- Cap variable payload sizes to protocol-level maxima.

Tests:
- Add malformed IPC unit tests for truncated heuristic strings, truncated metadata updates, oversized declared lengths, and fixed-size integer payloads with too-small buffers.
- Add one service integration test that sends malformed local requests and verifies the service remains alive.

### 2. Fix matcher name-index correctness and concurrency

Files:
- `src/matcher.c`

Finding:
- `matcher_find()` first searches an exact leaf-name hash bucket and only falls back to full scanning if the bucket produces zero candidates.
- Prefix or wildcard-compatible matches can be omitted when at least one exact leaf-name match exists.
- Name-index rebuild/install uses a mutex for pointer replacement, but readers can keep using an index that another thread frees during a competing rebuild.

Risk:
- Query results can be incomplete.
- Concurrent service/client search can use freed matcher index memory.

Plan:
- Use the name index only for exact query mode, or use it as a fast seed followed by a full prefix/glob-compatible path when the pattern requires it.
- Deduplicate exact and prefix results with a small visited set or result-id marker.
- Make index lifetime safe by either:
  - holding the matcher lock for the full indexed lookup,
  - adding refcount/RW lock ownership,
  - or never freeing a swapped-out index until database teardown.
- If another thread wins an install race, free only the local uninstalled index, not the published one.

Tests:
- Add a test database with names such as `src`, `src_core`, and `src-tools`; verify prefix searches return all expected candidates.
- Add a concurrent matcher stress test that repeatedly invalidates/rebuilds the index while queries run.

### 3. Remove unconditional production debug file writes

Files:
- `src/main.c`
- `src/database.c`

Finding:
- Production paths write hard-coded Windows debug files such as `C:\ncd_mv_debug.txt` and `C:\ncd_load_debug.txt`.

Risk:
- Unexpected filesystem side effects, permission failures, AV/security alerts, and unnecessary I/O in normal use.
- Windows-only paths leak into cross-platform code.

Plan:
- Remove these writes outright if they are obsolete.
- If still needed, route them through the service logging path or a compile-time/debug-only logging helper.
- Ensure release builds perform no unsolicited writes outside documented NCD state paths.

Tests:
- Add a smoke test around move/load paths that asserts no debug files are created in test mode.

### 4. Make recursive deletion symlink/reparse-point safe

Files:
- `../shared/platform.c`
- `src/main.c`

Finding:
- `platform_remove_tree()` uses `stat()` on POSIX, which follows symlinks to directories.
- The Windows branch recurses into directories without guarding against reparse points.
- `path_join()` return values are ignored in recursive deletion and dry-run collection paths.

Risk:
- `--agent:rmdirs --force` can escape the requested tree through symlinks or junctions.
- Long child paths can be truncated and then acted on incorrectly.

Plan:
- On POSIX, use `lstat()` and unlink symlinks rather than recursing into symlink targets.
- On Windows, treat `FILE_ATTRIBUTE_REPARSE_POINT` as a link boundary and delete the link itself, not the target tree.
- Check every `path_join()` return. On truncation, abort that branch and report a path-length error.
- Mirror the same behavior in `rmdirs` dry-run collection so preview and execution agree.

Tests:
- Add POSIX/WSL tests for directory symlinks pointing outside the deletion root.
- Add Windows tests for junction or symlink behavior when available.
- Add a long-path test that verifies truncation is reported and nothing unexpected is removed.

### 5. Make mkdirs strict and remove `--force`

Files:
- `src/main.c`

Finding:
- `--agent:mkdirs --force` can remove and recreate a pre-existing empty directory.
- In atomic mode, that directory can be marked as newly created and removed during rollback.
- This makes `mkdirs` less predictable than a tree creation command should be.

Risk:
- Atomic all-or-nothing behavior is violated. A failed nested create can delete user state that existed before the command.
- The `--force` meaning is weak for `mkdirs`: it only recreates empty directories and still fails on non-empty directories.

Plan:
- Treat `mkdirs` as a strict create operation.
- Fail with `error_exists` if any requested directory already exists.
- Reject `--force` for `mkdirs` with a clear error message.
- Keep `--force` for `mkdir`, `rmdir`, `rmdirs`, and `mv`, where the flag has clearer semantics.

Tests:
- Add tests for existing directories in atomic, non-atomic, and dry-run modes.
- Add a test that `mkdirs --force` fails with an unsupported-option message.

### 6. Fix Windows control IPC broken-pipe success handling

Files:
- `src/control_ipc_win.c`

Finding:
- A broken pipe during response polling returns `NCD_IPC_OK` for any request type before the later request-type-specific check.

Risk:
- Ping, state, or status requests can appear successful when the service died or disconnected.

Plan:
- Return success on broken pipe only for `NCD_MSG_REQUEST_SHUTDOWN`.
- Map all other broken-pipe cases through the normal IPC error path.

Tests:
- Add a Windows IPC unit/integration test where the service disconnects before sending a response to non-shutdown requests.

### 7. Close shared-memory publication/name-consumption gaps

Files:
- `src/service_publish.c`
- `src/state_backend_service.c`
- `src/shared_state.h`
- `src/shm_types.h`

Finding:
- Snapshot publication creates a `.new` object, removes the canonical object, then creates/copies into the canonical object. Clients open the canonical object, so a gap remains despite comments claiming no gap window.
- `state_backend_service.c` receives advertised shared-memory names from IPC, but recomputes names locally instead of opening `state_info.meta_name` and `state_info.db_name`.
- Several shared-memory constants are compatibility-defined in more than one header.

Risk:
- Clients can fail to open state during publication races.
- Versioned or generation-specific shared-memory names cannot be introduced cleanly while clients ignore advertised names.
- Duplicated protocol constants increase compatibility drift risk.

Plan:
- Update service-backed state opening to trust advertised shared-memory names from `NcdIpcStateInfo`.
- Replace canonical delete/recreate publication with a stable indirection model:
  - either a small control segment that advertises the active versioned names,
  - or a stable double-buffered canonical segment with generation and complete flags.
- Consolidate shared-memory protocol constants into one canonical header and keep compatibility aliases in one narrow place.

Tests:
- Add a publication-race test where clients repeatedly open service state during rescan publication.
- Add a test proving clients can open non-canonical/versioned shared-memory names advertised by IPC.

### 8. Make Linux service-backed drive letters match local behavior

Files:
- `src/state_backend_service.c`
- `src/shm_types.h`

Finding:
- Linux service-backed database reconstruction sets `drv->letter` from `mount_point[0]`, which is usually `'/'`.

Risk:
- Service-backed query JSON and drive-based logic can diverge from standalone/local mode.

Plan:
- Add drive-letter/drive-id to the shared-memory mount entry, or derive it through the same platform helper used by local mode.
- Add parity tests between service-backed and standalone mode for Linux/WSL mount results.

Tests:
- Extend service parity tests to compare `drive` fields in JSON query output on Linux/WSL.

## P1 - Platform Boundaries and DRY Cleanup

### 9. Introduce a small cross-platform directory iterator API

Files:
- `../shared/platform.h`
- `../shared/platform.c`
- `src/main.c`
- `src/scanner.c`
- `src/ui.c`

Finding:
- `main.c`, `scanner.c`, and `ui.c` each contain direct Windows/POSIX directory enumeration branches.
- Some large functions are effectively one Windows implementation plus one Linux implementation.

Plan:
- Add a narrow API such as:
  - `platform_dir_open(path, flags)`
  - `platform_dir_next(iterator, entry)`
  - `platform_dir_close(iterator)`
  - entry fields for name, type, symlink/reparse status, hidden/system status, and error.
- Use it in agent `ls`, recursive `tree`, `rmdirs`, scanner traversal, and navigator listing.
- Keep policy decisions outside the iterator. For example, scanner exclusions and UI sorting remain in their modules.

Tests:
- Add shared platform iterator tests on Windows and Linux/WSL.
- Add parity tests that `agent:ls` and navigator-visible entries handle hidden files, symlinks, and permission errors consistently.

### 10. Shrink UI platform-specific console blocks

Files:
- `src/ui.c`
- `../shared/platform.h`
- `../shared/platform.c`

Finding:
- `ui.c` has a large Windows console implementation and a large Linux terminal implementation in one file.
- `con_write_padded()` uses fixed 512-byte buffers and is only safe because current callers mostly pass small widths.

Plan:
- Move raw console operations behind `PlatformConsoleOps` or equivalent helper functions.
- Keep selector/navigator rendering logic platform-neutral.
- Clamp buffer widths before writing, or use a dynamic scratch buffer for padded writes.

Tests:
- Add unit tests for width clamping and ANSI/Win32 text padding behavior where feasible.
- Keep existing injected-key TUI tests as regression coverage.

### 11. Table-drive CLI and agent option parsing

Files:
- `src/cli.c`

Finding:
- `parse_agent_args()` is very large and has repeated option parsing.
- Known long-option tables are duplicated.

Plan:
- Create one canonical option table with spelling, arity, command applicability, and output field mapping.
- Split command parsing into small handlers for query, ls, tree, mkdir, mkdirs, rmdir/rmdirs, mv, ln, verify, chmod, check, and service controls.
- Preserve exact existing CLI behavior and error text unless tests explicitly update it.

Tests:
- Add parser contract tests for each command's accepted flags, rejected flags, missing arguments, and duplicate flags.

### 12. Remove brittle path-map type duplication

Files:
- `src/main.c`
- `src/database.c`
- `src/database.h`

Finding:
- `PathMap` layout is duplicated in `main.c` and `database.c` so database cleanup can free an opaque cache.
- `main.c` also contains an unused `path_map_free()` copy.

Plan:
- Move path-map ownership into one module.
- Expose only creation, invalidation, lookup, and destroy functions.
- Delete unused duplicate cleanup helpers.

Tests:
- Existing database path-map tests should continue to pass.
- Add a leak-oriented path-map lifecycle test if one does not already exist.

### 13. Consolidate snapshot publication helpers

Files:
- `src/service_publish.c`

Finding:
- Metadata and database snapshot publication have similar create/copy/remap logic.

Plan:
- Extract a shared helper for "publish buffer into snapshot object" once the publication race design is fixed.
- Keep metadata/db-specific validation and statistics outside the helper.

Tests:
- Existing shared-state validation tests plus the new publication-race test.

### 14. Consolidate shared-memory and platform compatibility macros

Files:
- `src/shared_state.h`
- `src/shm_types.h`
- `src/ncd.h`
- `../shared/platform.h`

Finding:
- Shared-memory constants and some platform compatibility macros are repeated behind compatibility guards.

Plan:
- Choose one canonical protocol header for shared-memory constants.
- Keep legacy aliases in one compatibility section with comments explaining removal criteria.
- Move general platform compatibility into `../shared/platform.h`; keep NCD-specific policy in `src/platform_ncd.*`.

Tests:
- Compile-only coverage on Windows and Linux/WSL is the main guard.

## P2 - Performance, Maintainability, and Polish

### 15. Decompose large functions along behavioral boundaries

Files:
- `src/main.c`
- `src/service_main.c`
- `src/state_backend_service.c`
- `src/cli.c`
- `src/scanner.c`

Finding:
- Several functions are 200 to 460 lines and mix parsing, filesystem work, JSON generation, rollback, service lifecycle, and cleanup.

Plan:
- Split only where it reduces real cognitive load:
  - parse/validate options,
  - prepare work items,
  - execute platform operation,
  - update database/state,
  - serialize result.
- Prefer `goto fail` cleanup in C functions with many allocations and handles.
- Avoid introducing abstractions that only rename existing code.

Tests:
- Run targeted unit tests after each extraction.
- Run the full suite after any extraction that touches shared behavior.

### 16. Keep non-O(N) paths intentionally indexed or bounded

Files:
- `src/matcher.c`
- `src/database.c`
- `src/main.c`

Finding:
- Search paths already use indexing in places, but fallback behavior and cache ownership are brittle.
- Some agent output builders repeatedly reconstruct paths and grow arrays.

Plan:
- After matcher correctness is fixed, profile exact, prefix, and fuzzy searches on large databases.
- Add comments documenting when full scans are intentional.
- Use cached full paths or bounded builders only where profiling shows repeated reconstruction cost.

Tests:
- Keep `test/bench_matcher.c` as the performance guard.
- Add a large synthetic DB benchmark case for exact vs prefix vs fuzzy search.

### 17. Delete confirmed dead code

Files:
- `src/main.c`

Finding:
- Unreferenced helpers include `path_map_free()`, debug-only `agent_txn_rollback()`, and single-scan progress helpers.

Plan:
- Confirm with a symbol search after any active branch changes are merged.
- Delete dead helpers and any related test-only scaffolding that is no longer used.

Tests:
- Compile all supported targets after removal.

## Verification Plan

For each focused fix:
- Run the smallest relevant unit or integration set first.
- Use `python test\runner.py unit` for unit-level changes.
- Use service-specific integration tests for IPC, service state, shared memory, and publication changes.

Before claiming the whole remediation is complete:
- Run `cmd /c clean.bat` after any `src\*.c` or `..\shared\*.c` change on Windows.
- Run `python test\generate_report.py`.
- Confirm no unsolicited debug files are created.
- Confirm Windows and Linux/WSL platform branches both compile.

New regression tests to add:
- Malformed IPC payload validation.
- Matcher exact-plus-prefix search behavior.
- Concurrent matcher index rebuild/search.
- Recursive delete symlink/junction boundary behavior.
- Recursive delete long-path truncation handling.
- Strict mkdirs failure on pre-existing directories.
- `mkdirs --force` unsupported-option handling.
- Windows IPC broken-pipe handling for non-shutdown requests.
- Shared-memory publication under repeated client opens.
- Service-backed Linux/WSL drive field parity.

## Suggested Execution Order

1. Remove production debug writes.
2. Add IPC validators and malformed payload tests.
3. Fix matcher correctness and index lifetime.
4. Fix recursive deletion symlink/reparse handling and `path_join()` checks.
5. Make mkdirs strict and reject `--force`.
6. Fix Windows control IPC broken-pipe semantics.
7. Make service-backed clients use advertised shared-memory names.
8. Redesign snapshot publication to remove the canonical-name gap.
9. Fix Linux service-backed drive-letter parity.
10. Add directory iterator and migrate one caller at a time.
11. Refactor UI console operations behind platform helpers.
12. Table-drive CLI parsing.
13. Consolidate path-map ownership and shared-memory macros.
14. Delete confirmed dead code.

## Definition of Done

- All P0 items have regression tests.
- Platform-specific branches in high-level modules are limited to small dispatch points.
- No hard-coded debug files or undocumented production side effects remain.
- Shared protocol constants have one source of truth.
- Full test report is green on the supported local targets.
- The remaining large functions have documented follow-up only when splitting them would not improve clarity immediately.
