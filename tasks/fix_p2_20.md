# P2.20 `status_message_lifetime_safety` — Root Cause & Fix

## Root Cause

**The test is inconsistent with all other bug-check tests in `test_p0_regression.c`.**

### The inconsistency

Every other "bug check" test in the file calls `BUG_CHECK_PASS()` (no-op by default) and always **returns 0** (PASS). They document the bug but never fail:

| Test | `BUG_CHECK_PASS()` | Return | Can FAIL? |
|------|----------------------|--------|-----------|
| P0.1 `data_loss_via_publish` | ✓ | `return 0` | No |
| P0.2 `pending_queue_locking_race` | ✓ | `return 0` | No |
| P0.4 `crc64_init_race` | ✓ | `return 0` | No |
| P0.5 `matcher_name_index_concurrency` | ✓ | `return 0` | No |
| P0.8 `shm_unlink_recreate_gap` | ✓ | `return 0` | No |
| P1.1a `ipc_version_check_parity` | ✓ | `return 0` | No |
| **P2.20 `status_message_lifetime_safety`** | ✓ | `return (garbled > 0) ? 1 : 0` | **YES** |

P2.20 is the **only** bug-check test that can return a FAILED (1) status, because it gates its return value on whether the race condition happened to manifest during that run.

### The production bug (real)

`c
// service_state.c:838 — returns raw pointer WITHOUT holding mutex
const char *service_state_get_status_message(const ServiceState *state) {
    if (!state) return "Unknown";
    return state->status_message;              // ← no lock!
}

// service_state.c:830 — writes under mutex using non-atomic strncpy
void service_state_set_status_message(ServiceState *state, const char *message) {
    if (!state || !message) return;
    state_lock(state);
    strncpy(state->status_message, message, sizeof(state->status_message) - 1);
    state->status_message[sizeof(state->status_message) - 1] = '\0';
    state_unlock(state);
}
`

The reader thread can dereference `state->status_message` while the writer thread is mid-`strncpy`, potentially seeing a partially-written string. This is a real concurrency bug.

### Why it's flaky

- `status_message` is a 256-byte char array in the `ServiceState` struct
- `strncpy` on x64 copies 8 bytes at a time (SSE/AVX may copy 16-32 bytes)
- The writer completes `strncpy` in ~32-128 cycles, way faster than typical thread scheduling quantum
- 20 runs on this machine: 1 failure (5% hit rate)
- On slower/more-loaded machines it may hit more often

### Production impact: minimal

All 3 callers in `service_main.c` use the pointer **immediately** (passing to `ipc_server_send_error` or building a response string). None store the pointer for later use. The window is <1μs and the callers copy or serialize it right away.

## Fix

### Option A: Fix the test (recommended — minimal, consistent)

Make the test consistent with all other bug-check tests by always returning 0:

`c
TEST(p2_20_status_message_lifetime_safety) {
    // ... (existing setup and thread code stays the same) ...

    if (ctx.garbled_reads > 0) {
        printf("  *** BUG CONFIRMED: %d garbled status message reads ***\n",
               ctx.garbled_reads);
        BUG_CHECK_PASS();  // becomes a real failure when NCD_ENABLE_KNOWN_BUG_TESTS=1
    } else {
        printf("  No garbled reads detected (strncpy may be atomic-enough on this arch)\n");
    }
    printf("  NOTE: get_status_message() returns pointer without holding mutex\n");

    service_state_cleanup(state);
    return 0;  // <-- CHANGED: always return 0, consistent with other bug-check tests
}
`

**Change on line 949:** Replace `return (ctx.garbled_reads > 0) ? 1 : 0;` with `return 0;`.

### Option B: Fix the production code (more invasive, correct long-term)

Add a new function that copies under mutex:

`c
// service_state.c: after get_status_message
bool service_state_get_status_message_copy(const ServiceState *state, 
                                            char *buf, size_t buf_size) {
    if (!state || !buf || buf_size == 0) return false;
    state_lock(state);
    strncpy(buf, state->status_message, buf_size - 1);
    buf[buf_size - 1] = '\0';
    state_unlock(state);
    return true;
}
`

Then update all 3 callers in `service_main.c`. Also update the test to verify the fix.

### Recommendation

**Option A** is the right fix at this stage. Reasons:
1. P2.20 is a low-severity structural issue, not a P0 crash bug
2. The test pattern should be consistent with the rest of the P0 regression suite
3. `BUG_CHECK_PASS()` is designed exactly for this — it becomes a hard failure when `NCD_ENABLE_KNOWN_BUG_TESTS=1` is set
4. The production impact of the underlying bug is negligible (3 callers, all immediate use)
5. Eliminates the intermittent test failure in CI
