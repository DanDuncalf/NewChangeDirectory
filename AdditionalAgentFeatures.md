# Additional Agent Mode Features — API Specification & Implementation Plan

## Design Principles

1. **Backward compatible** — existing `--agent:*` commands and flags continue to work unchanged.
2. **Fail-fast by default** — new flags make dangerous operations explicit (e.g., `--force`, `--atomic`).
3. **Machine-first output** — every new command supports `--json` with a strict, versioned schema.
4. **Cross-platform parity** — features work on Windows and Linux where the OS permits; graceful degradation where it does not.
5. **Composable** — commands are designed to be chained in shell pipelines (stdin/stdout friendly).

---

## Enhanced Existing Commands

### `--agent:mkdir` — Enhanced Single Directory Creation

**New flags:**

| Flag | Description |
|------|-------------|
| `--dry-run` | Print what would be created; do not touch the filesystem. |
| `--parents-required` | Fail if the parent directory does not already exist (do not auto-create parents). |
| `--force` | If the path exists and is an empty directory, remove it and recreate. If it exists and is non-empty, fail. |
| `--mode <octal>` | **Linux only.** Set directory mode after creation (e.g., `--mode 0700`). Ignored on Windows. |
| `--verify` | Exit `0` if the directory already exists (and matches `--mode` if specified), else exit `1`. No creation attempted. |

**Example:**
```bash
ncd --agent:mkdir /opt/myapp/config --mode 0700 --dry-run --json
ncd --agent:mkdir /opt/myapp/config --mode 0700 --parents-required --json
```

**JSON output schema (mkdir):**
```json
{
  "v": 1,
  "path": "/opt/myapp/config",
  "result": "created",
  "message": "Directory created",
  "mode": "0700"
}
```

Result codes: `created`, `exists`, `verified`, `error_perms`, `error_parent`, `error_path`, `error_not_empty`, `error`.

---

### `--agent:mkdirs` — Enhanced Tree Creation

**New flags:**

| Flag | Description |
|------|-------------|
| `--atomic` | Validate entire tree first, then create all directories. If any creation fails, remove all directories created during this invocation (rollback). |
| `--dry-run` | Print the full list of directories that would be created. |
| `--stop-on-error` | Halt on the first failure instead of continuing with siblings. |
| `--mode <octal>` | **Linux only.** Applied to every created directory. |
| `--force` | Recreate existing directories only if they are empty. Non-empty existing directories cause failure. |
| `--verify` | Verify the full tree exists (and optionally matches `--mode`). Exit `0` if complete, `1` if any node missing. |
| `--parents-required` | Fail if any intermediate parent does not already exist. |

**Behavior changes:**
- **Buffered JSON:** When `--atomic` or `--json` is used, JSON output is buffered and emitted as a single valid document. Partial JSON on crash is no longer possible.
- **Stdin input:** If no `--file` or positional argument is provided, `mkdirs` reads from stdin.

**Example:**
```bash
# Atomic project scaffold
cat <<EOF | ncd --agent:mkdirs --atomic --mode 0755 --json
project
  src
    core
    ui
  docs
EOF

# Verify existing tree
cat tree.txt | ncd --agent:mkdirs --verify --json
```

**JSON output schema (mkdirs):**
```json
{
  "v": 1,
  "atomic": true,
  "dirs": [
    {"path":"project","result":"created","message":"Directory created"},
    {"path":"project/src","result":"created","message":"Directory created"}
  ],
  "rollback": [],
  "summary": {"created":6,"exists":0,"failed":0}
}
```

If `--atomic` rollback occurs:
```json
{
  "v": 1,
  "atomic": true,
  "dirs": [
    {"path":"project","result":"created"},
    {"path":"project/src","result":"error_perms","message":"Permission denied"}
  ],
  "rollback": [
    {"path":"project","result":"removed"}
  ],
  "summary": {"created":1,"exists":0,"failed":1,"rolled_back":1}
}
```

---

## New Commands

### `--agent:rmdir <path>` — Remove Empty Directory

Remove a single directory only if it is empty. Update the NCD database to remove the entry.

| Flag | Description |
|------|-------------|
| `--force` | Remove directory even if non-empty (recursive delete of contents). **Dangerous.** |
| `--json` | Structured output. |

**JSON output:**
```json
{"v":1,"path":"/tmp/old","result":"removed","message":"Directory removed"}
```

