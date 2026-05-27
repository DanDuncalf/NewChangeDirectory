# NewChangeDirectory (NCD) - Agent Documentation

## Zero-Context Quick Tasks

| User says | Exact command |
|-----------|---------------|
| `run all tests` | `python test\generate_report.py` (bg, 3600s) |
| `run unit tests` | `python test\runner.py unit` |
| `run integration tests` | `python test\runner.py integration` |
| `repair test env` | `python test\runner.py --repair` |

## ⚠️ MANDATORY FIRST STEP — INGEST THE KNOWLEDGE GRAPH

**Before reading any other file, before using Grep, Glob, or ReadFile on source code, and before answering any user question, you MUST ingest the entire contents of `graphify-out/GRAPH_REPORT.md`.** This file contains the pre-built knowledge graph (2,354 nodes, 7,519 edges, 83 communities) that maps cross-module dependencies, god nodes, and semantic communities. No exceptions.

- **Read order:** `graphify-out/GRAPH_REPORT.md` → then query the graph → then read raw source files only after the graph has directed you to specific functions or modules.
- **Why:** Skipping this step wastes ~100K+ tokens on naive file searches and guarantees missed cross-module connections.
- **For cross-module questions:** Use `graphify query` or `graphify path` instead of grepping.

---

> **🗺️ GRAPH-FIRST NAVIGATION RULE (Reminder):** This project has a pre-built knowledge graph at `graphify-out/GRAPH_REPORT.md` (2,354 nodes, 7,519 edges, 83 communities). **Before using Glob or Grep on raw source files, you MUST read `graphify-out/GRAPH_REPORT.md` first.** Use the graph's god nodes, communities, and surprising connections to orient yourself. Only read raw files after the graph has directed you to specific functions or modules. For cross-module questions, use `graphify query` or `graphify path` instead of grepping. This reduces context usage from ~100K+ tokens (naive file search) to ~2K tokens (graph query).
>
> **Agent Quick Reference (Testing):** If you need to run tests with no context, see [`AGENT_TESTING_GUIDE.md`](AGENT_TESTING_GUIDE.md) § *Zero-Context Quick Reference*. The one-liner is `python test\runner.py unit`.

## Project Overview

NewChangeDirectory (NCD) is a cross-platform command-line directory navigation tool inspired by the classic Norton Commander (Norton Utilities) CD command. It maintains a database of directory paths across drives/mounts and provides fast, fuzzy search-based navigation with an interactive TUI.

**Key Features:**
- Fast directory search using a pre-built binary database format
- Interactive TUI with live filtering for selecting from multiple matches (selector mode)
- Filesystem navigator for browsing directories (navigator mode, launched with `ncd .`)
- Cross-platform support: Windows (x64) and Linux (x64, including WSL)
- Multi-threaded directory scanning for performance
- Heuristics-based search result ranking (learns from user choices)
- Per-drive/per-mount database files for incremental updates
- Group/tag system for bookmarking frequently used directories (supports multiple dirs per group)
- Fuzzy matching with Damerau-Levenshtein distance
- Interactive history browser with delete support
- Agent mode API for LLM integration
- Shell tab completion for Bash, Zsh, and PowerShell
- Exclusion list for filtering unwanted directories
- Optional resident service for shared-memory state access
- Directory history for quick navigation between recent locations

## Quality Remediation (completed 2026-05-08)

Phases completed:
- **Phase 0:** Baseline established (2376 tests, 9 pre-existing failures)
- **Phase 1:** Test verdict integrity — explicit SKIP semantics (C framework + Python executor)
- **Phase 2:** Missing test coverage — removed dead code, added regression tests, normalized POSIX exit handling
- **Phase 3:** P0 bug test matrix — 11 regression tests covering P0.1-P0.8, P1.1a, P2.20
- **Phase 4:** Production P0 fixes — atomic flags, mutex-protected queues, CRC64 init fix, name-index locking, SHM gap fix
- **Phase 5:** Test DRY cleanup — extracted service_test_common and agent_test_common helpers
- **Phase 6:** Production P1 refactors — IPC consolidation, atomic-write consolidation
- **Phase 7:** Final cleanup — documentation update, test-health dashboard

Key deliverables:
- `test/test_framework.h`: SKIP_TEST() macro, TEST_SKIP sentinel, tests_skipped counter
- `test/test_framework_contract.c`: Framework contract validation
- `test/test_p0_regression.c`: P0 bug regression suite (11 tests)
- `test/test_posix_exit.h`: safe_exit_code() helper
- `test/test_service_database.c`: regression_rescan_no_data_loss test
- Full test suite: 2376 tests, 0 failures, 2 skipped (framework contract)

## Technology Stack

- **Language:** C11
- **Platforms:** Windows x64, Linux x64 (WSL compatible)
- **Build Tools:**
  - Windows: MSVC (`cl.exe`) via `build.bat`, or MinGW-w64 (`gcc`) via `Makefile`
  - Linux: GCC or Clang via `build.sh`
- **Threading:** Windows threads (Win32 API) / POSIX threads (pthreads)
- **UI:** Platform-native console I/O with ANSI escape sequences (Linux) / Win32 Console API (Windows)
- **IPC:** Named pipes (Windows) / Unix domain sockets (Linux)
- **Shared Memory:** `CreateFileMapping` (Windows) / `shm_open` (Linux)

## Project Structure

