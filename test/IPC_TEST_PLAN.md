# NCD Service IPC Test Programs - Implementation Plan

## Overview

Create standalone test executables that test each IPC request type in isolation. These are **not** unit tests - they are diagnostic tools for testing the service IPC protocol directly without involving the full NCD client.

These tools will be useful for:
- Debugging IPC protocol issues
- Testing service responses under specific conditions
- Validating service behavior without NCD client complexity
- Load testing specific IPC operations

## Existing Test Coverage

| Existing File | Type | Coverage |
|---------------|------|----------|
| `test_ipc.c` | Unit tests | Low-level IPC client/server APIs, message formatting |
| `test_service_ipc.c` | Integration tests | Full client-service communication, all message types |
| `test_service_lifecycle.c` | Integration tests | Service start/stop, state transitions |

**Gap:** No standalone command-line tools for manual IPC testing and diagnostics.

---

## Proposed Test Programs

### 1. `ipc_ping_test` (Windows: `ipc_ping_test.exe`)

**Purpose:** Test basic service liveness via PING requests.

**Operations Tested:**
- `NCD_MSG_PING` - Liveness check

**Features:**
- Single ping: `ipc_ping_test --once`
- Continuous ping: `ipc_ping_test --continuous [--interval <ms>]`
- Latency measurement: Reports round-trip time in microseconds
- Connection timeout testing: `ipc_ping_test --timeout <ms>`
- Batch ping: `ipc_ping_test --count <n>` for statistics

**Exit Codes:**
- 0: All pings successful
- 1: Service not running
- 2: Ping timeout
- 3: Invalid response

**Example Output:**
```
$ ./ipc_ping_test --count 5
NCD Service IPC Ping Test
=========================
Service: RUNNING
Ping 1: 0.42 ms
Ping 2: 0.38 ms
Ping 3: 0.45 ms
Ping 4: 0.41 ms
Ping 5: 0.39 ms
-------------------------
Min: 0.38 ms
Max: 0.45 ms
Avg: 0.41 ms
Result: PASS
```

---

### 2. `ipc_state_test` (Windows: `ipc_state_test.exe`)

**Purpose:** Test state information retrieval.

**Operations Tested:**
- `NCD_MSG_GET_STATE_INFO` - Get generations and shared memory names
- `NCD_MSG_GET_VERSION` - Get service version info

**Features:**
- Display state info: `ipc_state_test --info`
- Display version: `ipc_state_test --version`
- Display all: `ipc_state_test --all`
- Watch mode (poll for changes): `ipc_state_test --watch [--interval <ms>]`
- JSON output: `ipc_state_test --json`

**Information Displayed:**
- Protocol version
- Text encoding (UTF-8 or UTF-16LE)
- Metadata generation
- Database generation
- Metadata snapshot size
- Database snapshot size
- Shared memory object names
- Application version
- Build timestamp

**Exit Codes:**
- 0: Success
- 1: Service not running
- 2: Invalid response

---

### 3. `ipc_metadata_test` (Windows: `ipc_metadata_test.exe`)

**Purpose:** Test metadata update operations.

**Operations Tested:**
- `NCD_MSG_SUBMIT_METADATA` - All metadata update types

**Update Types Supported:**
| Type | Command |
|------|---------|
| Group Add | `ipc_metadata_test --group-add @name /path` |
| Group Remove | `ipc_metadata_test --group-remove @name` |
| Group Remove Path | `ipc_metadata_test --group-remove-path @name /path` |
| Exclusion Add | `ipc_metadata_test --exclusion-add pattern` |
| Exclusion Remove | `ipc_metadata_test --exclusion-remove pattern` |
| Config Update | `ipc_metadata_test --config key=value` |
| Clear History | `ipc_metadata_test --clear-history` |
| Dir History Add | `ipc_metadata_test --history-add /path` |
| Dir History Remove | `ipc_metadata_test --history-remove <index>` |
| Dir History Swap | `ipc_metadata_test --history-swap` |
| Encoding Switch | `ipc_metadata_test --encoding utf8\|utf16` |

