# WSL-Windows Service Bridge

## Overview

When NCD is built and run as a Linux binary inside WSL, it currently operates
in standalone mode or connects to its own Linux daemon via Unix domain sockets.
However, if the Windows NCD service is already running on the host, it has the
entire directory database warm in shared memory. Rescanning `/mnt/c`, `/mnt/d`,
etc. from WSL is redundant and slow (traversing the 9P filesystem bridge).

This plan describes how the Linux NCD client can detect it is running in WSL,
check for a running Windows NCD service, and delegate queries to it -- avoiding
duplicate scanning and leveraging cached data.

---

## Architecture

```
+------------------------------------------------------+
|  Windows Host                                        |
|                                                      |
|  NCD Service (ncd.exe)                               |
|    |-- Named pipe: \\.\pipe\ncd_control              |
|    |-- Shared memory: Local\NCD_<SID>_metadata       |
|    |-- Shared memory: Local\NCD_<SID>_database       |
|    |-- NEW: TCP listener on 127.0.0.1:<port>         |
|    +-- Full directory database in memory             |
|                                                      |
+---------------------------+--------------------------+
                            | TCP loopback / pipe relay
+---------------------------+--------------------------+
|  WSL (Linux VM)                                      |
|                                                      |
|  NCD Client (ncd, Linux build)                       |
|    1. Detect WSL environment                         |
|    2. Try Windows service (TCP or pipe relay)         |
|    3. Fall back to Linux daemon                      |
|    4. Fall back to standalone scan                   |
|                                                      |
+------------------------------------------------------+
```

---

## Phase 1: WSL Detection

### Implementation

Add to `platform.c` (Linux build):

```c
/*
 * platform_is_wsl  --  Detect if running inside WSL
 *
 * Checks /proc/version for "microsoft" (case insensitive).
 * Works for both WSL1 and WSL2.
 *
 * Secondary check: /proc/sys/fs/binfmt_misc/WSLInterop exists.
 */
bool platform_is_wsl(void);
```

Detection methods (check in order, first match wins):

| Method                        | WSL1 | WSL2 | Reliable |
|-------------------------------|------|------|----------|
| `/proc/version` contains "microsoft" (case-insensitive) | Yes | Yes | High |
| `/proc/sys/fs/binfmt_misc/WSLInterop` exists            | Yes | Yes | High |
| `WSL_DISTRO_NAME` env var is set                        | Yes | Yes | Medium |

Recommend: check `/proc/version` first, then `WSLInterop` as fallback.

### Caching

The result should be cached in a static variable since it cannot change during
the process lifetime:

```c
static int g_is_wsl = -1;  /* -1 = unknown, 0 = no, 1 = yes */
```

### Header Addition

```c
/* In platform.h */
#if NCD_PLATFORM_LINUX
bool platform_is_wsl(void);
#else
#define platform_is_wsl() false
#endif
```

---

## Phase 2: Cross-Boundary IPC

### The Shared Memory Problem

Windows and WSL use completely different shared memory mechanisms that
**cannot interoperate**:

| Aspect        | Windows                              | WSL/POSIX                        |
|---------------|--------------------------------------|----------------------------------|
| API           | `CreateFileMapping`/`MapViewOfFile`   | `shm_open`/`mmap`               |
| Namespace     | `Local\NCD_<SID>_metadata`           | `/ncd_<uid>_metadata`            |
| Backing       | Windows kernel paging file           | `/dev/shm` (Linux tmpfs)         |
| Kernel        | NT kernel objects                    | Linux kernel (real VM on WSL2)   |

Even on WSL1 (syscall translation), POSIX `shm_open()` maps to different
backing storage than `CreateFileMapping()`. On WSL2 it is a completely
separate VM with its own memory space.

**Conclusion**: Shared memory cannot be used across the WSL/Windows boundary.
All data exchange must go over IPC (wire protocol).

### Chosen Approach: IPC Query Protocol

Add a new IPC message type that lets a client send a search query and receive
results over the wire, without needing to map shared memory.

#### New IPC Messages