```
NewChangeDirectory/
├── src/                       # Source code
│   ├── ncd.h                 # Core types, constants, platform detection macros
│   ├── main.c                # Entry point, CLI parsing, orchestration, heuristics
│   ├── database.c/.h         # Database load/save (binary format), groups, config, metadata
│   ├── scanner.c/.h          # Multi-threaded directory enumeration
│   ├── matcher.c/.h          # Search matching algorithm (chain + fuzzy)
│   ├── ui.c/.h               # Interactive terminal UI (selector + navigator + history)
│   ├── platform.c/.h         # NCD-specific platform wrappers (includes ../shared/)
│   ├── strbuilder.h          # Compatibility shim to ../shared/strbuilder.h
│   ├── state_backend.h       # State access abstraction (local or service)
│   ├── state_backend_local.c # Local disk state implementation
│   ├── state_backend_service.c # Service-backed state implementation
│   ├── shared_state.h/.c     # Shared memory snapshot format
│   ├── shm_platform.h        # Shared memory platform abstraction
│   ├── shm_platform_win.c    # Windows shared memory implementation
│   ├── shm_platform_posix.c  # Linux/POSIX shared memory implementation
│   ├── control_ipc.h         # IPC control channel interface
│   ├── control_ipc_win.c     # Windows named pipe IPC
│   ├── control_ipc_posix.c   # Linux Unix socket IPC
│   ├── service_main.c        # Service executable entry point
│   ├── service_state.c/.h    # Service state management
│   ├── service_publish.c/.h  # Snapshot publication
│   ├── ncd.vcxproj           # Visual Studio project file
│   └── ncd.sln               # Visual Studio solution file
├── completions/              # Shell completion scripts
│   ├── ncd.bash              # Bash completion
│   ├── _ncd                  # Zsh completion
│   └── ncd.ps1               # PowerShell completion
├── test/                     # Test suite
│   ├── Test-Service-Windows.bat   # Windows Service tests (isolated)
│   ├── test_service_wsl.sh        # [NEW] WSL Service tests (isolated)
│   ├── Test-NCD-Windows-Standalone.bat # NCD Windows - standalone
│   ├── Test-NCD-Windows-With-Service.bat # NCD Windows - with service
│   ├── test_ncd_wsl_standalone.sh    # [NEW] NCD WSL - standalone
│   ├── test_ncd_wsl_with_service.sh  # [NEW] NCD WSL - with service
│   ├── test_framework.h/.c   # Minimal unit testing framework
│   ├── test_database.c       # Database module unit tests
│   ├── test_matcher.c        # Matcher module unit tests
│   ├── test_db_corruption.c  # Database corruption handling tests
│   ├── test_bugs.c           # Known bug detection tests
│   ├── test_shared_state.c   # Shared state tests
│   ├── test_service_lazy_load.c   # Service lazy loading tests
│   ├── test_service_parity.c      # Service vs standalone parity tests
│   ├── test_service_lifecycle.c   # Service lifecycle (start/stop) tests
│   ├── test_service_integration.c # NCD client service integration tests
│   ├── fuzz_database.c       # Fuzz testing for database loading
│   ├── bench_matcher.c       # Performance benchmarks
│   ├── Makefile              # Test build system
│   ├── README.md             # Test suite documentation
│   ├── Win/                  # Windows-specific tests
│   ├── Wsl/                  # WSL/Linux-specific tests
│   └── PowerShell/           # PowerShell-specific tests
├── tasks/                    # Active task notes
│   └── heap_corruption_investigation.md  # Active investigation note
├── ncd.bat                   # Windows wrapper script (CMD)
├── ncd                       # Linux wrapper script (Bash)
├── ncd_service.bat           # Windows service launcher
├── ncd_service               # Linux service launcher
├── build.bat                 # Windows build (MSVC)
├── build.sh                  # Linux build (GCC/Clang)
├── Makefile                  # Cross-platform build (MinGW)
├── deploy.bat                # Windows deployment script
├── deploy.sh                 # Linux deployment script
└── LINUX_PORT_CHANGELIST.md  # Linux porting documentation
```

**External Dependency:**
The project requires a shared platform abstraction library located at `../shared/` (sibling directory). This library provides cross-platform utilities for filesystem, console I/O, threading, and string building. The build scripts reference files like `../shared/platform.c`, `../shared/platform.h`, `../shared/strbuilder.c`, and `../shared/common.c`.

## Module Organization

| Module | Purpose | Key Files |
|--------|---------|-----------|
| Core Types | Platform detection, shared structures, constants | `ncd.h` |
| Main | CLI parsing, heuristics, result generation, orchestration | `main.c` |
| Database | Binary DB load/save, path helpers, groups, config, metadata, exclusions, history | `database.c/.h` |
| Scanner | Thread pool management, recursive directory scanning | `scanner.c/.h` |
| Matcher | Chain-matching algorithm, name index, fuzzy matching with DL distance | `matcher.c/.h` |
| UI | Interactive selection list, filesystem navigator, history browser, dialogs, config editor | `ui.c/.h` |
| Platform | NCD-specific wrappers around shared platform library | `platform.c/.h` |
| State Backend | Abstraction for local disk vs service-backed state | `state_backend.h`, `state_backend_local.c`, `state_backend_service.c` |
| Shared State | Shared memory snapshot format and validation | `shared_state.h/.c` |
| SHM Platform | Cross-platform shared memory wrapper | `shm_platform.h`, `shm_platform_win.c`, `shm_platform_posix.c` |
| Control IPC | Inter-process communication for service/client | `control_ipc.h`, `control_ipc_win.c`, `control_ipc_posix.c` |
| Service | Resident state service implementation | `service_main.c`, `service_state.c/.h`, `service_publish.c/.h` |
| String Builder | Dynamic string construction, JSON escaping | `../shared/strbuilder.c` + `src/strbuilder.h` |
| Common | Memory allocation wrappers that exit on OOM | `../shared/common.c` |

## Build Commands

See [`README.md`](README.md) § *Building from Source* for full build instructions per platform.

Quick reference:
```powershell
cmd /c build.bat        # Windows (MSVC)
make                    # Windows (MinGW) / Linux (see Makefile)
./build.sh              # Linux (GCC/Clang)
```

### Full Clean (Prevent Stale Object Files)