**Features:**
- Dry-run mode: `ipc_metadata_test --dry-run ...`
- Verbose protocol dump: `ipc_metadata_test --verbose ...`
- Verify persistence: `ipc_metadata_test --verify ...`

**Exit Codes:**
- 0: Update accepted (may be queued)
- 1: Service not running
- 2: Service busy (loading/scanning)
- 3: Invalid parameter
- 4: Update rejected

---

### 4. `ipc_heuristic_test` (Windows: `ipc_heuristic_test.exe`)

**Purpose:** Test heuristic update submissions.

**Operations Tested:**
- `NCD_MSG_SUBMIT_HEURISTIC` - Submit search→path mappings

**Features:**
- Single submission: `ipc_heuristic_test --search "term" --path "/result/path"`
- Batch from file: `ipc_heuristic_test --file heuristics.txt`
- File format: `search_term|target_path` (one per line)
- Performance test: `ipc_heuristic_test --perf-test --count <n>`
- Stress test: `ipc_heuristic_test --stress --duration <sec> --threads <n>`

**Exit Codes:**
- 0: Heuristic submitted successfully
- 1: Service not running
- 2: Service busy
- 3: Invalid parameters

**Example:**
```
$ ./ipc_heuristic_test --search "downloads" --path "/home/user/Downloads"
Submitting heuristic: "downloads" -> "/home/user/Downloads"
Result: ACCEPTED (queued for processing)
Exit code: 0
```

---

### 5. `ipc_rescan_test` (Windows: `ipc_rescan_test.exe`)

**Purpose:** Test database rescan requests.

**Operations Tested:**
- `NCD_MSG_REQUEST_RESCAN` - Request filesystem rescan

**Features:**
- Full rescan: `ipc_rescan_test --full`
- Drive-specific: `ipc_rescan_test --drive C` or `ipc_rescan_test --drives C,D,E`
- Subdirectory scan: `ipc_rescan_test --path /specific/directory`
- Wait for completion: `ipc_rescan_test --full --wait [--timeout <sec>]`
- Check status: `ipc_rescan_test --status`

**Exit Codes:**
- 0: Rescan request accepted
- 1: Service not running
- 2: Service busy (already scanning)
- 3: Invalid drive/path
- 4: Timeout (with --wait)

---

### 6. `ipc_flush_test` (Windows: `ipc_flush_test.exe`)

**Purpose:** Test immediate persistence requests.

**Operations Tested:**
- `NCD_MSG_REQUEST_FLUSH` - Force immediate save to disk

**Features:**
- Simple flush: `ipc_flush_test`
- With verification: `ipc_flush_test --verify` (checks files exist after)
- Measure time: `ipc_flush_test --timing` (reports flush duration)
- Force mode: `ipc_flush_test --force` (wait even if busy)

**Exit Codes:**
- 0: Flush completed successfully
- 1: Service not running
- 2: Flush failed
- 3: Timeout

---

### 7. `ipc_shutdown_test` (Windows: `ipc_shutdown_test.exe`)

**Purpose:** Test graceful shutdown requests.

**Operations Tested:**
- `NCD_MSG_REQUEST_SHUTDOWN` - Request service shutdown

**Features:**
- Graceful shutdown: `ipc_shutdown_test`
- With timeout: `ipc_shutdown_test --timeout <sec>`
- Force if needed: `ipc_shutdown_test --force`
- Verify stopped: `ipc_shutdown_test --verify`

**Exit Codes:**
- 0: Shutdown requested successfully
- 1: Service not running
- 2: Shutdown request failed
- 3: Service didn't stop (timeout)

---

### 8. `ipc_fuzzer` (Windows: `ipc_fuzzer.exe`)