```c
/* In control_ipc.h - new message types */

NCD_MSG_QUERY_SEARCH = 9,          /* Client -> Server: search query       */
NCD_MSG_QUERY_RESPONSE = 0x89,     /* Server -> Client: search results     */

/*
 * NcdQuerySearchPayload  --  Search query from client
 */
typedef struct {
    uint32_t search_len;            /* Length of search term                 */
    uint32_t max_results;           /* Maximum results to return (0 = all)  */
    uint32_t flags;                 /* Search flags (see below)             */
    uint8_t  path_format;           /* 0 = native, 1 = POSIX /mnt/x style  */
    uint8_t  reserved[3];
    /* Followed by: search_term \0 */
} NcdQuerySearchPayload;

/* Search flags */
#define NCD_QUERY_FLAG_FUZZY        0x0001  /* Enable fuzzy matching        */
#define NCD_QUERY_FLAG_SHOW_HIDDEN  0x0002  /* Include hidden directories   */
#define NCD_QUERY_FLAG_SHOW_SYSTEM  0x0004  /* Include system directories   */
#define NCD_QUERY_FLAG_USE_HEUR     0x0008  /* Apply heuristic boosting     */

/*
 * NcdQueryResultEntry  --  Single search result
 *
 * Variable-length. Each entry is:
 *   [NcdQueryResultEntry header] [path bytes \0]
 */
typedef struct {
    uint32_t path_len;              /* Length of path string (including \0)  */
    int32_t  score;                 /* Match score (higher = better)        */
    char     drive_letter;          /* Original drive letter                */
    uint8_t  is_hidden;
    uint8_t  is_system;
    uint8_t  pad;
} NcdQueryResultEntry;              /* 12 bytes, followed by path string    */

/*
 * NcdQueryResponsePayload  --  Search results
 */
typedef struct {
    uint32_t result_count;          /* Number of results                    */
    uint32_t total_payload_len;     /* Total bytes following this header    */
    /* Followed by: result_count x [NcdQueryResultEntry + path \0]         */
} NcdQueryResponsePayload;
```

### Transport Options

#### Option A: TCP Loopback (Recommended)

WSL2 shares the network stack with the Windows host. A TCP connection to
`127.0.0.1` from WSL reaches Windows.

```
Windows service:  listen(127.0.0.1, <port>)
WSL client:       connect(127.0.0.1, <port>)
```

**Port selection**: Use a deterministic port derived from the user's SID or
a fixed well-known port (e.g., 47632). Alternatively, write the port to a
file at a known path (e.g., `%LOCALAPPDATA%\ncd\service.port`) that WSL can
read via `/mnt/c/Users/<user>/AppData/Local/ncd/service.port`.

Implementation:

- Add `control_ipc_tcp.c` with TCP socket client/server.
- Reuse the same `NcdIpcHeader` wire format -- just different transport.
- Service opens **both** named pipe (for Windows clients) and TCP socket
  (for WSL clients).

#### Option B: Named Pipe Relay via npiperelay

Use `npiperelay.exe` + `socat` to bridge a Unix domain socket in WSL to the
Windows named pipe. More fragile, requires extra tools.

**Not recommended** -- too many moving parts.

#### Option C: File-based Port Discovery + TCP

Hybrid of A: service writes port to a file, WSL client reads it.

```
Windows service writes:  %LOCALAPPDATA%\ncd\service.port
                         containing: "47632\n"

WSL client reads:        /mnt/c/Users/<user>/AppData/Local/ncd/service.port
```

This is the recommended discovery mechanism for Option A.

### Service-Side Changes

The Windows NCD service (`service_main.c`) needs to:

1. Open a TCP listener on loopback in addition to the named pipe.
2. Accept connections on both transports in its event loop.
3. Handle `NCD_MSG_QUERY_SEARCH` by running the matcher against the
   in-memory database and returning results.
4. When `path_format == 1` (POSIX), translate paths before sending
   (see Phase 3 below).

### Client-Side Changes

The Linux NCD client startup sequence becomes:

```
main()
  |
  +-- platform_is_wsl()?
  |     |
  |     +-- YES: try_connect_windows_service()
  |     |     |
  |     |     +-- read /mnt/c/.../ncd/service.port
  |     |     +-- TCP connect 127.0.0.1:<port>
  |     |     +-- ipc_client_ping()
  |     |     +-- SUCCESS: use Windows service via TCP IPC
  |     |     +-- FAIL: fall through
  |     |
  |     +-- NO: skip Windows service check
  |
  +-- ipc_service_exists()?  (check Linux daemon)
  |     +-- YES: use Linux daemon via Unix socket
  |     +-- NO: fall through
  |
  +-- standalone mode (scan locally)
```

New function needed:

```c
/*
 * ipc_wsl_try_windows_service  --  Attempt to connect to Windows NCD service
 *
 * Looks for service.port file via /mnt/c, connects via TCP loopback.
 * Returns IPC client handle on success, NULL if not available.
 *
 * The returned client uses TCP transport but speaks the same IPC protocol.
 */
NcdIpcClient *ipc_wsl_try_windows_service(void);
```

---

## Phase 3: Path Translation

This is the most nuanced part. Paths stored in the Windows service database
use Windows conventions. The WSL client needs Linux paths. Translation must
be bidirectional:

- **Results from Windows service -> WSL client**: Windows paths to POSIX paths
- **Heuristic submissions from WSL client -> Windows service**: POSIX paths to Windows paths

### Translation Rules

#### Windows to POSIX (results coming back to WSL)

| Windows Path             | POSIX Path              | Rule                          |
|--------------------------|-------------------------|-------------------------------|
| `C:\Users\Dan\code`     | `/mnt/c/Users/Dan/code` | Drive letter -> `/mnt/<lower>`|
| `D:\Projects`           | `/mnt/d/Projects`       | Drive letter -> `/mnt/<lower>`|
| `\\server\share\dir`    | (not translatable)      | UNC paths: skip or warn       |
| `C:\`                   | `/mnt/c`                | Root of drive                 |

#### POSIX to Windows (heuristic data going to service)

| POSIX Path                | Windows Path            | Rule                          |
|---------------------------|-------------------------|-------------------------------|
| `/mnt/c/Users/Dan/code`  | `C:\Users\Dan\code`    | `/mnt/<x>/` -> `<X>:\`       |
| `/mnt/d/Projects`        | `D:\Projects`          | `/mnt/<x>/` -> `<X>:\`       |
| `/home/dan/work`         | (not translatable)      | Pure Linux path: no mapping   |
| `/usr/local/bin`         | (not translatable)      | Pure Linux path: no mapping   |

### Separator Handling

| Context                    | Separator | Notes                              |
|----------------------------|-----------|-------------------------------------|
| Windows DB storage         | `\`       | All paths use backslash             |
| NCD IPC wire format        | `\`       | Preserved as stored                 |
| WSL client display/output  | `/`       | Must convert for shell `cd` to work |
| WSL client input           | `/` or `\`| Already handled by matcher          |

### Implementation

```c
/* New file: path_translate.h / path_translate.c */

/*
 * path_win_to_posix  --  Convert Windows path to POSIX/WSL path
 *
 * "C:\Users\Dan\code" -> "/mnt/c/Users/Dan/code"
 *
 * Rules:
 *   1. If path starts with X:\ or X:/, extract drive letter
 *   2. Construct /mnt/<lowercase_drive>/
 *   3. Append remainder, converting all \ to /
 *   4. Remove trailing separator if present (unless root)
 *
 * Returns: true on success, false if path is not translatable
 *          (UNC paths, relative paths, etc.)
 */
bool path_win_to_posix(const char *win_path, char *out_buf, size_t buf_size);

/*
 * path_posix_to_win  --  Convert POSIX/WSL path to Windows path
 *
 * "/mnt/c/Users/Dan/code" -> "C:\Users\Dan\code"
 *
 * Rules:
 *   1. Must start with /mnt/<single_letter>/
 *   2. Extract drive letter, uppercase it
 *   3. Construct X:\
 *   4. Append remainder, converting all / to \
 *
 * Returns: true on success, false if path is not under /mnt/<x>/
 */
bool path_posix_to_win(const char *posix_path, char *out_buf, size_t buf_size);

/*
 * path_normalize_separators  --  Convert separators for target platform
 *
 * Converts all \ to / (POSIX) or all / to \ (Windows).
 * Operates in-place.
 */
void path_normalize_separators(char *path, char target_sep);

/*
 * path_is_wsl_windows_mount  --  Check if path is a WSL Windows mount
 *
 * Returns true for paths like /mnt/c/..., /mnt/d/...
 * Returns false for /mnt/wsl/..., /home/..., etc.
 */