**Always run a full clean before any complete rebuild-and-test cycle.** The MSVC build system places intermediate `.obj` files in multiple locations (`obj\`, `test\obj\`, and occasionally the project root). If a source file is modified but an old `.obj` survives in any of these locations, test executables may link against stale code while the main binary uses current code, causing mysterious test failures.

```powershell
cmd /c clean.bat        # Remove ALL objects + test binaries everywhere
```

After cleaning, rebuild in order:
```powershell
cmd /c build.bat
cd test && cmd /c build-tests.bat
cd test && cmd /c build_new_tests.bat
python test\generate_report.py
```

> **When to clean:**
> - Before running `generate_report.py` after any `src\*.c` or `..\shared\*.c` change
> - When tests fail with "unresolved external" or behave as if source changes were ignored
> - When `test_agent_mv.exe` or other agent tests fail after database-related edits
> - As the first step of any CI/CD build pipeline

### Deploy Checklist — MANDATORY

**Before every deploy of `NewChangeDirectory.exe`, check and clean up Image File Execution Options (IFEO).** Agents may enable the Application Verifier during debugging (injecting `vrfcore.dll` + `vfbasics.dll`), which adds ~6 seconds of overhead to every launch. This is keyed by filename, so it silently persists across rebuilds and reboots.

```powershell
# Check for IFEO key (returns nothing if clean)
Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\NewChangeDirectory.exe" -ErrorAction SilentlyContinue

# If present, remove it immediately
Remove-Item -Path "HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\NewChangeDirectory.exe" -Recurse -Force
```

**Symptoms that IFEO is active:**
- `ncd --version` takes 5-7 seconds instead of <300 ms
- Renaming the binary to anything else makes it fast
- Issue is specific to the filename, not the path or binary content
- Persists after Windows Defender is disabled and exclusions are added

> **History:** On 2026-05-27, a 6-second launch delay was traced to a leftover IFEO key with `VerifierDlls=vrfcore.dll vfbasics.dll`. An agent had enabled Application Verifier during debugging and never removed it. The previous investigation misattributed this to Codex sandbox because renaming the binary bypassed the IFEO name match.

## Testing

> **📋 Full docs:** [`AGENT_TESTING_GUIDE.md`](AGENT_TESTING_GUIDE.md)

### Run All Tests (Mandatory Command)

When the user asks to "run all tests", execute **exactly** this command and nothing else:

```powershell
python test/generate_report.py
```

This is the **unified cross-platform runner** — it auto-builds, runs Windows + WSL tests, and writes the detailed `test.results` report. See `AGENT_TESTING_GUIDE.md` § *Expected Output Format — MANDATORY*.

> **Pre-test clean:** If any `src\*.c` or `..\shared\*.c` file has changed since the last build, run `cmd /c clean.bat` first. Stale `.obj` files can cause test executables to link against old code.

### Quick Reference
```powershell
python test\runner.py unit           # Unit tests only
python test\runner.py integration    # Integration tests only
python test\runner.py --repair       # Fix corrupted test environment
```

### Key Rules
- **Never run `test\*.exe` directly** — always use the harness
- Use `run_in_background=true` with timeout `3600` for full-suite runs (~35 minutes)
- Set `NCD_TEST_MODE=1` to disable background rescans during tests
- **Always show the test runner output** — do not suppress or truncate
- **Act on background task notifications immediately**

### Keystroke Injection for TUI Testing

| Variable | Purpose |
|----------|---------|
| `NCD_UI_KEYS` | Comma-separated keys (`DOWN,ENTER,TEXT:hello`). Prefix with `@` to load from file. |
| `NCD_UI_KEY_TIMEOUT_MS` | Timeout for next key (ms). Default: indefinite. |

Key tokens: `UP`, `DOWN`, `LEFT`, `RIGHT`, `PGUP`, `PGDN`, `HOME`, `END`, `ENTER`, `ESC`, `TAB`, `BACKSPACE`, `DELETE`, `SPACE`, `TEXT:<string>`.

### Troubleshooting Test Issues

| Symptom | Fix |
|---------|-----|
| "LOCALAPPDATA points to test temp" | `python test\runner.py --repair` |
| Service tests hang | `taskkill /F /IM NCDService.exe` then re-run |
| PowerShell execution policy | Use Python runner instead (no policy issues) |

See `AGENT_TESTING_GUIDE.md` for detailed procedures.


## Usage

> **For basic command-line usage, interactive UI, wrapper scripts, and shell tab completion, see [`README.md`](README.md).**

### Agent Mode (LLM Integration)

Agent mode is the primary interface for automated and LLM-driven interaction.

For the implemented filesystem-mutation flags and JSON field details, see [`docs/agent_mode.md`](docs/agent_mode.md).

```bash
ncd --agent:query <search> [--json] [--limit N] [--depth]
ncd --agent:ls <path> [--json] [--depth N] [--dirs-only|--files-only]
ncd --agent:tree <path> [--json] [--depth N] [--flat]
ncd --agent:check <path> | --db-age | --stats | --service-status
ncd --agent:complete <partial> [--limit N]
ncd --agent:mkdir <path> [--json] [--force] [--mode <octal>] [--verify]
ncd --agent:mkdirs [--file <path>] [--json] [--atomic] [--verify] <content>
ncd --agent:rmdir <path> [--force] [--json]
ncd --agent:rmdirs <path> [--force] [--json]
ncd --agent:mv <src> <dst> [--force] [--json]
ncd --agent:ln <target> <link> [--json]
ncd --agent:verify <path> [--empty] [--mode <octal>] [--tree <spec>] [--json]
ncd --agent:chmod <path> [--mode <octal>] [--recursive] [--json]
ncd --agent:help
```

**Exit codes:** `0` = Success/Found, `1` = Not found/Error.

#### Agent Tree Output Formats

| Option | Description |
|--------|-------------|
| `--depth N` | Max depth (default: 3) |
| `--json` | Output JSON |
| `--flat` | Full relative paths instead of names |

#### Agent Mkdirs Input Formats

**Flat file** (2-space indentation = nesting):
```
project
  src
    core
  docs
```

**JSON:**
```bash
ncd --agent:mkdirs '[{"name":"project","children":[{"name":"src"}]}]'
```

`mkdirs` creates new directory trees only. If any requested directory already exists, the command fails with `error_exists`.

Result codes: `created`, `error_exists`, `error_perms`, `error_parent`, `error_path`, `error`.

#### Agent Rmdir / Rmdirs

Remove directories. `rmdir` removes a single empty directory; `rmdirs` recursively removes a directory tree.

```bash
ncd --agent:rmdir /home/user/old_dir --json
ncd --agent:rmdirs /home/user/old_tree --force --json
```

| Option | Description |
|--------|-------------|
| `--force` | Required for `rmdirs`; also allows `rmdir` to remove non-empty directories |
| `--json` | JSON output |

Result codes: `removed`, `error_not_found`, `error_not_empty`, `error_perms`.

#### Agent Mv / Ln

Move or symlink directories. `mv` updates the NCD database automatically.

```bash
ncd --agent:mv /home/user/src /home/user/dst --json
ncd --agent:ln /home/user/target /home/user/link --json
```

| Option | Description |
|--------|-------------|
| `--force` | Overwrite empty destination directory (mv only) |
| `--json` | JSON output |

Result codes (mv): `moved`, `error_not_found`, `error_exists`, `error_perms`.
Result codes (ln): `created`, `error_exists`, `error_unsupported` (Windows without dev mode), `error_perms`.

#### Agent Verify

Verify directory properties. Useful for CI/CD and deployment validation.

```bash
ncd --agent:verify /home/user/projects --json
ncd --agent:verify /home/user/empty_dir --empty --json
ncd --agent:verify /home/user/projects --tree "src\n  core\n  ui" --json
```

| Option | Description |
|--------|-------------|
| `--empty` | Verify directory contains no entries |
| `--mode <octal>` | Verify permissions match (Linux only) |
| `--tree <spec>` | Verify directory tree matches flat or JSON spec |
| `--json` | JSON output with per-check results |

Output fields: `verified` (bool), `checks[]` (array of check objects).

#### Agent Chmod

Change directory permissions. **Linux only**; returns `error_unsupported` on Windows.

```bash
ncd --agent:chmod /home/user/projects --mode 0755 --recursive --json
```

| Option | Description |
|--------|-------------|
| `--mode <octal>` | Permission mode (required) |
| `--recursive` | Apply to entire directory tree |
| `--json` | JSON output |

Result codes: `changed`, `error_unsupported`, `error_not_found`, `error_perms`.

#### Agent Help

Display the complete list of agent commands and options.

```bash
ncd --agent:help
```

### Service Management & Logging

```bash
ncd_service start                 # Start per-user resident service
ncd_service start                 # System-wide service is now the default (all users)
ncd_service start --user-mode     # Per-user service (current user only)
ncd_service start -init           # Start and initialize database (scan all drives)
ncd_service start -init C,D,E     # Start and initialize database (scan specific drives)
ncd_service stop                  # Stop (default 5s timeout)
ncd_service stop block N          # Stop with custom timeout
ncd --agent:check --service-status # Check status
ncd <search>                      # System-wide mode is the default
ncd --user-mode <search>          # Client connecting to per-user service
```

Log levels (`-log0` through `-log5`): See Service Logging section below.

### Configuration Override

```bash
ncd -conf <path>                  # Custom metadata file
ncd_service start -conf <path>    # Service with custom metadata
```

### Service Database Initialization (`-init`)

The `-init` option performs a synchronous blocking database scan during service startup. This is especially useful for first-time deployment or when you want to ensure the database is fully built before the service accepts client requests.

**Behavior:**
- **No metadata file exists** (first run): The service enters `SCANNING` state, scans the requested drives (or all drives), saves the database and metadata to disk, then transitions to `READY`.
- **Metadata file exists**: The option behaves like `ncd -r {drivelist}` — performs a forced rescan of the specified drives on startup.
- **No drive list specified**: Scans all drives/mounts (same as `ncd -r`).
- **Drive list specified** (e.g. `-init C,D,E`): Scans only the requested drives.

**Key characteristics:**
- The scan runs in the **main thread** and blocks service readiness until complete
- Clients connecting during init see `SCANNING` state and receive a busy message
- Uses the same `scan_mounts()` code path as `ncd -r` for identical behavior
- Automatically saves metadata after scan to prevent re-initialization on future starts
- Respects `NCD_TEST_MODE=1` (skips scan in test mode to avoid scanning user drives)

**Examples:**
```bash
# First-time setup: scan all drives before service becomes ready
ncd_service start -init

# Initialize with specific drives only
ncd_service start -init C,D

# Combine with other options
ncd_service start -init -log2 -conf C:\NCD\custom.metadata
```

## Database Storage

### Locations

**System-wide (default):** Shared paths accessible by all users:
- Windows: `C:\ProgramData\NCD\`
- Linux:   `/var/lib/ncd/`

**User mode (`--user-mode`):** See [`README.md`](README.md) § *Database Storage* for per-user path locations.

System mode uses shared, fixed paths so all users access the same database.
Also uses fixed IPC pipe/socket names and `Global\` shared memory namespace
on Windows (or non-UID-scoped names on Linux) with relaxed permissions.

### Binary Format

Magic: `NCDB` (0x4244434E)
Version: 2 (with CRC64 checksum)
Layout:
```
[BinFileHdr: 32 bytes]
[BinDriveHdr x drive_count: 80 bytes each]
[DirEntry[] + name_pool per drive]
[CRC64 checksum]
```

### Metadata Format

Magic: `NCMD` (0x444D434E)
Version: 1
Sections: Config (0x01), Groups (0x02), Heuristics (0x03), Exclusions (0x04), DirHistory (0x05)

### Atomic Writes

All database writes use temp-file-then-rename:
1. Write to `.tmp`
2. Move current to `.old` (backup)
3. Move `.tmp` to final name
4. On failure: restore from `.old`

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

### Why consolidated metadata?

- Single file for config, groups, heuristics, exclusions, and directory history
- Atomic updates (all or nothing)
- Reduces file I/O and simplifies backup

### Auto-Rescan Configuration

The auto-rescan interval is now configurable. Use `ncd -c` to edit the configuration and set:

- **Auto-rescan hours**: Number of hours (1-168) before automatically triggering a background rescan
- **Never** (-1): Disable auto-rescan entirely

The default is -1 (never auto-rescan). When set to a positive value and the database is older than the configured interval, NCD will automatically spawn a background rescan after a successful navigation.

**⚠️ WARNING:** The background rescan uses `ncd -r` which scans **ALL drives/mounts**. If you don't want this behavior, set `rescan_interval_hours=-1` or be aware that using `ncd` manually (outside of test mode) may trigger full system scans.

### Why two UI modes?

1. **Selector:** List of search results with live filtering (arrow keys to select, type to filter)
2. **Navigator:** Browse filesystem hierarchy (Tab to enter, Backspace to exit)

This mirrors Norton Commander's behavior for both search-driven and browse-driven workflows.

### Why an optional resident service?

- Keeps metadata and database hot in shared memory
- Eliminates disk I/O on client startup when service is running
- Client falls back to standalone mode if service unavailable
- Service handles persistence and snapshot publication

### Service Shutdown Behavior

When stopping the service, the following occurs:

1. **Graceful shutdown is always attempted first**: The `stop` command sends an IPC `REQUEST_SHUTDOWN` message to the service, which sets the shutdown flag. The service then exits its main loop and performs cleanup.

2. **Automatic flush on exit**: During cleanup, the service checks if there are any dirty (unsaved) changes:
   - Metadata changes (config, groups, heuristics, exclusions, directory history)
   - Database changes (from rescans)
   - All dirty data is flushed to disk before the process exits

3. **Blocking with timeout**: The `stop` command blocks and waits for the service to stop:
   - Default wait: 5 seconds
   - `stop block N`: Wait N seconds (1-300)
   - Returns `0` if stopped successfully
   - Returns `-1` if timeout exceeded (service may still be stopping)
   - Returns `1` if service not running or IPC error

4. **Force termination only as last resort**: Tests and scripts should always try graceful stop first. Force kill (e.g., `TerminateProcess` on Windows, `kill -9` on Linux) should only be used if graceful shutdown times out.

**Example - Graceful shutdown with custom timeout:**
```bash
# Stop with default 5-second timeout
ncd_service stop

# Stop with 10-second timeout (good for systems with large databases)
ncd_service stop block 10

# Check exit code to determine if force kill is needed
if [ $? -eq -1 ]; then
    echo "Service did not stop in time, may need force kill"
fi
```

### Service Version Compatibility

NCD maintains version compatibility between the client (`NewChangeDirectory.exe`/`ncd`) and the service (`NCDService.exe`/`ncd_service`). The IPC protocol includes version negotiation to ensure compatibility.

**Version Check Behavior:**
- When a client starts and detects a running service, it performs a version check via IPC
- If the service version is older than the minimum required by the client:
  1. The client attempts to gracefully stop the old service
  2. The client informs the user that a new service version is needed
  3. The user must manually start the new service version
- If version check fails (IPC error), the client falls back to standalone mode

**Compatibility Matrix:**

| Client Version | Min Service Version | Compatible Service Versions |
|----------------|---------------------|----------------------------|
| 1.3.x | 1.3.0 | 1.3.0 - 1.3.x |
| 1.2.x | 1.2.0 | 1.2.0 - 1.3.x (forward compatible) |

**Upgrade/Downgrade Procedures:**

**Upgrading (Client + Service):**
```bash
# 1. Stop the old service
ncd_service stop

# 2. Replace both binaries (ncd and ncd_service)
#    - Windows: Copy NewChangeDirectory.exe and NCDService.exe
#    - Linux: Copy ncd and ncd_service

# 3. Start the new service
ncd_service start

# 4. Verify version
ncd -v
```

**Downgrading (if needed):**
```bash
# 1. Stop the current service
ncd_service stop

# 2. Restore previous version binaries

# 3. Start the previous service version
ncd_service start
```

**Force Override:**
Use `--force` flag to bypass version check (not recommended):
```bash
ncd --force <search_term>
```

### Service Logging System

The NCD Service includes a comprehensive logging system for debugging crashes and monitoring service behavior. The logging system is **thread-safe** and writes to a log file with proper locking.

**Log File Location:**
- **Windows (per-user):** `%LOCALAPPDATA%\NCD\ncd_service.log` (e.g., `C:\Users\<username>\AppData\Local\NCD\ncd_service.log`)
- **Linux (per-user):** `~/.local/share/ncd/ncd_service.log` (or `$XDG_DATA_HOME/ncd/ncd_service.log`)
- **System mode (Windows):** `C:\ProgramData\NCD\ncd_service.log`
- **System mode (Linux):** `/var/lib/ncd/ncd_service.log`

The log file is created in the same directory as the database files and is opened in append mode.

**Log Levels:**

| Level | Flag | Description |
|-------|------|-------------|
| -1 | (none) | Logging disabled (default) |
| 0 | `-log0` | Service start/stop, rescan requests, all client requests |
| 1 | `-log1` | Level 0 + all responses sent to clients |
| 2 | `-log2` | Level 1 + detailed startup/shutdown steps, intermediate operations |
| 3-5 | `-log3` to `-log5` | Reserved for future debugging use |

**Usage Examples:**

```bash
# Start service with basic logging (high-level events only)
ncd_service start -log0

# Start service with detailed logging (recommended for crash diagnosis)
ncd_service start -log2

# Combine with other options
ncd_service start -log2 -conf C:\NCD\custom.metadata
```

**Log Format:**
```
[HH:MM:SS] [thread_id] [L<level>] message
```

Example log output:
```
[14:32:15] [1234] [L0] === Service Starting at 2026-04-07 14:32:15 ===
[14:32:15] [1234] [L0] Version: 1.3 (Build: Apr 7 2026 14:30:00)
[14:32:15] [1234] [L0] Log Level: 2 (+ detailed operations)
[14:32:15] [1234] [L2] Initializing service state...
[14:32:15] [1234] [L2] Service state initialized successfully
[14:32:15] [1234] [L0] Metadata snapshot published
[14:32:15] [1234] [L2] Starting background loader thread...
[14:32:16] [5678] [L2] Background loader thread started
[14:32:16] [5678] [L2] About to load databases...
[14:32:18] [5678] [L0] Service state changed to READY
```

**For Crash Diagnosis:**
- Use `-log2` to capture detailed startup/shutdown sequences
- The log file is flushed after each write to ensure data is preserved on crash
- Thread IDs help track which thread was active during a crash
- Each log entry is timestamped with millisecond precision

**Testing with Logging:**

When testing the service, logging should be enabled at level 2 (`-log2`) to verify correct operation:

```bash
# Start service with level 2 logging for testing
ncd_service start -log2

# Or in debug mode (automatically enables -log2)
ncd_service /agdb
```

**Log Verification:**
- **Level 1 (Error Logging):** All error conditions in the service must be logged at level 1 or higher. Any log entry containing "ERROR" indicates a failure.
- **Level 2 (Success Logging):** When a potential error is checked and the operation succeeds, a success message is logged at level 2.
- **Test Verification:** Tests should verify that no "ERROR" entries appear in the log file after service operations.

Example error-check logging pattern:
```
[L1] ERROR - Failed to initialize snapshot publisher    # Error occurred
[L2] Snapshot publisher initialized successfully         # Success path (no error)
```

To verify no errors in automated tests:
```bash
# After running service tests, check log for errors
if grep -i "ERROR" ~/.local/share/ncd/ncd_service.log; then
    echo "Test FAILED: Errors found in service log"
    exit 1
fi
```

### Service-Side Rescan

When the NCD service is running, `-r` (rescan) requests are handled by the service rather than the client. This provides several benefits:

- **Non-blocking operation**: The service continues serving queries from the old snapshot while building a new database
- **Atomic updates**: The new database is published atomically once scanning completes
- **Consistent state**: All clients see the same database generation after rescan completes

**How It Works:**

1. Client sends `REQUEST_RESCAN` IPC message to the service
2. Service transitions to `SCANNING` state (logged at level 0)
3. Service builds new `NcdDatabase` in memory (old snapshot remains available)
4. On completion, service atomically publishes new snapshot and bumps generation
5. Service returns to `READY` state

**IPC Protocol:**

| Message | Direction | Purpose |
|---------|-----------|---------|
| `NCD_MSG_REQUEST_RESCAN` | Client → Service | Request filesystem rescan |
| `NCD_REQUEST_RESCAN_OK` | Service → Client | Rescan accepted/started |
| `NCD_REQUEST_RESCAN_BUSY` | Service → Client | Service already scanning |

**Log Events:**

With `-log0` or higher, the service logs rescan operations:
```
[L0] REQUEST_RESCAN received (seq=42, drive_mask=0xFFFFFFFF, partial=0)
[L0] Service state changed to SCANNING
[L0] Starting filesystem rescan...
[L0] Service state changed back to READY after rescan
[L0] Database snapshot published after rescan
```

**Test Mode:**

When `NCD_TEST_MODE=1` is set, the service skips rescans to prevent scanning user drives during testing:
```
[L0] Rescan skipped: NCD_TEST_MODE is set
```

### IPC Diagnostic Tools

The NCD distribution includes standalone command-line tools for testing and debugging the service IPC protocol. These tools do not require the full NCD client and can be used for diagnostics, load testing, and protocol validation.

**Available Tools:**

| Tool | Purpose |
|------|---------|
| `ipc_ping_test` | Test service liveness and measure latency |
| `ipc_state_test` | Query service state and version info |
| `ipc_metadata_test` | Submit metadata updates (groups, exclusions, config) |
| `ipc_heuristic_test` | Submit heuristic search mappings |
| `ipc_rescan_test` | Request filesystem rescans |
| `ipc_flush_test` | Request immediate persistence to disk |
| `ipc_shutdown_test` | Request graceful service shutdown |
| `ipc_fuzzer` | Fuzz test the IPC protocol |
| `ipc_stress_test` | Load test with concurrent connections |
| `ipc_cli` | Interactive CLI for manual testing |

**Usage Examples:**

```bash
# Check if service is reachable
ipc_ping_test --once

# Continuous ping with latency stats
ipc_ping_test --continuous --interval 1000

# Get service state information
ipc_state_test --info

# Get JSON-formatted state
ipc_state_test --info --json

# Add a group via IPC
ipc_metadata_test --group-add @projects /home/user/projects

# Submit a heuristic mapping
ipc_heuristic_test --search "downloads" --path "/home/user/Downloads"

# Request full rescan
ipc_rescan_test --full

# Request rescan and wait for completion
ipc_rescan_test --full --wait --timeout 60

# Force immediate flush to disk
ipc_flush_test --verify

# Graceful shutdown
ipc_shutdown_test --timeout 10 --verify

# Fuzz test with bit flipping
ipc_fuzzer --bitflip --count 10000

# Stress test with 100 concurrent connections
ipc_stress_test --connections 100 --duration 60 --operation ping

# Interactive CLI
ipc_cli
> ping
> state
> version
> quit
```

**Exit Codes:**

All IPC tools use consistent exit codes:
- `0` - Success / Service running / Operation completed
- `1` - Service not running / Connection failed
- `2` - Timeout / Service busy
- `3` - Invalid parameter / Invalid response

**Build Location:**

The tools are built in the `test/` directory:
- Windows: `test\ipc_ping_test.exe`, etc.
- Linux: `test/ipc_ping_test`, etc.

## Shared Memory Architecture

NCD uses a platform-native shared memory (SHM) architecture for zero-copy state sharing between the resident service and clients.

### Design Principles

1. **No hardcoded offsets** - All offsets calculated using `sizeof()` and `offsetof()`
2. **Variable mount points** - Not limited to 26 drive letters; supports any mount path
3. **Platform-native text encoding**:
   - **Windows**: UTF-16 (`wchar_t`) - No conversion, full international character support
   - **Linux**: UTF-8 (`char`) - Native format
4. **Zero-copy client access** - Clients map SHM directly, no data copying

### Platform Abstraction Types

```c
/* src/shm_types.h */

#ifdef NCD_PLATFORM_WINDOWS
/* Windows: UTF-16 (WCHAR) for all paths */
typedef wchar_t NcdShmChar;
#define NCD_SHM_PATH_MAX NCD_MAX_PATH
#define NCD_SHM_TEXT_ENCODING NCD_TEXT_UTF16LE
#define shm_strlen wcslen
#define shm_strcpy wcscpy
#define shm_snprintf snwprintf
#define SHM_PATH_FMT "%ls"
#else
/* Linux: UTF-8 (char) for all paths */
typedef char NcdShmChar;
#define NCD_SHM_PATH_MAX NCD_MAX_PATH
#define NCD_SHM_TEXT_ENCODING NCD_TEXT_UTF8
#define shm_strlen strlen
#define shm_strcpy strcpy
#define shm_snprintf snprintf
#define SHM_PATH_FMT "%s"
#endif
```

### SHM Database Format

```
ShmDatabaseHeader (32 bytes)
  magic = SHM_DATABASE_MAGIC
  version = 1
  mount_count = N
  header_size = sizeof(ShmDatabaseHeader) + N*sizeof(ShmMountEntry)
  text_encoding = NCD_TEXT_UTF16LE or NCD_TEXT_UTF8
  
ShmMountEntry[N] (variable size)
  mount_point[NCD_SHM_PATH_MAX]  - Full mount path in platform encoding
  volume_label[64]               - Human-readable label
  dir_count                      - Number of directories
  dirs_offset                    - Byte offset to DirEntry array
  pool_offset                    - Byte offset to name pool
  pool_size                      - Size of name pool

DirEntry arrays                  - Directory entries
Name pools                       - String data in platform encoding
```

### Key Structures

```c
/* Platform-specific mount entry */
typedef struct {
    NcdShmChar mount_point[NCD_SHM_PATH_MAX];   /* Full mount path */
    NcdShmChar volume_label[64];                /* Human-readable name */
    uint32_t   dir_count;
    uint32_t   dirs_offset;      /* Offset from ShmDatabase start */
    uint32_t   pool_offset;      /* Offset from ShmDatabase start */
    uint32_t   pool_size;
} ShmMountEntry;

/* Database header */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t generation;
    uint64_t checksum;
    uint32_t total_size;
    uint32_t header_size;
    uint32_t mount_count;
    uint32_t text_encoding;      /* NCD_TEXT_UTF8 or NCD_TEXT_UTF16LE */
} ShmDatabaseHeader;
```

### Size Calculation (No Hardcoded Offsets)

```c
size_t shm_database_compute_size(const NcdDatabase *db) {
    size_t size = sizeof(ShmDatabaseHeader);
    
    /* Mount entry array */
    size += db->drive_count * sizeof(ShmMountEntry);
    
    /* Align before first mount's data */
    size = (size + 7) & ~7;
    
    /* Data for each mount */
    for (int i = 0; i < db->drive_count; i++) {
        DriveData *drv = &db->drives[i];
        
        /* DirEntry array */
        size += drv->dir_count * sizeof(DirEntry);
        
        /* Align before pool */
        size = (size + 7) & ~7;
        
        /* Name pool - size depends on encoding! */
        size += drv->name_pool_len;
    }
    
    return size;
}
```

### Client Access Pattern

```c
/* Map and validate */
int state_backend_open_service(NcdStateView **out, NcdStateSourceInfo *info) {
    /* ... map database SHM ... */
    ShmDatabase *db = shm_map(db_shm);
    
    /* Validate encoding matches platform */
    if (db->hdr.text_encoding != NCD_SHM_TEXT_ENCODING) {
        return -1;  /* Encoding mismatch - service/client version skew */
    }
    
    /* Store pointer - NO COPYING! */
    view->data.service.db = db;
    *out = view;
    return 0;
}
```

### Benefits

| Metric | Before | After |
|--------|--------|-------|
| NcdStateView size | 45,456 bytes | ~48-64 bytes |
| International paths | Corrupted on Windows | Preserved (UTF-16) |
| Path encoding | ANSI code page | UTF-16 (Win) / UTF-8 (Linux) |
| Max path length | 64 chars (label) | 4096 chars (NCD_MAX_PATH) |
| Drive/mount limit | 64 fixed | Variable (FAM) |

## Security Considerations

1. **Path escaping:** Result files escape special characters for batch/shell safety
   - Windows: Replaces `%`, `!`, quotes, control characters
   - Linux: Rejects shell metacharacters (`$`, `` ` ``, `|`, `&`, etc.)