Result codes: `removed`, `error_not_found`, `error_not_empty`, `error_perms`, `error`.

**Exit codes:** `0` = removed, `1` = error.

---

### `--agent:rmdirs <path>` — Remove Directory Tree

Remove a directory and all descendants. Updates the NCD database.

| Flag | Description |
|------|-------------|
| `--dry-run` | List what would be deleted. |
| `--force` | Required flag to actually delete; without it the command fails as a safety guard. |
| `--preserve-root` | Fail if `<path>` is `/` or a drive root (default: true). |
| `--json` | Structured output. |

**JSON output:**
```json
{
  "v": 1,
  "dirs": [
    {"path":"project/src/core","result":"removed"},
    {"path":"project/src","result":"removed"},
    {"path":"project","result":"removed"}
  ],
  "summary": {"removed":3,"failed":0}
}
```

---

### `--agent:mv <src> <dst>` — Move or Rename

Move a directory tree from `<src>` to `<dst>`. Updates NCD database entries atomically.

| Flag | Description |
|------|-------------|
| `--force` | Overwrite `<dst>` if it exists and is empty. Fail if non-empty. |
| `--json` | Structured output. |

**JSON output:**
```json
{"v":1,"src":"/tmp/project","dst":"/opt/project","result":"moved","message":"Directory moved"}
```

Result codes: `moved`, `error_not_found`, `error_exists`, `error_perms`, `error`.

---

### `--agent:verify <path>` — Verify Directory State

Verify that a path exists, is a directory, and optionally matches expected properties. Does **not** create anything.

| Flag | Description |
|------|-------------|
| `--tree <file>` | Verify the entire tree structure matches the flat-file or JSON spec. |
| `--mode <octal>` | Verify Linux mode matches ( exact, ignoring umask). |
| `--empty` | Verify the directory is empty. |
| `--json` | Structured output. |

**Example:**
```bash
ncd --agent:verify /opt/project --tree tree.spec --json
```

**JSON output:**
```json
{
  "v": 1,
  "path": "/opt/project",
  "verified": true,
  "checks": [
    {"check":"exists","passed":true},
    {"check":"is_directory","passed":true},
    {"check":"tree_structure","passed":true},
    {"check":"mode","expected":"0755","actual":"0755","passed":true}
  ]
}
```

**Exit codes:** `0` = all checks passed, `1` = one or more checks failed.

---

### `--agent:chmod <path> <mode>` — Change Permissions

Set directory permissions. Windows ignores this command (returns `error_unsupported`).

| Flag | Description |
|------|-------------|
| `--recursive` | Apply mode to all descendants. |
| `--json` | Structured output. |

**JSON output:**
```json
{"v":1,"path":"/opt/project","mode":"0700","result":"changed","changed":1}
```

---

### `--agent:ln <target> <link>` — Create Symbolic Link

Create a symlink at `<link>` pointing to `<target>`. Updates the NCD database if the link target is a directory.

| Flag | Description |
|------|-------------|
| `--json` | Structured output. |

**JSON output:**
```json
{"v":1,"target":"/real/project","link":"/opt/project","result":"created"}
```

Result codes: `created`, `error_exists`, `error_perms`, `error_unsupported` (Windows without developer mode), `error`.

---

## Shared Infrastructure Changes

### 1. Agent Result Buffering
All JSON-emitting agent commands must buffer output into a `StrBuilder` or temporary heap buffer and emit the final document in one `agent_print()` call. This prevents partial JSON on crash or Ctrl+C.

### 2. Transactional Context
A lightweight `AgentTxn` struct tracks directories created during an atomic operation:
```c
typedef struct {
    char **paths;
    int count;
    int capacity;
} AgentTxn;

void agent_txn_add(AgentTxn *txn, const char *path);
bool agent_txn_rollback(AgentTxn *txn);  /* Remove all tracked directories */
```

### 3. Permission Abstraction
A cross-platform `platform_set_mode(const char *path, int mode)` wrapper:
- Linux: `chmod(path, mode)`
- Windows: No-op returning `true` (or future ACL support)

### 4. Stdin Input Support
`agent_mode_mkdirs` should read from `stdin` when no `--file` or argument is provided (detected via `isatty()` on POSIX, file handle check on Windows).

---

## Parallel Implementation Plan (4 Sub-Agents)