**Purpose:** Fuzz test the IPC protocol for robustness.

**Operations Tested:**
- All message types with various payload mutations

**Features:**
- Random message types: `ipc_fuzzer --random-type`
- Random payload sizes: `ipc_fuzzer --random-size --max <bytes>`
- Bit flipping: `ipc_fuzzer --bitflip --count <n>`
- Boundary values: `ipc_fuzzer --boundaries`
- Specific target: `ipc_fuzzer --target <msg_type>`

**Exit Codes:**
- 0: Service handled all fuzzing without crash
- 1: Service crashed (detected by ping failure)
- 2: Service returned unexpected errors

---

### 9. `ipc_stress_test` (Windows: `ipc_stress_test.exe`)

**Purpose:** Load test the service IPC under heavy concurrency.

**Operations Tested:**
- All operations under concurrent load

**Features:**
- Concurrent connections: `ipc_stress_test --connections <n>`
- Request rate: `ipc_stress_test --rate <req/sec>`
- Duration: `ipc_stress_test --duration <sec>`
- Specific operation: `ipc_stress_test --operation <ping|state|heuristic|...>`
- Mixed operations: `ipc_stress_test --mixed`

**Metrics Reported:**
- Requests per second
- Average latency
- P99 latency
- Error rate
- Service availability

---

### 10. `ipc_cli` (Windows: `ipc_cli.exe`)

**Purpose:** Interactive CLI for manual IPC testing.

**Features:**
- Interactive prompt: `ipc_cli`
- Commands: `ping`, `state`, `version`, `heuristic`, `metadata`, `rescan`, `flush`, `shutdown`, `quit`
- Tab completion for commands
- History
- Verbose mode showing raw message bytes

**Example Session:**
```
$ ./ipc_cli
NCD IPC CLI v1.0
Type 'help' for commands, 'quit' to exit.

> ping
PING: OK (0.45 ms)

> state
PROTOCOL: 3
ENCODING: UTF-8
META_GEN: 12345
DB_GEN: 12346
META_SIZE: 4096
DB_SIZE: 1048576
META_SHM: /ncd_meta_abc123
DB_SHM: /ncd_db_def456

> heuristic downloads /home/user/Downloads
HEURISTIC: ACCEPTED

> quit
Goodbye.
```

---

## Implementation Notes

### Common Infrastructure

All test programs should share:
- Common argument parsing
- IPC client connection setup/teardown
- Error message formatting
- Exit code conventions
- JSON output formatting (optional)

### Platform Differences

| Feature | Windows | Linux/WSL |
|---------|---------|-----------|
| Named pipes | `\\.\pipe\NCDService` | Unix socket `/tmp/ncd_service.sock` |
| Process check | Named pipe exists | Socket file exists + PID check |
| Build | MSVC/MinGW | GCC/Clang |
| Executable | `.exe` | no extension |

### Build Integration

Add to `test/Makefile`:
```makefile
IPC_TESTS = ipc_ping_test ipc_state_test ipc_metadata_test \
            ipc_heuristic_test ipc_rescan_test ipc_flush_test \
            ipc_shutdown_test ipc_fuzzer ipc_stress_test ipc_cli

ipc-tests: $(IPC_TESTS)

$(IPC_TESTS): %: %.c $(IPC_OBJS)
    $(CC) $(CFLAGS) -o $@ $< $(IPC_OBJS) $(LIBS)
```

Add to `test/build-tests.bat`:
```batch
:: IPC diagnostic tools
echo Building ipc_ping_test.exe...
cl %CFLAGS% ipc_ping_test.c ...
...
```

### Testing Matrix