2. **No elevated privileges:** Tool runs as regular user
3. **Database integrity:** Magic number, version checks, and CRC64 checksum on load
4. **Buffer safety:** All string operations use bounded copies
5. **Overflow checking:** Multiplication/addition operations check for overflow
6. **Symlink handling:** On Linux, symlinks are followed; no cycle detection currently
7. **Shared memory:** User-scoped naming prevents cross-user collisions

## Environment Variables

### `NCD_TEST_MODE`
When set to any non-empty value (e.g., `NCD_TEST_MODE=1`), disables automatic background rescans. This is used by the test suite to prevent tests from scanning user drives when the database is stale.

**Usage:**
```bash
# Disable background rescans during testing
export NCD_TEST_MODE=1
ncd some_search
```

**Note:** When running NCD manually (not through tests), the auto-rescan feature may trigger a full system scan (`ncd -r`) if the database is older than the configured interval (default: -1/disabled). To enable auto-rescan, use `ncd -c rescan_interval_hours=24`. To ensure it stays disabled, either:
- Set `NCD_TEST_MODE=1` before running NCD
- Use `ncd -c rescan_interval_hours=-1` to disable auto-rescan permanently

## Known Limitations

1. No symlink cycle detection on Linux
2. Some CLI and filesystem paths still pass through ANSI-oriented code paths, but the database and shared-memory formats support UTF-8/UTF-16 text encoding
3. Directory history limited to 9 entries (NCD_DIR_HISTORY_MAX); frequent search history limited to 100 entries (NCD_HEUR_MAX_ENTRIES)
4. Database refresh triggered manually, by configurable interval, or disabled
5. No network drive support on Linux (only local filesystems)
6. Requires external `../shared/` library for building