bool path_is_wsl_windows_mount(const char *path);
```

### Where Translation Happens

Translation should be done at the **IPC boundary**, not scattered through
the codebase. Two integration points:

1. **In `NCD_MSG_QUERY_SEARCH` response handling** (client side):
   When the WSL client receives results from the Windows service, it
   translates each path before returning to the caller.

2. **In `NCD_MSG_SUBMIT_HEURISTIC` sending** (client side):
   When the WSL client sends a heuristic update (user selected `/mnt/c/foo`),
   it translates to `C:\foo` before sending to the Windows service.

The **service never needs to know about POSIX paths**. Translation is entirely
a client-side concern. This is controlled by the `path_format` field in
`NcdQuerySearchPayload`:
- `path_format = 0`: server returns native Windows paths (for Windows clients)
- `path_format = 1`: server returns native Windows paths, **client** translates

Actually, it is cleaner to always have the **client translate**, since the
server shouldn't need to know about `/mnt/c`. The `path_format` field can be
removed in favor of the client always translating results when it knows it's
in WSL mode.

### Edge Cases

| Case                              | Handling                                         |
|-----------------------------------|--------------------------------------------------|
| Non-standard mount points         | Check `/proc/mounts` for drvfs mount locations   |
| `/mnt` prefix changed by user     | Read `/etc/wsl.conf` `[automount] root =` value  |
| UNC paths in results              | Skip, don't display (or display with warning)    |
| Subst/junction targets            | Pass through, may not be valid in WSL             |
| Long paths (> 260 chars)          | Preserve as-is, Linux has no limit                |
| Case sensitivity                  | Windows is case-insensitive, Linux is sensitive.  |
|                                   | WSL drvfs mounts are case-insensitive by default  |
|                                   | so this is usually not a problem for /mnt/x paths |

### Custom Mount Point Detection

WSL allows changing the automount root via `/etc/wsl.conf`:

```ini
[automount]
root = /windrives/
```

This would make drives appear at `/windrives/c/` instead of `/mnt/c/`.

```c
/*
 * wsl_get_automount_root  --  Get WSL automount prefix
 *
 * Reads /etc/wsl.conf [automount] root= setting.
 * Returns "/mnt" by default if not configured.
 */
const char *wsl_get_automount_root(void);
```

This must be read once at startup and cached. All translation functions
use this value instead of hardcoding `/mnt`.

---

## Phase 4: Integration with Existing Code

### Modified Service Startup (service_main.c)

```c
/* In service_main.c init sequence */

/* Existing: named pipe listener for Windows clients */
ipc_server = ipc_server_init();

/* NEW: TCP listener for WSL clients */
tcp_server = ipc_tcp_server_init(NCD_WSL_BRIDGE_PORT);