| Program | Basic | Busy | Not Running | Invalid Params |
|---------|-------|------|-------------|----------------|
| ipc_ping_test | X | X | X | - |
| ipc_state_test | X | X | X | - |
| ipc_metadata_test | X | X | X | X |
| ipc_heuristic_test | X | X | X | X |
| ipc_rescan_test | X | X | X | X |
| ipc_flush_test | X | X | X | - |
| ipc_shutdown_test | X | - | X | - |
| ipc_fuzzer | X | - | X | - |
| ipc_stress_test | X | X | X | - |
| ipc_cli | X | X | X | - |

---

## Priority Order

**Phase 1 (High Priority):**
1. `ipc_ping_test` - Basic connectivity
2. `ipc_state_test` - Information retrieval
3. `ipc_shutdown_test` - Service lifecycle

**Phase 2 (Medium Priority):**
4. `ipc_metadata_test` - Metadata operations
5. `ipc_heuristic_test` - Heuristic submissions
6. `ipc_rescan_test` - Rescan operations

**Phase 3 (Lower Priority):**
7. `ipc_flush_test` - Persistence testing
8. `ipc_cli` - Interactive tool

**Phase 4 (Advanced):**
9. `ipc_fuzzer` - Protocol robustness
10. `ipc_stress_test` - Performance/load testing

---

## Comparison: Existing Tests vs. Proposed Tools

| Aspect | Existing Tests | Proposed Tools |
|--------|---------------|----------------|
| **Type** | Unit & Integration tests | Standalone CLI utilities |
| **Framework** | test_framework.h | Main() function, CLI args |
| **Execution** | Run by test runner | Run manually or in scripts |
| **Output** | PASS/FAIL | Detailed diagnostics |
| **Interaction** | Automated | Interactive (ipc_cli) |
| **Use Case** | CI/CD validation | Development debugging |
| **NCD Client** | May use client APIs | Direct IPC only |
| **Service State** | Managed by test | User manages service |

---

## Success Criteria

Each test program must:
- [ ] Compile on both Windows and Linux/WSL
- [ ] Handle service not running gracefully
- [ ] Provide meaningful exit codes
- [ ] Support `--help` with usage information
- [ ] Include verbose mode for debugging
- [ ] Be runnable standalone without NCD client
- [ ] Not interfere with running NCD client operations

---

## Files to Create

1. `test/ipc_ping_test.c`
2. `test/ipc_state_test.c`
3. `test/ipc_metadata_test.c`
4. `test/ipc_heuristic_test.c`
5. `test/ipc_rescan_test.c`
6. `test/ipc_flush_test.c`
7. `test/ipc_shutdown_test.c`
8. `test/ipc_fuzzer.c`
9. `test/ipc_stress_test.c`
10. `test/ipc_cli.c`
11. `test/ipc_test_common.c` - Shared utilities
12. `test/ipc_test_common.h` - Shared header

Total: **12 new source files** (plus this plan document)

---

## Example Use Cases

### Debugging Service Connectivity
```bash
# Check if service is reachable
$ ./ipc_ping_test --once
Service: RUNNING
Ping: OK (0.42 ms)

# Or continuously monitor
$ ./ipc_ping_test --continuous --interval 1000
```

### Testing Metadata Updates
```bash
# Add a group
$ ./ipc_metadata_test --group-add @projects /home/user/projects
Result: ACCEPTED

# Verify by checking state
$ ./ipc_state_test --info
```

### Performance Testing
```bash
# Stress test with 1000 concurrent connections
$ ./ipc_stress_test --connections 1000 --duration 60 --operation ping

# Or fuzz test
$ ./ipc_fuzzer --bitflip --count 10000
```

### Interactive Debugging
```bash
# Start interactive CLI
$ ./ipc_cli
> help
Available commands:
  ping [count]       - Send ping request(s)
  state              - Get state information
  version            - Get service version
  heuristic <s> <p>  - Submit heuristic
  metadata <type>    - Submit metadata update
  rescan [drive]     - Request rescan
  flush              - Request persistence
  shutdown           - Request service shutdown
  quit               - Exit CLI
> ping
PING: OK
```