## File Descriptions

| File | Description |
|------|-------------|
| `src/ncd.h` | Core types, platform detection, limits, utility macros, binary format headers |
| `src/main.c` | CLI parsing, heuristics (history), result generation, main loop, version info |
| `src/database.c` | Binary DB load/save, path helpers, groups, config, metadata, exclusions, history |
| `src/scanner.c` | Multi-threaded filesystem scanning, mount enumeration, exclusion filtering |
| `src/matcher.c` | Chain matching, name index, fuzzy matching with Damerau-Levenshtein |
| `src/ui.c` | TUI implementation (selector with live filter, navigator, history browser, dialogs) |
| `src/platform.c` | NCD-specific platform wrappers (delegates to ../shared/) |
| `src/strbuilder.h` | Compatibility shim to shared StringBuilder API |
| `../shared/strbuilder.c` | Dynamic string builder with JSON escaping |
| `../shared/common.c` | Memory allocation wrappers with OOM handling |
| `src/state_backend.h` | State access abstraction interface |
| `src/state_backend_local.c` | Local disk state implementation |
| `src/state_backend_service.c` | Service-backed state via shared memory |
| `src/shared_state.h/.c` | Shared memory snapshot format and validation |
| `src/shm_platform.h` | Shared memory platform abstraction |
| `src/control_ipc.h` | IPC message protocol for service/client |
| `src/service_main.c` | Service executable entry point |
| `src/service_state.c/.h` | Service state management and dirty tracking |
| `src/service_publish.c/.h` | Snapshot publication and generation management |
| `test/test_framework.h` | Minimal unit testing framework macros |
| `test/test_database.c` | Database module unit tests |
| `test/test_matcher.c` | Matcher module unit tests |
| `test/test_shared_state.c` | Shared state validation tests |
| `test/test_service_lazy_load.c` | Service lazy loading and state machine tests |
| `test/test_service_parity.c` | Service vs standalone parity tests |
| `test/test_service_lifecycle.c` | Service start/stop/restart lifecycle tests |
| `test/test_service_integration.c` | NCD client service status integration tests |
| `test/test_service_database.c` | Service database loading and query tests |
| `test/test_service_ipc.c` | Service IPC communication protocol tests |