/* Event loop handles both */
while (running) {
    /* Poll named pipe and TCP socket */
    ...
}
```

### Modified Client Startup (main.c)

The existing `ipc_service_exists()` check becomes a three-tier check:

```c
static NcdIpcClient *connect_best_service(void)
{
    /* Tier 1: WSL -> Windows service (if in WSL) */
    if (platform_is_wsl()) {
        NcdIpcClient *client = ipc_wsl_try_windows_service();
        if (client) {
            /* Verify version compatibility */
            NcdIpcVersionCheckResult ver;
            if (ipc_client_check_version(client, NCD_BUILD_VER,
                                         NCD_BUILD_STAMP, &ver) == NCD_IPC_OK
                && ver.versions_match) {
                return client;  /* Use Windows service */
            }
            ipc_client_disconnect(client);
        }
    }

    /* Tier 2: Linux daemon (Unix socket) */
    if (ipc_service_exists()) {
        NcdIpcClient *client = ipc_client_connect();
        if (client) return client;
    }

    /* Tier 3: standalone mode */
    return NULL;
}
```

### Modified result_ok (main.c)

When in WSL bridge mode, `result_ok()` receives translated POSIX paths,
so the existing Linux codepath (which does not prepend `drive:`) works
as-is. No change needed here.

### Heuristic Feedback

When the user selects a result and NCD submits a heuristic update, the
path must be translated back to Windows format before sending:

```c
/* In the heuristic submission path */
if (using_wsl_bridge) {
    char win_path[MAX_PATH];
    if (path_posix_to_win(selected_path, win_path, sizeof(win_path))) {
        ipc_client_submit_heuristic(client, search_term, win_path);
    }
} else {
    ipc_client_submit_heuristic(client, search_term, selected_path);
}
```

---

## Phase 5: Server-Side Query Handler

The Windows service needs a handler for `NCD_MSG_QUERY_SEARCH`. This runs
the existing matcher logic against the in-memory database:

```c
/* In service message handler */
case NCD_MSG_QUERY_SEARCH: {
    NcdQuerySearchPayload *q = (NcdQuerySearchPayload *)payload;
    const char *search = (const char *)(q + 1);

    /* Run matcher against current database snapshot */
    MatchResult results[MAX_RESULTS];
    int count = matcher_search(
        &g_database,
        &g_metadata,
        search,
        results,
        q->max_results ? q->max_results : 20,
        q->flags
    );

    /* Build response with NcdQueryResultEntry array */
    /* ... serialize results into wire format ... */

    ipc_server_send_response(conn, hdr->sequence,
                             response_buf, response_len);
    break;
}
```

This is essentially what `main.c` does locally today, but factored into
a function the service can call on behalf of a remote client.

---

## File Inventory

### New Files

| File                        | Purpose                                    |
|-----------------------------|--------------------------------------------|
| `src/path_translate.h`     | Path translation API declarations          |
| `src/path_translate.c`     | Win<->POSIX path conversion functions      |
| `src/control_ipc_tcp.c`    | TCP transport for IPC (used by WSL bridge) |
| `test/test_path_translate.c`| Unit tests for path translation           |
| `test/test_ipc_tcp.c`      | Unit tests for TCP IPC transport           |

### Modified Files

| File                        | Change                                     |
|-----------------------------|--------------------------------------------|
| `src/platform.h`           | Add `platform_is_wsl()` declaration        |
| `src/platform.c`           | Implement WSL detection                    |
| `src/control_ipc.h`        | Add `NCD_MSG_QUERY_SEARCH` message types   |
| `src/control_ipc.h`        | Add `ipc_wsl_try_windows_service()` decl   |
| `src/service_main.c`       | Add TCP listener + query search handler    |
| `src/main.c`               | Three-tier service connection logic         |
| `src/Makefile` (Linux)     | Add new source files                       |
| `src/ncd.vcxproj` (Windows)| Add new source files                       |

---

## Testing Strategy

### Unit Tests

1. **path_translate tests** (can run on any platform):
   - `C:\Users\Dan` -> `/mnt/c/Users/Dan` and back
   - `D:\` -> `/mnt/d` and back
   - `/home/dan` -> returns false (not translatable)
   - UNC paths -> returns false
   - Custom mount root (simulate `/windrives/c/`)
   - Separator normalization (`\` <-> `/`)
   - Edge cases: empty string, null, paths with spaces, unicode

2. **WSL detection tests** (mock `/proc/version`):
   - Contains "microsoft-standard-WSL2" -> true
   - Contains "Microsoft" (WSL1 style) -> true
   - Generic Linux kernel string -> false

3. **TCP IPC tests** (can run on any platform):
   - Loopback connect/disconnect
   - Send/receive message round-trip
   - Timeout handling
   - Multiple concurrent clients

### Integration Tests (require WSL + Windows)

4. **Cross-boundary query** (manual/CI with WSL):
   - Start Windows NCD service
   - From WSL: connect, query, verify results have `/mnt/c/` prefix
   - From WSL: submit heuristic, verify service received `C:\` path

### What Can Be Tested Without WSL

The path translation and TCP transport are pure logic -- no WSL needed.
Mock the WSL detection for the connection fallback logic.

---

## Implementation Order

1. `path_translate.c` + tests (standalone, no dependencies)
2. `platform_is_wsl()` (small, isolated)
3. `NCD_MSG_QUERY_SEARCH` protocol definition in `control_ipc.h`
4. `control_ipc_tcp.c` TCP transport + tests
5. Service-side query handler in `service_main.c`
6. Client-side WSL bridge logic in `main.c`
7. Integration testing

Steps 1-4 can be done and tested without a working WSL environment.
Steps 5-7 require cross-platform testing.

---

## Open Questions

1. **Port number**: Fixed well-known port vs. dynamic with file-based
   discovery? File-based is more robust but adds a dependency on
   `/mnt/c` being accessible.

2. **Version compatibility**: The WSL Linux client and Windows service may
   be different builds. The existing version check handles this, but should
   the WSL client be able to use a newer/older Windows service?

3. **Exclusions**: If the user has different exclusion lists on Windows vs.
   Linux, which takes precedence when querying the Windows service from WSL?
   Recommendation: use the Windows service's exclusions (since it owns the
   database), but allow the WSL client to add additional exclusions via
   query flags.

4. **Non-Windows drives**: If the WSL environment has native Linux mounts
   (e.g., `/home`, `/opt`) that are not Windows drives, these won't be in
   the Windows service's database. The client may need to fall back to
   local scanning for non-`/mnt/x` paths.  This means a hybrid mode:
   Windows service for `/mnt/c`, `/mnt/d` + local scan for `/home`, etc.