### Agent 1 — mkdir Enhancements (Dry Run, Atomic, Verify, Force)
**Scope:** `agent_mode_mkdir`, `agent_mode_mkdirs`, `mkdir_create_recursive`
**Tasks:**
1. Implement `--dry-run` flag and dry-run path in `mkdir_create_recursive`.
2. Implement `--atomic` with `AgentTxn` rollback tracking.
3. Implement `--verify` (read-only traversal, no creation).
4. Implement `--force` (remove empty existing directories before recreating).
5. Implement `--stop-on-error` (halt recursion on first error).
6. Implement buffered JSON output for `mkdirs`.
7. Add stdin support to `mkdirs`.

**Files to touch:** `src/main.c` (agent functions), `src/ncd.h` (new option flags, `AgentTxn`), `../shared/platform.c` (if new platform helpers needed).

### Agent 2 — Delete & Cleanup (rmdir, rmdirs)
**Scope:** `agent_mode_rmdir`, `agent_mode_rmdirs`
**Tasks:**
1. Implement `--agent:rmdir` with empty-directory safety.
2. Implement `--agent:rmdirs` with `--force` guard and `--preserve-root`.
3. Implement `--dry-run` for `rmdirs`.
4. Update NCD database to remove deleted entries (`db_remove_path` or equivalent).
5. Implement recursive deletion helper (`platform_rmdir_recursive` or similar).

**Files to touch:** `src/main.c`, `src/database.c/.h` (add `db_remove_path`), `../shared/platform.c` (add `platform_remove_dir` / `platform_remove_tree`).

### Agent 3 — Move, Rename & Symlinks
**Scope:** `agent_mode_mv`, `agent_mode_ln`
**Tasks:**
1. Implement `--agent:mv` with database entry atomic update.
2. Implement `--force` for mv (overwrite empty destination).
3. Implement `--agent:ln` with platform detection.
4. On Windows: detect developer mode / SeCreateSymbolicLinkPrivilege; return `error_unsupported` if unavailable.
5. Update NCD database: remove old path, add new path.

**Files to touch:** `src/main.c`, `src/database.c/.h`, `../shared/platform.c` (add `platform_move_dir`, `platform_create_symlink`).

### Agent 4 — Permissions & Verification Infrastructure
**Scope:** `agent_mode_verify`, `agent_mode_chmod`, permission abstraction
**Tasks:**
1. Implement `platform_set_mode()` in `../shared/platform.c`.
2. Implement `platform_get_mode()` for verification.
3. Implement `--agent:chmod` with `--recursive`.
4. Implement `--agent:verify` with `--tree`, `--mode`, `--empty` checks.
5. Define JSON schema constants and validation helpers.

**Files to touch:** `src/main.c`, `src/ncd.h` (result enums, JSON schema version), `../shared/platform.c/.h`.

---

## Testing Requirements

Each sub-agent must add tests to `test/test_agent_mkdir.c` (or create `test/test_agent_extended.c`):

| Test Category | Required Tests |
|---------------|----------------|
| Dry Run | `mkdir_dry_run_does_not_create`, `mkdirs_dry_run_lists_all_paths` |
| Atomic | `mkdirs_atomic_all_or_nothing`, `mkdirs_atomic_rollback_on_failure` |
| Verify | `mkdir_verify_existing_passes`, `mkdir_verify_missing_fails`, `mkdirs_verify_tree_mismatch_fails` |
| Force | `mkdir_force_recreate_empty`, `mkdir_force_fails_on_non_empty` |
| Permissions | `mkdir_linux_mode_0700`, `chmod_recursive_changes_all`, `verify_mode_mismatch_fails` |
| Delete | `rmdir_removes_empty`, `rmdir_fails_on_non_empty`, `rmdirs_force_removes_tree`, `rmdirs_preserve_root_blocks_drive_root` |
| Move | `mv_renames_directory`, `mv_updates_database`, `mv_force_overwrite_empty` |
| Symlinks | `ln_creates_symlink`, `ln_fails_without_privilege_windows` |
| Stdin | `mkdirs_reads_from_stdin` |
| JSON | `mkdirs_json_is_valid_on_failure`, `mkdirs_json_contains_rollback_array` |

---

## Migration & Compatibility

- **No breaking changes** to existing `--agent:mkdir` or `--agent:mkdirs` invocations.
- New flags are ignored by old binaries (they will error as unrecognized options, which is standard CLI behavior).
- JSON schema version remains `v:1` with additive fields only.
- `--mode` is silently ignored on Windows (documented behavior), not an error.