## Version History

- Current binary version: 3 (added encoding field and UTF-16 support)
- Current metadata format version: 1 (container format)
- Current heuristics version: 1
- Current config version: 4 (added text_encoding field)
- Current build version: 1.3

Note: The metadata file (ncd.metadata) has its own format version (1), while the
config section inside it uses config version 4. These are independent version
numbers for different layers of the data structure.

When changing database format:
1. Increment `NCD_BIN_VERSION` in `ncd.h`
2. Update `BinFileHdr` structure if needed
3. Ensure backward compatibility or provide migration path
4. Update CRC64 calculation if header changes

### Config Format Changes

**Version 3** - Added `rescan_interval_hours` field to support configurable auto-rescan intervals (1-168 hours, or -1 for never).

**Version 4** - Added `text_encoding` to the config and database path/metadata pipeline.


---

## Knowledge Graph (Graphify)

This project has a pre-built knowledge graph in `graphify-out/` that maps the codebase structure, cross-module dependencies, and semantic communities.

### Graph Summary
- **2353 nodes, 7518 edges, 84 communities**
- Built from 196 files (~249K words)
- **God nodes**: `db_free()`, `db_create()`, `ipc_client_disconnect()`, `db_add_dir()`, `ipc_client_connect()`, `db_add_drive()`, `db_metadata_free()`, `ui_inject_keys()`, `ui_set_io_backend()`, `run_test()`
- **Top communities**: Database Core Operations, IPC Client Protocol, TUI Key Injection Tests, Config & Heuristics, Agent CLI Extended Tests, Shared Memory Snapshots, IPC Server Protocol, Argument Parsing & Glob Matching, Agent Mode Parsing, State Backend Management

### Outputs Location
```
graphify-out/
├── graph.html        # Interactive visualization (open in browser)
├── GRAPH_REPORT.md   # Full audit report with communities, god nodes, surprises
└── graph.json        # Queryable graph data (NetworkX node_link_data)
```

### How to Use the Graph

**Before answering architecture questions or searching raw files**, read `graphify-out/GRAPH_REPORT.md` for:
1. **God Nodes** - the highest-degree hubs at the heart of the system
2. **Community structure** - which modules form natural clusters
3. **Surprising Connections** - unexpected cross-file dependencies

**Query the graph programmatically:**
```powershell
# Find shortest path between two concepts
graphify path "db_create" "ipc_client_connect"

# Explain a node and its neighbors
graphify explain "db_free"

# BFS query for broad context
graphify query "how does the service publish snapshots"

# DFS query to trace a specific chain
graphify query "state backend to shared memory" --dfs
```

**Update the graph after code changes:**
```powershell
# Incremental update (code-only changes, no LLM cost)
graphify update .

# Full rebuild (if docs/semantic content changed)
# Re-run detection + extraction + build pipeline via graphify Python API
```

### Graphify Integration for Kimi

Graphify is installed (`pip install graphifyy`). The skill is not officially available for Kimi yet, but the graph outputs are always-on via this AGENTS.md section. When the user asks about:
- Code architecture or module relationships
- "How does X connect to Y"
- "What are the main components"
- "Explain the structure of..."

**Read `graphify-out/GRAPH_REPORT.md` first** before using Glob/Grep, as it provides a structured map of the system.

### .graphifyignore
A `.graphifyignore` file exists in the project root to exclude build artifacts, `.git/`, log files, and temporary outputs from future graph builds.
