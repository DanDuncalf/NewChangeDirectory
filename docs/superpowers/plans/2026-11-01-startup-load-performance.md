# Startup Load Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut NCD database load time (service startup and every standalone client run) from about 33 s to under 1 s on a ~460K-directory machine. Also fix the merge bug that corrupts parent links, and make exclusion filtering, CRC checking and metadata loading cheap.

**Architecture:** Bulk scans and loads stop doing duplicate checks; `db_add_dir()` becomes a pure append. Rescans keep the existing replace-the-subtree model and use a hashed path index (moved out of `main.c` into `database.c`) to find the subtree and its parent in O(1). Per-drive files are merged by bulk copy (`db_adopt_drive`), which keeps the stored parent indices valid. A single slice-by-8 `platform_crc64` in `../shared` replaces both CRC implementations. All three exclusion matchers are unified behind one compiled `NcdExclusionIndex`: it is linear for 8 or fewer patterns and hashes literal patterns above that. The standalone client reuses the backend's metadata instead of reloading it.

**Tech Stack:** C11, MSVC (`cl`) on Windows, GCC on WSL/Linux, the in-repo C test framework (`test/test_framework.h`), and the harness `python test/generate_report.py`.

## Global Constraints

- Windows x64 + Linux x64 (WSL) must both build and pass. Linux test builds use `-Wall -Wextra -Werror` (test/Makefile line 5), so there can be no warnings.
- On-disk formats do not change: `.database` files and `ncd.metadata` must load bit-for-bit as before. `platform_crc64()` output must equal the historical byte-wise algorithm (golden values in Task 1).
- There is exactly one CRC implementation, and it lives in `../shared/platform.c`. `shm_crc64`/`shm_crc64_update` are deleted.
- `../shared` is a separate git repo with unrelated uncommitted edits (`tui.cpp`, `tui.hpp`). Stage only the files this plan touches there.
- The exclusion hash path is used only when an exclusion list has **more than 8** entries. With 8 or fewer, matching is linear.
- Exclusion semantics become those of `db_exclusion_check()` on the **drive-relative** path, everywhere (scanner, load filter, result filter). The scanner's separate matcher is deleted (user decision).
- Case-only sibling duplicates on case-sensitive folders are **kept** as distinct entries (user decision).
- Rescan model stays replace-the-subtree, with a hashed lookup (user decision).
- New code needs both functional (in-process C) and integration (real `NewChangeDirectory`/`NCDService` binaries, isolated data dir) tests.
- Never run `test\*.exe` directly for the final gate. Use `python test\generate_report.py` (Windows + WSL; WSL is required). Run `cmd /c clean.bat` before full rebuilds after `src\*.c` / `..\shared\*.c` changes.
- Zero failures and zero skips. Never use `SKIP_TEST` in new tests.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Measured baseline (2026-11-01, this machine)

| Step | Time |
|---|---|
| Read 9 DB files | 5.7 ms |
| Byte-wise CRC64 | 28 ms |
| Merge via `db_add_dir` (O(n²)) | 30,466 ms |
| Same merge by memcpy | 2.4 ms |
| Service `LOADING` → `READY` (log) | 33 s |

The benchmark source is in the session scratchpad (`bench.c`). It reproduces the loader's merge exactly.

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `../shared/tools/gen_crc64_table.py` | Generates the slice-by-8 tables | Create |
| `../shared/crc64_table.inc` | `static const uint64_t crc64_tables[8][256]` (generated) | Create |
| `../shared/platform.c` | The one CRC64 (slice-by-8) | Modify ~1252-1305 |
| `src/shared_state.c/.h` | Delete own CRC; checksum via `platform_crc64` | Modify |
| `src/shm_types.h` | `NCD_SHM_TYPES_VERSION` 2 → 3 (snapshot checksum convention changed) | Modify line 28 |
| `src/database.c/.h` | `db_adopt_drive`, `db_load_all_drives_into`, append-only `db_add_dir`, path index + children index (moved from main.c), `NcdExclusionIndex`, faster `db_full_path`/`db_filter_excluded`, metadata load counter | Modify |
| `src/service_state.c` | Loader uses `db_load_all_drives_into` | Modify 174-261 |
| `src/state_backend_local.c` | Loader uses `db_load_all_drives_into` | Modify 60-120 |
| `src/scanner.c` | Rescan uses path index; scanner uses the exclusion index; old matcher deleted | Modify |
| `src/main.c` | Use exported path/children index; result filter uses exclusion index; metadata reuse | Modify |
| `test/test_crc64.c` | Functional: CRC | Create |
| `test/test_drive_load.c` | Functional: merge, loaders, metadata counter | Create |
| `test/test_rescan_dedup.c` | Functional: append, path index, rescan | Create |
| `test/test_exclusion_index.c` | Functional: exclusion index, filter, scanner, `db_full_path` | Create |
| `test/test_load_integration.c` | Integration: real binaries, isolated data dir | Create (grows each task) |
| `test/build-tests.bat`, `test/Makefile` | Build/register new tests | Modify |
| Existing tests using `shm_crc64` / dedup semantics | Update | Modify |

**How test discovery works:** `test/ncd_testlib/discovery.py` picks up every `test/test_*.exe` (Windows) / ELF `test/test_*` (Linux) that has a matching `.c`. Adding the build rule is enough. On Linux, every new target must also be added to `TESTS` and to the `test:` line in `test/Makefile`.

**Where to add Windows build lines:** in `test/build-tests.bat`, immediately before the final `echo.` / `echo Build successful!` block near the end of the file. Each `cl` line is followed by `if errorlevel 1 goto :error`.

---

### Task 1: One slice-by-8 CRC64 in ../shared

**Files:**
- Create: `../shared/tools/gen_crc64_table.py`, `../shared/crc64_table.inc` (generated)
- Modify: `../shared/platform.c:1252-1305`, `src/shared_state.c:1-80,138-150`, `src/shared_state.h:106-107`, `src/shm_types.h:28`
- Modify tests: `test/test_shared_state.c:75-115`, `test/test_shared_state_extended.c:22,31-32`, `test/test_p0_regression.c:367-410`
- Create: `test/test_crc64.c`, `test/test_load_integration.c`
- Modify: `test/build-tests.bat`, `test/Makefile`

**Interfaces:**
- Produces: `unsigned long long platform_crc64(const void *data, size_t len);`. Same output as before; empty/NULL returns 0.
- Produces: `unsigned long long platform_crc64_update(unsigned long long prev, const void *data, size_t len);`. **Changed semantics:** it chains finalized values, so `platform_crc64_update(platform_crc64(a), b) == platform_crc64(a‖b)`, and `platform_crc64_update(0, d, n) == platform_crc64(d, n)`. Its only callers are NCD tests; the sibling repo `loc` calls only `platform_crc64`.
- Removes: `shm_crc64`, `shm_crc64_update`.
- Produces (test helpers in `test/test_load_integration.c`, reused by Tasks 2-5): `it_env_init`, `it_env_cleanup`, `it_run_ncd`, `it_read_result`, `it_db_path`, `it_set_process_env`.

Golden values (computed from the current byte-wise code):

| input | `platform_crc64` |
|---|---|
| `""` | `0x0000000000000000` |
| `"a"` | `0x856176DE4CFF4E76` |
| `"123456789"` | `0xB86883E6FA710A9F` |
| `"The quick brown fox jumps over the lazy dog"` | `0xF4C4E982E838D97E` |

- [ ] **Step 1: Write the failing functional test `test/test_crc64.c`**

```c
/* test_crc64.c -- Functional tests for the single slice-by-8 platform_crc64 */
#include "test_framework.h"
#include "platform.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

/* Reference: the historical byte-wise implementation, kept here only as an oracle. */
static uint64_t g_ref_table[256];
static int g_ref_ready = 0;
static uint64_t ref_crc64(const void *data, size_t len)
{
    if (!data || len == 0) return 0;
    if (!g_ref_ready) {
        for (int i = 0; i < 256; i++) {
            uint64_t c = (uint64_t)i;
            for (int j = 0; j < 8; j++) c = (c & 1) ? (c >> 1) ^ 0x42F0E1EBA9EA3693ULL : c >> 1;
            g_ref_table[i] = c;
        }
        g_ref_ready = 1;
    }
    const uint8_t *b = (const uint8_t *)data;
    uint64_t c = ~0ULL;
    for (size_t i = 0; i < len; i++) c = g_ref_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void fill_random(uint8_t *buf, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; buf[i] = (uint8_t)(seed >> 16); }
}

TEST(crc64_golden_values) {
    ASSERT_TRUE(platform_crc64("", 0) == 0ULL);
    ASSERT_TRUE(platform_crc64("a", 1) == 0x856176DE4CFF4E76ULL);
    ASSERT_TRUE(platform_crc64("123456789", 9) == 0xB86883E6FA710A9FULL);
    const char *fox = "The quick brown fox jumps over the lazy dog";
    ASSERT_TRUE(platform_crc64(fox, strlen(fox)) == 0xF4C4E982E838D97EULL);
    return 0;
}

TEST(crc64_null_and_empty) {
    ASSERT_TRUE(platform_crc64(NULL, 10) == 0ULL);
    ASSERT_TRUE(platform_crc64_update(0x1234ULL, NULL, 0) == 0x1234ULL);
    ASSERT_TRUE(platform_crc64_update(0x1234ULL, "x", 0) == 0x1234ULL);
    return 0;
}

TEST(crc64_matches_reference_every_alignment_and_length) {
    enum { CAP = 4096 + 64 };
    uint8_t *buf = (uint8_t *)malloc(CAP);
    ASSERT_NOT_NULL(buf);
    fill_random(buf, CAP, 0xC0FFEEu);
    for (size_t off = 0; off < 16; off++) {
        for (size_t len = 0; len <= 300; len++) {
            if (platform_crc64(buf + off, len) != ref_crc64(buf + off, len)) {
                printf("  mismatch off=%zu len=%zu\n", off, len);
                free(buf);
                ASSERT_TRUE(0);
            }
        }
        ASSERT_TRUE(platform_crc64(buf + off, 4096) == ref_crc64(buf + off, 4096));
    }
    free(buf);
    return 0;
}

TEST(crc64_update_chains_at_every_split) {
    uint8_t buf[200];
    fill_random(buf, sizeof(buf), 42u);
    uint64_t whole = platform_crc64(buf, sizeof(buf));
    for (size_t split = 0; split <= sizeof(buf); split++) {
        uint64_t c = platform_crc64_update(0, buf, split);
        c = platform_crc64_update(c, buf + split, sizeof(buf) - split);
        ASSERT_TRUE(c == whole);
    }
    return 0;
}

static double now_seconds(void) { return (double)clock() / (double)CLOCKS_PER_SEC; }

TEST(crc64_slice8_faster_than_bytewise) {
    const size_t n = 32u * 1024u * 1024u;
    uint8_t *buf = (uint8_t *)malloc(n);
    ASSERT_NOT_NULL(buf);
    fill_random(buf, n, 7u);
    double best_ref = 1e9, best_new = 1e9;
    volatile uint64_t sink = 0;
    for (int r = 0; r < 3; r++) {
        double t0 = now_seconds(); sink ^= ref_crc64(buf, n); double t1 = now_seconds();
        sink ^= platform_crc64(buf, n); double t2 = now_seconds();
        if (t1 - t0 < best_ref) best_ref = t1 - t0;
        if (t2 - t1 < best_new) best_new = t2 - t1;
    }
    (void)sink;
    if (best_new <= 0) best_new = 1e-6;
    printf("  bytewise %.0f MB/s, slice-by-8 %.0f MB/s (%.2fx)\n",
           32.0 / best_ref, 32.0 / best_new, best_ref / best_new);
#if defined(_MSC_VER) || defined(__OPTIMIZE__)
    ASSERT_TRUE(best_ref / best_new >= 3.0);
#else
    ASSERT_TRUE(best_ref / best_new >= 1.5);   /* Linux test builds are -O0 */
#endif
    free(buf);
    return 0;
}

TEST_MAIN(
    RUN_TEST(crc64_golden_values);
    RUN_TEST(crc64_null_and_empty);
    RUN_TEST(crc64_matches_reference_every_alignment_and_length);
    RUN_TEST(crc64_update_chains_at_every_split);
    RUN_TEST(crc64_slice8_faster_than_bytewise);
)
```

- [ ] **Step 2: Register the test in both build systems**

`test/build-tests.bat` (before the final `echo Build successful!` block):
```bat
:: CRC64 slice-by-8 functional tests
echo Building test_crc64.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_crc64.exe test_crc64.c %OBJDIR%\test_framework.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error
```
`test/Makefile`: append `test_crc64` to `TESTS` (the last line, after `test_agent_verify`) and to the `test:` line (line 406). Add the rule:
```make
test_crc64: test_crc64.c test_framework.c ../src/platform_ncd.c $(SHARED_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 3: Run it and confirm it fails**

Run: `cd test && cmd /c build-tests.bat && python runner.py unit`, then look at `test_crc64.exe` in the output.
Expected: golden/alignment/chain tests PASS, `crc64_update_chains_at_every_split` FAILS (old update is raw, not chaining), and `crc64_slice8_faster_than_bytewise` FAILS (ratio ≈1.0).

- [ ] **Step 4: Create the table generator `../shared/tools/gen_crc64_table.py`**

```python
#!/usr/bin/env python3
"""Generate ../crc64_table.inc: slice-by-8 tables for platform_crc64().

The CRC is the historical NCD variant: polynomial 0x42F0E1EBA9EA3693 processed
LSB-first (reflected table), init ~0, final xor ~0. Re-run after any change;
the output is checked in so no build step needs Python.
"""
import pathlib

POLY = 0x42F0E1EBA9EA3693
MASK = (1 << 64) - 1

t0 = []
for i in range(256):
    c = i
    for _ in range(8):
        c = ((c >> 1) ^ POLY) if (c & 1) else (c >> 1)
    t0.append(c)

tables = [t0]
for _ in range(1, 8):
    prev = tables[-1]
    tables.append([((v >> 8) ^ t0[v & 0xFF]) & MASK for v in prev])

lines = ["/* Generated by tools/gen_crc64_table.py -- do not edit by hand. */",
         "static const uint64_t crc64_tables[8][256] = {"]
for k, tbl in enumerate(tables):
    lines.append("  { /* table %d */" % k)
    for row in range(0, 256, 4):
        lines.append("    " + ", ".join("0x%016XULL" % v for v in tbl[row:row + 4]) + ",")
    lines.append("  },")
lines.append("};")

out = pathlib.Path(__file__).resolve().parent.parent / "crc64_table.inc"
with open(out, "w", encoding="ascii", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print("wrote", out)
```

Run: `python ..\shared\tools\gen_crc64_table.py`
Expected: `wrote E:\llama\shared\crc64_table.inc` (8 tables × 64 lines).

- [ ] **Step 5: Replace the CRC block in `../shared/platform.c`**

Delete everything from `#define CRC64_POLY 0x42F0E1EBA9EA3693ULL` (line 1252) through the closing `}` of `platform_crc64_update` (line ~1305): the static table, `crc64_table_initialized`, `crc64_init_table`, and both functions. Replace with:

```c
/* ============================================================ CRC64 (slice-by-8)
 *
 * The one CRC64 used by every NCD file format and shared-memory snapshot.
 * Polynomial 0x42F0E1EBA9EA3693 processed LSB-first, init ~0, xorout ~0 --
 * bit-identical to the historical byte-wise implementation.  Tables are
 * compile-time constants (tools/gen_crc64_table.py), so there is no lazy
 * initialisation and no init race.
 */
#include "crc64_table.inc"

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define CRC64_LITTLE_ENDIAN 0
#else
#define CRC64_LITTLE_ENDIAN 1   /* MSVC targets and all supported GCC/Clang targets (x64, arm64, riscv64) */
#endif

#define CRC64_BYTE(crc, b) (crc64_tables[0][((crc) ^ (b)) & 0xFFu] ^ ((crc) >> 8))

#if CRC64_LITTLE_ENDIAN
static inline uint64_t crc64_fold8(uint64_t crc, uint64_t w)
{
    w ^= crc;
    return crc64_tables[7][ w        & 0xFFu] ^
           crc64_tables[6][(w >>  8) & 0xFFu] ^
           crc64_tables[5][(w >> 16) & 0xFFu] ^
           crc64_tables[4][(w >> 24) & 0xFFu] ^
           crc64_tables[3][(w >> 32) & 0xFFu] ^
           crc64_tables[2][(w >> 40) & 0xFFu] ^
           crc64_tables[1][(w >> 48) & 0xFFu] ^
           crc64_tables[0][ w >> 56         ];
}
#endif

unsigned long long platform_crc64_update(unsigned long long prev, const void *data, size_t len)
{
    if (!data || len == 0) return prev;
    const uint8_t *p = (const uint8_t *)data;
    uint64_t crc = ~(uint64_t)prev;
#if CRC64_LITTLE_ENDIAN
    /* Align to 8 bytes so the wide loads below never straddle a cache line. */
    while (len && ((uintptr_t)p & 7u)) { crc = CRC64_BYTE(crc, *p++); len--; }
    /* Main loop: 32 bytes per iteration (four slice-by-8 folds). */
    while (len >= 32) {
        uint64_t w0, w1, w2, w3;
        memcpy(&w0, p,      8);
        memcpy(&w1, p + 8,  8);
        memcpy(&w2, p + 16, 8);
        memcpy(&w3, p + 24, 8);
        crc = crc64_fold8(crc, w0);
        crc = crc64_fold8(crc, w1);
        crc = crc64_fold8(crc, w2);
        crc = crc64_fold8(crc, w3);
        p += 32; len -= 32;
    }
    while (len >= 8) {
        uint64_t w;
        memcpy(&w, p, 8);
        crc = crc64_fold8(crc, w);
        p += 8; len -= 8;
    }
#endif
    while (len--) crc = CRC64_BYTE(crc, *p++);
    return (unsigned long long)~crc;
}

unsigned long long platform_crc64(const void *data, size_t len)
{
    return platform_crc64_update(0, data, len);
}
```

Make sure `platform.c` includes `<stdint.h>` and `<string.h>` near the top (add them if missing). Update the comment above the two declarations in `../shared/platform.h` (lines 230-231) to:
```c
/* CRC64 (slice-by-8).  platform_crc64_update chains finalized values:
 * platform_crc64_update(platform_crc64(a), b) == platform_crc64(a || b). */
```

- [ ] **Step 6: Optimisation pass (measure, keep the fastest correct variant)**

Use `test_crc64.exe`'s printed MB/s line as the benchmark. Run it from the harness (`python test\runner.py unit`); the output shows the line. Try each variant, record the MB/s, and keep the fastest:
1. As written (align + 32-byte unroll + 8-byte tail).
2. Without the alignment pre-loop (delete the `while (len && ((uintptr_t)p & 7u))` line).
3. 16-byte unroll (two folds per iteration) instead of 32.

Correctness tests must stay green for every variant. Put the numbers for all three in the commit message body (format: `variant: MB/s`). Expected: ≥3× byte-wise with MSVC `/O2`, typically 2–3 GB/s.

- [ ] **Step 7: Delete the SHM CRC and route snapshots through `platform_crc64`**

In `src/shared_state.c`, delete the whole `/* CRC64 implementation */` block: `#define CRC64_POLY`, `g_crc64_table`, both `crc64_init*` variants with their `INIT_ONCE`/`pthread_once` statics, `shm_crc64`, and `shm_crc64_update` (lines ~12-80). Delete the `#include <pthread.h>` that only served them. Keep `<stdatomic.h>` if anything else uses it; delete it otherwise. Add `#include "platform.h"` after `#include "shared_state.h"`. Change `shm_compute_checksum`'s return to:
```c
    return (uint64_t)platform_crc64(data_start, data_size);
```
In `src/shared_state.h`, delete lines 106-107 (the `shm_crc64` / `shm_crc64_update` prototypes).
In `src/shm_types.h` line 28, change to:
```c
#define NCD_SHM_TYPES_VERSION   3       /* v3: checksum = platform_crc64 (init/xorout ~0) */
```
This makes a client and service from different builds reject each other's snapshots cleanly instead of failing on a checksum mismatch.

- [ ] **Step 8: Update the existing tests that used `shm_crc64`**

`test/test_shared_state.c`: replace the bodies of `crc64_basic`, `crc64_empty`, `crc64_incremental` (lines 75-115) with:
```c
TEST(crc64_basic) {
    const char *test_data = "123456789";
    uint64_t crc = platform_crc64(test_data, 9);
    ASSERT_TRUE(crc == 0xB86883E6FA710A9FULL);
    ASSERT_TRUE(platform_crc64(test_data, 9) == crc);
    ASSERT_TRUE(platform_crc64("123456780", 9) != crc);
    return 0;
}

TEST(crc64_empty) {
    ASSERT_TRUE(platform_crc64("", 0) == 0ULL);
    return 0;
}

TEST(crc64_incremental) {
    const char *combined = "Hello World!";
    uint64_t crc_full = platform_crc64(combined, 12);
    uint64_t crc_inc = platform_crc64_update(0, "Hello ", 6);
    crc_inc = platform_crc64_update(crc_inc, "World!", 6);
    ASSERT_TRUE(crc_full == crc_inc);
    return 0;
}
```
(add `#include "platform.h"` if the file doesn't already include it).
`test/test_shared_state_extended.c` lines 22, 31, 32: replace `shm_crc64(` with `platform_crc64(` (add `#include "platform.h"`).
`test/test_p0_regression.c` lines 367-410 (P0.4): replace every `shm_crc64(` with `platform_crc64(`. Update the comment at line 367 to say the tables are now compile-time constants, so the race is structurally impossible; the 8-thread test still runs as a guard.

- [ ] **Step 9: Write the integration test file `test/test_load_integration.c` (CRC part + shared helpers)**

```c
/* test_load_integration.c -- Integration tests: real NCD binaries against an
 * isolated data directory (LOCALAPPDATA / XDG_DATA_HOME), NCD_TEST_MODE=1. */
#include "test_framework.h"
#include "agent_test_common.h"
#include "service_test_common.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    char base[NCD_MAX_PATH];   /* LOCALAPPDATA / XDG_DATA_HOME value */
    char data[NCD_MAX_PATH];   /* base\NCD or base/ncd */
    char tmp[NCD_MAX_PATH];    /* TEMP/TMP or XDG_RUNTIME_DIR for result file */
    char fs[NCD_MAX_PATH];     /* scratch filesystem tree for rescans */
} ItEnv;

static void it_mkdir(const char *p) { AGENT_MKDIR(p, 0755); }

static void it_env_init(ItEnv *e, const char *suffix)
{
    agent_get_temp_dir(e->base, sizeof(e->base), suffix);
    agent_rm_rf(e->base);
    it_mkdir(e->base);
    agent_build_ncd_dir(e->data, sizeof(e->data), e->base);
    it_mkdir(e->data);
    snprintf(e->tmp, sizeof(e->tmp), "%s%stmp", e->base, NCD_PATH_SEP);
    it_mkdir(e->tmp);
    snprintf(e->fs, sizeof(e->fs), "%s%sfs", e->base, NCD_PATH_SEP);
    it_mkdir(e->fs);
}

static void it_env_cleanup(ItEnv *e) { agent_rm_rf(e->base); }

static void it_db_path(const ItEnv *e, char letter, char *buf, size_t n)
{
    agent_build_db_path(buf, n, e->data, letter);
}

/* Point this test process at the isolated env (for in-process service starts). */
static void it_set_process_env(const ItEnv *e)
{
#if NCD_PLATFORM_WINDOWS
    _putenv_s("LOCALAPPDATA", e->base);
    _putenv_s("NCD_TEST_MODE", "1");
#else
    setenv("XDG_DATA_HOME", e->base, 1);
    setenv("NCD_TEST_MODE", "1", 1);
#endif
}

/* Run the real client with raw args; captures stdout+stderr. Returns exit code. */
static int it_run_ncd(const ItEnv *e, const char *extra_env, const char *args,
                      char *out, size_t out_size)
{
    const char *exe = agent_find_exe();
    if (!exe) { snprintf(out, out_size, "EXE_NOT_FOUND"); return -1; }
    char cmd[NCD_MAX_PATH * 6];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd),
        "set \"LOCALAPPDATA=%s\" && set \"TEMP=%s\" && set \"TMP=%s\" && set NCD_TEST_MODE=1 && %s%s\"%s\" %s 2>&1",
        e->base, e->tmp, e->tmp, extra_env ? extra_env : "", extra_env ? " && " : "", exe, args);
#else
    snprintf(cmd, sizeof(cmd),
        "XDG_DATA_HOME='%s' XDG_RUNTIME_DIR='%s' NCD_TEST_MODE=1 %s '%s' %s 2>&1",
        e->base, e->tmp, extra_env ? extra_env : "", exe, args);
#endif
    FILE *fp = AGENT_POPEN(cmd, "r");
    if (!fp) return -1;
    size_t n = fread(out, 1, out_size - 1, fp);
    out[n] = '\0';
    return AGENT_PCLOSE(fp);
}

/* Read the wrapper result file the client writes (ncd_result.bat / .sh). */
static bool it_read_result(const ItEnv *e, char *buf, size_t n)
{
    char path[NCD_MAX_PATH];
#if NCD_PLATFORM_WINDOWS
    snprintf(path, sizeof(path), "%s\\ncd_result.bat", e->tmp);
#else
    snprintf(path, sizeof(path), "%s/ncd_result.sh", e->tmp);
#endif
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t r = fread(buf, 1, n - 1, f);
    buf[r] = '\0';
    fclose(f);
    return true;
}

/* ---------------------------------------------------------------- Task 1 */

static uint64_t ref_crc64(const uint8_t *b, size_t len)
{
    static uint64_t t[256]; static int ready = 0;
    if (!ready) {
        for (int i = 0; i < 256; i++) {
            uint64_t c = (uint64_t)i;
            for (int j = 0; j < 8; j++) c = (c & 1) ? (c >> 1) ^ 0x42F0E1EBA9EA3693ULL : c >> 1;
            t[i] = c;
        }
        ready = 1;
    }
    if (len == 0) return 0;
    uint64_t c = ~0ULL;
    for (size_t i = 0; i < len; i++) c = t[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

/* The on-disk checksum is unchanged: a file saved by the new code carries the
 * historical byte-wise CRC, and the real binary accepts it. */
TEST(it_crc_db_file_checksum_unchanged_and_loads) {
    ItEnv e; it_env_init(&e, "it_crc");
    char letter = agent_test_drive_letter();
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    int a = db_add_dir(drv, "crcroot", -1, false, false);
    db_add_dir(drv, "crc_unique_leaf_7731", a, false, false);
    char path[NCD_MAX_PATH]; it_db_path(&e, letter, path, sizeof(path));
    ASSERT_TRUE(db_save_binary_single(db, 0, path));
    db_free(db);

    FILE *f = fopen(path, "rb"); ASSERT_NOT_NULL(f);
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    uint8_t *blob = (uint8_t *)malloc((size_t)sz);
    ASSERT_TRUE(fread(blob, 1, (size_t)sz, f) == (size_t)sz); fclose(f);
    size_t off = (sz >= 2 && blob[0] == 0xFF && blob[1] == 0xFE) ? 2 : 0;
    BinFileHdr hdr; memcpy(&hdr, blob + off, sizeof(hdr));
    ASSERT_TRUE(hdr.checksum == ref_crc64(blob + off + sizeof(hdr), (size_t)sz - off - sizeof(hdr)));
    free(blob);

    char out[8192];
    it_run_ncd(&e, NULL, "crc_unique_leaf_7731", out, sizeof(out));
    char res[4096]; ASSERT_TRUE(it_read_result(&e, res, sizeof(res)));
    ASSERT_STR_CONTAINS(res, "crc_unique_leaf_7731");
    it_env_cleanup(&e);
    return 0;
}

TEST_MAIN(
    agent_kill_any_service();
    RUN_TEST(it_crc_db_file_checksum_unchanged_and_loads);
)
```

Register it. `test/build-tests.bat`:
```bat
:: Load-path integration tests (real binaries, isolated data dir)
echo Building test_load_integration.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_load_integration.exe test_load_integration.c %OBJDIR%\test_framework.obj %OBJDIR%\agent_test_common.obj %OBJDIR%\service_test_common.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\control_ipc_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error
```
`test/Makefile`: add `test_load_integration` to `TESTS` and the `test:` line, plus:
```make
LOAD_INTEGRATION_SOURCES = agent_test_common.c service_test_common.c ../src/control_ipc_posix.c ../src/control_ipc_common.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_load_integration: test_load_integration.c test_framework.c $(LOAD_INTEGRATION_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 10: Rebuild everything and run the affected tests**

Run:
```powershell
cmd /c clean.bat
cmd /c build.bat
cd test; cmd /c build-tests.bat; cd ..
python test\runner.py unit
```
Expected: `test_crc64`, `test_shared_state`, `test_shared_state_extended`, `test_p0_regression`, `test_service_database`, and `test_load_integration` all PASS. If `test_load_integration.exe` reports `EXE_NOT_FOUND`, the client didn't build; fix `build.bat` output first.

- [ ] **Step 11: Commit (two repos)**

```bash
git -C ../shared add platform.c platform.h crc64_table.inc tools/gen_crc64_table.py
git -C ../shared commit -m "perf: single slice-by-8 platform_crc64 with compile-time tables

<variant MB/s numbers from Step 6>

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git add src/shared_state.c src/shared_state.h src/shm_types.h test/test_crc64.c test/test_load_integration.c test/test_shared_state.c test/test_shared_state_extended.c test/test_p0_regression.c test/build-tests.bat test/Makefile
git commit -m "perf: use the shared slice-by-8 CRC for snapshots; drop shm_crc64; SHM v3

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Bulk-copy drive merge (fixes 30 s load + parent-index corruption)

**Files:**
- Modify: `src/database.h` (after `db_find_drive`, ~line 161), `src/database.c` (after `db_find_drive`, ~line 968)
- Modify: `src/service_state.c:169-261`, `src/state_backend_local.c:57-120`
- Create: `test/test_drive_load.c`
- Modify: `test/test_load_integration.c`, `test/build-tests.bat`, `test/Makefile`

**Interfaces:**
- Consumes: none from Task 1 (it is independent but ordered after it).
- Produces:
```c
DriveData *db_adopt_drive(NcdDatabase *dst, const DriveData *src);
typedef void (*DbLoadProgressFn)(char drive_letter, int index, int total, void *user_data);
int db_load_all_drives_into(NcdDatabase *dst, DbLoadProgressFn progress, void *user_data);
```
`db_adopt_drive` copies the whole drive (letter, label, mount_point, volume_label, type, dirs[], name_pool) with no duplicate checks and returns the new drive. If `dst` already has `src->letter`, it returns NULL and leaves `dst` unchanged (first file wins). `db_load_all_drives_into` returns the number of files loaded.

**Why this fixes the correctness bug:** `DirEntry.parent` is a position in the array. The old merge sent each entry through `db_add_dir`, which dropped case-variant siblings. That shifted every later entry down while their stored parent numbers stayed the same, so children pointed at the wrong parent (2 entries dropped from N: today). Copying the arrays whole keeps every position, and therefore every parent link, intact.

- [ ] **Step 1: Write the failing functional test `test/test_drive_load.c`**

```c
/* test_drive_load.c -- Functional tests for bulk drive adoption and the loaders */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/ncd.h"
#include "../src/service_state.h"
#include "../src/state_backend.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if NCD_PLATFORM_WINDOWS
#include <direct.h>
#define TDL_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TDL_MKDIR(p) mkdir(p, 0755)
#endif

static char g_base[NCD_MAX_PATH];
static char g_data[NCD_MAX_PATH];

static void env_setup(const char *suffix)
{
    const char *t = getenv("TEMP"); if (!t) t = getenv("TMP"); if (!t) t = "/tmp";
    snprintf(g_base, sizeof(g_base), "%s%sncd_tdl_%s", t, NCD_PATH_SEP, suffix);
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", g_base); system(cmd);
    TDL_MKDIR(g_base);
    snprintf(g_data, sizeof(g_data), "%s\\NCD", g_base); TDL_MKDIR(g_data);
    _putenv_s("LOCALAPPDATA", g_base);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_base); system(cmd);
    TDL_MKDIR(g_base);
    snprintf(g_data, sizeof(g_data), "%s/ncd", g_base); TDL_MKDIR(g_data);
    setenv("XDG_DATA_HOME", g_base, 1);
#endif
    ncd_set_system_mode(false);
}

static char test_letter(void)
{
    char drives[32];
    int n = platform_get_available_drives(drives, 27);
    return n > 0 ? drives[0] : 'C';
}

/* Tree with case-variant siblings: the exact shape that the old merge corrupted. */
static NcdDatabase *make_case_variant_db(char letter)
{
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    int root  = db_add_dir(drv, "root", -1, false, false);      /* 0 */
    int foo   = db_add_dir(drv, "Foo", root, false, false);     /* 1 */
    int foo2  = db_add_dir(drv, "foo", root, false, false);     /* 2 */
    db_add_dir(drv, "upper_child_x1", foo, false, false);       /* 3 */
    int lc = db_add_dir(drv, "lower_child_y2", foo2, false, false); /* 4 */
    db_add_dir(drv, "deep_leaf_z3", lc, false, false);          /* 5 */
    return db;
}

TEST(adopt_drive_copies_entries_verbatim) {
    NcdDatabase *src = make_case_variant_db('C');
    NcdDatabase *dst = db_create();
    DriveData *got = db_adopt_drive(dst, &src->drives[0]);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ_INT(6, got->dir_count);
    ASSERT_TRUE(memcmp(got->dirs, src->drives[0].dirs, 6 * sizeof(DirEntry)) == 0);
    ASSERT_TRUE(got->name_pool_len == src->drives[0].name_pool_len);
    ASSERT_TRUE(memcmp(got->name_pool, src->drives[0].name_pool, got->name_pool_len) == 0);
    ASSERT_TRUE(got->dirs != src->drives[0].dirs);          /* deep copy */
    char a[NCD_MAX_PATH], b[NCD_MAX_PATH];
    for (int i = 0; i < 6; i++) {
        db_full_path(&src->drives[0], i, a, sizeof(a));
        db_full_path(got, i, b, sizeof(b));
        ASSERT_EQ_STR(a, b);
    }
    db_free(src);
    db_free(dst);
    return 0;
}

TEST(adopt_drive_preserves_case_variant_parents) {
    NcdDatabase *src = make_case_variant_db('C');
    NcdDatabase *dst = db_create();
    DriveData *got = db_adopt_drive(dst, &src->drives[0]);
    char p[NCD_MAX_PATH];
    db_full_path(got, 5, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "foo");
    ASSERT_STR_CONTAINS(p, "lower_child_y2");
    ASSERT_STR_CONTAINS(p, "deep_leaf_z3");
    db_full_path(got, 3, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "Foo");
    db_free(src);
    db_free(dst);
    return 0;
}

TEST(adopt_drive_rejects_duplicate_letter) {
    NcdDatabase *src = make_case_variant_db('D');
    NcdDatabase *dst = db_create();
    ASSERT_NOT_NULL(db_adopt_drive(dst, &src->drives[0]));
    ASSERT_NULL(db_adopt_drive(dst, &src->drives[0]));
    ASSERT_EQ_INT(1, dst->drive_count);
    ASSERT_EQ_INT(6, dst->drives[0].dir_count);
    db_free(src);
    db_free(dst);
    return 0;
}

TEST(adopt_drive_empty_drive) {
    NcdDatabase *src = db_create();
    db_add_drive(src, 'E');
    NcdDatabase *dst = db_create();
    DriveData *got = db_adopt_drive(dst, &src->drives[0]);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ_INT(0, got->dir_count);
    ASSERT_NULL(got->dirs);
    db_free(src);
    db_free(dst);
    return 0;
}

static NcdDatabase *make_big_db(char letter, int n)
{
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    char name[32];
    for (int i = 0; i < n; i++) {
        snprintf(name, sizeof(name), "d%07d", i);
        db_add_dir(drv, name, i < 64 ? -1 : (i / 64) - 1, false, false);
    }
    return db;
}

TEST(load_all_drives_into_reads_files_and_is_fast) {
    env_setup("big");
    char letter = test_letter();
    NcdDatabase *big = make_big_db(letter, 250000);
    char path[NCD_MAX_PATH];
    ASSERT_NOT_NULL(db_drive_path(letter, path, sizeof(path)));
    ASSERT_TRUE(db_save_binary_single(big, 0, path));
    db_free(big);

    NcdDatabase *dst = db_create();
    clock_t t0 = clock();
    int loaded = db_load_all_drives_into(dst, NULL, NULL);
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  loaded %d file(s), 250000 dirs in %.3f s\n", loaded, secs);
    ASSERT_EQ_INT(1, loaded);
    DriveData *drv = db_find_drive(dst, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(250000, drv->dir_count);
    ASSERT_TRUE(secs < 1.0);   /* old O(n^2) merge: ~20 s for this size */
    db_free(dst);
    return 0;
}

static int g_progress_calls = 0;
static void count_progress(char l, int i, int t, void *ud) { (void)l; (void)i; (void)t; (void)ud; g_progress_calls++; }

TEST(load_all_drives_into_reports_progress) {
    env_setup("prog");
    g_progress_calls = 0;
    NcdDatabase *dst = db_create();
    db_load_all_drives_into(dst, count_progress, NULL);
    char drives[32];
    ASSERT_EQ_INT(platform_get_available_drives(drives, 27), g_progress_calls);
    db_free(dst);
    return 0;
}

TEST(service_loader_preserves_case_variant_parents) {
    env_setup("svc");
    char letter = test_letter();
    NcdDatabase *src = make_case_variant_db(letter);
    char path[NCD_MAX_PATH];
    ASSERT_NOT_NULL(db_drive_path(letter, path, sizeof(path)));
    ASSERT_TRUE(db_save_binary_single(src, 0, path));
    db_free(src);

    ServiceState *st = service_state_init();
    ASSERT_NOT_NULL(st);
    ASSERT_TRUE(service_state_load_databases(st));
    const NcdDatabase *db = service_state_get_database(st);
    DriveData *drv = db_find_drive((NcdDatabase *)db, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(6, drv->dir_count);
    char p[NCD_MAX_PATH];
    db_full_path(drv, 5, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "lower_child_y2");
    service_state_cleanup(st);
    return 0;
}

TEST(local_backend_loader_preserves_case_variant_parents) {
    env_setup("local");
    char letter = test_letter();
    NcdDatabase *src = make_case_variant_db(letter);
    char path[NCD_MAX_PATH];
    ASSERT_NOT_NULL(db_drive_path(letter, path, sizeof(path)));
    ASSERT_TRUE(db_save_binary_single(src, 0, path));
    db_free(src);

    NcdStateView *view = NULL;
    NcdStateSourceInfo info;
    ASSERT_EQ_INT(0, state_backend_open_local(&view, &info));
    const NcdDatabase *db = state_view_database(view);
    DriveData *drv = db_find_drive((NcdDatabase *)db, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(6, drv->dir_count);
    char p[NCD_MAX_PATH];
    db_full_path(drv, 5, p, sizeof(p));
    ASSERT_STR_CONTAINS(p, "deep_leaf_z3");
    ASSERT_STR_CONTAINS(p, "lower_child_y2");
    state_backend_close(view);
    return 0;
}

TEST_MAIN(
    RUN_TEST(adopt_drive_copies_entries_verbatim);
    RUN_TEST(adopt_drive_preserves_case_variant_parents);
    RUN_TEST(adopt_drive_rejects_duplicate_letter);
    RUN_TEST(adopt_drive_empty_drive);
    RUN_TEST(load_all_drives_into_reads_files_and_is_fast);
    RUN_TEST(load_all_drives_into_reports_progress);
    RUN_TEST(service_loader_preserves_case_variant_parents);
    RUN_TEST(local_backend_loader_preserves_case_variant_parents);
)
```

Note for the implementer: `make_case_variant_db` builds case-variant siblings with `db_add_dir`. Until Task 3 makes `db_add_dir` append-only, the Windows build deduplicates "foo" into "Foo", so set up these fixtures by writing the arrays directly. Add this helper near the top of the file and use it inside `make_case_variant_db` and `make_big_db` instead of `db_add_dir`:
```c
static int raw_add(DriveData *drv, const char *name, int parent)
{
    size_t nl = strlen(name) + 1;
    if (drv->name_pool_len + nl > drv->name_pool_cap) {
        drv->name_pool_cap = (drv->name_pool_cap ? drv->name_pool_cap * 2 : 4096) + nl;
        drv->name_pool = realloc(drv->name_pool, drv->name_pool_cap);
    }
    memcpy(drv->name_pool + drv->name_pool_len, name, nl);
    if (drv->dir_count >= drv->dir_capacity) {
        drv->dir_capacity = drv->dir_capacity ? drv->dir_capacity * 2 : 64;
        drv->dirs = realloc(drv->dirs, sizeof(DirEntry) * (size_t)drv->dir_capacity);
    }
    DirEntry *e = &drv->dirs[drv->dir_count];
    memset(e, 0, sizeof(*e));
    e->parent = parent;
    e->name_off = (uint32_t)drv->name_pool_len;
    drv->name_pool_len += nl;
    return drv->dir_count++;
}
```
(`make_big_db` must use `raw_add` too, or building the fixture itself is O(n²) until Task 3.)

- [ ] **Step 2: Register `test_drive_load`**

`test/build-tests.bat`:
```bat
:: Drive load / merge functional tests
echo Building test_drive_load.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_drive_load.exe test_drive_load.c %OBJDIR%\test_framework.obj %OBJDIR%\control_ipc_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\state_backend_local.obj %OBJDIR%\state_backend_service.obj %OBJDIR%\service_publish.obj %OBJDIR%\shared_state.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\service_state.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error
```
`test/Makefile`: add `test_drive_load` to `TESTS` and `test:`, plus:
```make
test_drive_load: test_drive_load.c test_framework.c $(SERVICE_EMPTY_DB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 3: Build tests and confirm they fail**

Run: `cd test && cmd /c build-tests.bat`
Expected: link error `unresolved external symbol db_adopt_drive` / `db_load_all_drives_into`. That's the expected failure, because the functions don't exist yet.

- [ ] **Step 4: Implement `db_adopt_drive` and `db_load_all_drives_into`**

`src/database.h`, after `DriveData *db_find_drive(NcdDatabase *db, char letter);`:
```c
/*
 * Copy a fully-formed drive (dirs[] + name pool, verbatim) into dst as a new
 * drive.  O(dir_count) memcpy, no duplicate checks: positions -- and therefore
 * parent links -- are preserved exactly.  Returns NULL (dst unchanged) if dst
 * already has a drive with src->letter.
 */
DriveData *db_adopt_drive(NcdDatabase *dst, const DriveData *src);

/* Progress callback for db_load_all_drives_into: index is 1-based. */
typedef void (*DbLoadProgressFn)(char drive_letter, int index, int total, void *user_data);

/*
 * Load the per-drive database file of every drive present on this machine
 * and adopt its drives into dst.  Returns the number of files loaded.
 */
int db_load_all_drives_into(NcdDatabase *dst, DbLoadProgressFn progress, void *user_data);
```
`src/database.c`, after `db_find_drive` (add `#include "platform.h"` at the top if `platform_get_available_drives` isn't already visible):
```c
DriveData *db_adopt_drive(NcdDatabase *dst, const DriveData *src)
{
    if (!dst || !src) return NULL;
    if (db_find_drive(dst, src->letter)) return NULL;
    db_make_mutable(dst);

    DriveData *drv = db_add_drive(dst, src->letter);   /* invalidates caches */
    *drv = *src;                        /* letter, label, mount_point, volume_label, type */
    drv->dirs = NULL;
    drv->dir_count = drv->dir_capacity = 0;
    drv->name_pool = NULL;
    drv->name_pool_len = drv->name_pool_cap = 0;

    if (src->dir_count > 0 && src->dirs) {
        size_t bytes = sizeof(DirEntry) * (size_t)src->dir_count;
        drv->dirs = ncd_malloc(bytes);
        memcpy(drv->dirs, src->dirs, bytes);
        drv->dir_count = drv->dir_capacity = src->dir_count;
    }
    if (src->name_pool_len > 0 && src->name_pool) {
        drv->name_pool = ncd_malloc(src->name_pool_len);
        memcpy(drv->name_pool, src->name_pool, src->name_pool_len);
        drv->name_pool_len = drv->name_pool_cap = src->name_pool_len;
    }
    return drv;
}

int db_load_all_drives_into(NcdDatabase *dst, DbLoadProgressFn progress, void *user_data)
{
    if (!dst) return 0;
    char drives[32];
    int drive_count = platform_get_available_drives(drives, 27);
    int loaded = 0;
    for (int i = 0; i < drive_count; i++) {
        if (progress) progress(drives[i], i + 1, drive_count, user_data);
        char path[NCD_MAX_PATH];
        if (!db_drive_path(drives[i], path, sizeof(path))) continue;
        if (!platform_file_exists(path)) continue;
        NcdDatabase *one = db_load_auto(path);
        if (!one) continue;
        for (int d = 0; d < one->drive_count; d++)
            db_adopt_drive(dst, &one->drives[d]);
        db_free(one);
        loaded++;
    }
    return loaded;
}
```

- [ ] **Step 5: Switch both loaders**

`src/service_state.c`: replace the body of `service_state_load_databases` (lines 174-261) with:
```c
static void service_load_progress(char letter, int index, int total, void *user_data)
{
    ServiceState *state = (ServiceState *)user_data;
    char status[256];
    snprintf(status, sizeof(status), "Loading drive %c: (%d/%d)...", letter, index, total);
    service_state_set_status_message(state, status);
}

bool service_state_load_databases(ServiceState *state) {
    if (!state) return false;

    service_state_set_runtime_state(state, SERVICE_STATE_LOADING);
    service_state_set_status_message(state, "Loading databases...");

    char drives[32];
    int drive_count = platform_get_available_drives(drives, 27);
    int loaded_count = db_load_all_drives_into(state->database, service_load_progress, state);
    NCD_DEBUG_LOG("DEBUG: Loaded %d drive database file(s) of %d drives\n", loaded_count, drive_count);

    if (state->metadata && state->metadata->exclusions.count > 0) {
        int removed = db_filter_excluded(state->database, state->metadata);
        if (removed > 0) {
            NCD_DEBUG_LOG("DEBUG: Filtered %d excluded dirs from service database\n", removed);
        }
    }

    state->last_rescan = state->database->last_scan;

    char status[256];
    snprintf(status, sizeof(status), "Loaded %d drives", loaded_count);
    service_state_set_status_message(state, status);
    return loaded_count > 0 || drive_count == 0;
}
```
(`service_load_progress` goes immediately above the function. Keep the doc comment that was above it.)

`src/state_backend_local.c`: replace the body of `load_all_drive_databases` (lines 60-120) with:
```c
static NcdDatabase *load_all_drive_databases(void) {
    NcdDatabase *db = db_create();
    if (!db) {
        set_error("Failed to create database");
        return NULL;
    }
    db_load_all_drives_into(db, NULL, NULL);
    return db;
}
```

- [ ] **Step 6: Run the functional tests**

Run: `cd test && cmd /c build-tests.bat && cd .. && python test\runner.py unit`
Expected: all 8 `test_drive_load` tests PASS. `load_all_drives_into_reads_files_and_is_fast` prints well under 1 s.

- [ ] **Step 7: Add integration tests (standalone + service) to `test/test_load_integration.c`**

Insert above `TEST_MAIN`:
```c
/* ---------------------------------------------------------------- Task 2 */

static int it_raw_add(DriveData *drv, const char *name, int parent)
{
    size_t nl = strlen(name) + 1;
    if (drv->name_pool_len + nl > drv->name_pool_cap) {
        drv->name_pool_cap = (drv->name_pool_cap ? drv->name_pool_cap * 2 : 4096) + nl;
        drv->name_pool = realloc(drv->name_pool, drv->name_pool_cap);
    }
    memcpy(drv->name_pool + drv->name_pool_len, name, nl);
    if (drv->dir_count >= drv->dir_capacity) {
        drv->dir_capacity = drv->dir_capacity ? drv->dir_capacity * 2 : 64;
        drv->dirs = realloc(drv->dirs, sizeof(DirEntry) * (size_t)drv->dir_capacity);
    }
    DirEntry *d = &drv->dirs[drv->dir_count];
    memset(d, 0, sizeof(*d));
    d->parent = parent;
    d->name_off = (uint32_t)drv->name_pool_len;
    drv->name_pool_len += nl;
    return drv->dir_count++;
}

/* Case-variant siblings + 200K filler dirs. */
static void it_write_case_variant_db(const ItEnv *e, char letter, int filler)
{
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, letter);
    int root = it_raw_add(drv, "itroot", -1);
    int up   = it_raw_add(drv, "Foo", root);
    int lo   = it_raw_add(drv, "foo", root);
    it_raw_add(drv, "it_upper_child_q8", up);
    int lc   = it_raw_add(drv, "it_lower_child_w4", lo);
    it_raw_add(drv, "it_deep_leaf_k5", lc);
    char name[32];
    for (int i = 0; i < filler; i++) {
        snprintf(name, sizeof(name), "f%07d", i);
        it_raw_add(drv, name, i < 64 ? root : 6 + (i / 64) - 1);
    }
    char path[NCD_MAX_PATH]; it_db_path(e, letter, path, sizeof(path));
    db_save_binary_single(db, 0, path);
    db_free(db);
}

TEST(it_standalone_merge_keeps_parents_and_is_fast) {
    ItEnv e; it_env_init(&e, "it_merge");
    it_write_case_variant_db(&e, agent_test_drive_letter(), 200000);
    char out[8192];
    time_t t0 = time(NULL);
    it_run_ncd(&e, NULL, "it_deep_leaf_k5", out, sizeof(out));
    double secs = difftime(time(NULL), t0);
    char res[4096]; ASSERT_TRUE(it_read_result(&e, res, sizeof(res)));
    ASSERT_STR_CONTAINS(res, "it_lower_child_w4");
    ASSERT_STR_CONTAINS(res, "it_deep_leaf_k5");
    printf("  standalone search over 200K dirs: %.0f s\n", secs);
    ASSERT_TRUE(secs <= 5.0);   /* old O(n^2) merge: ~15 s */
    it_env_cleanup(&e);
    return 0;
}

static bool it_wait_service_ready(const ItEnv *e, int timeout_s)
{
    char out[4096];
    for (int i = 0; i < timeout_s * 4; i++) {
        it_run_ncd(e, NULL, "--agent:check --service-status", out, sizeof(out));
        if (strstr(out, "READY")) return true;
        platform_sleep_ms(250);
    }
    return false;
}

TEST(it_service_merge_keeps_parents_and_is_fast) {
    ItEnv e; it_env_init(&e, "it_svc");
    it_write_case_variant_db(&e, agent_test_drive_letter(), 200000);
    it_set_process_env(&e);
    agent_kill_any_service();
    char out[8192];
    run_service_command("start", out, sizeof(out));
    time_t t0 = time(NULL);
    bool ready = it_wait_service_ready(&e, 20);
    double secs = difftime(time(NULL), t0);
    printf("  service READY after %.0f s\n", secs);
    ASSERT_TRUE(ready);
    ASSERT_TRUE(secs <= 5.0);   /* old: ~15 s for 200K dirs */

    it_run_ncd(&e, NULL, "it_deep_leaf_k5", out, sizeof(out));
    char res[4096]; ASSERT_TRUE(it_read_result(&e, res, sizeof(res)));
    ASSERT_STR_CONTAINS(res, "it_lower_child_w4");
    ASSERT_STR_CONTAINS(res, "it_deep_leaf_k5");

    run_service_command("stop", out, sizeof(out));
    ensure_service_stopped();
    it_env_cleanup(&e);
    return 0;
}
```
and add to `TEST_MAIN`:
```c
    RUN_TEST(it_standalone_merge_keeps_parents_and_is_fast);
    RUN_TEST(it_service_merge_keeps_parents_and_is_fast);
```
The service test also covers Task 1 end to end: the client only reaches the service path if the v3 snapshot checksum validates.

- [ ] **Step 8: Rebuild everything and run unit + integration-related tests**

Run:
```powershell
cmd /c clean.bat; cmd /c build.bat; cd test; cmd /c build-tests.bat; cd ..
python test\runner.py unit
```
Expected: `test_drive_load` 8/8 PASS and `test_load_integration` 3/3 PASS. `test_service_*` and `test_service_empty_db` must also pass.

- [ ] **Step 9: Commit**

```bash
git add src/database.c src/database.h src/service_state.c src/state_backend_local.c test/test_drive_load.c test/test_load_integration.c test/build-tests.bat test/Makefile
git commit -m "perf: merge per-drive databases by bulk copy (33s -> ms); fix parent-index corruption

db_add_dir per entry was O(n^2) and dropped case-variant siblings without
remapping later parent indices. db_adopt_drive copies arrays verbatim.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Append-only `db_add_dir`; rescans locate subtree/parent via hashed path index

**Files:**
- Modify: `src/database.c` (PathMap block lines 70-100; `db_add_dir` lines 972-1015), `src/database.h`
- Modify: `src/main.c:1758-2002` (delete moved code), call sites at `main.c:2044-2065, 2087-2198, 2307-2308, 2572-2579`
- Modify: `src/scanner.c:780-860` (`scan_subdirectory`)
- Modify: `test/test_matcher_extended.c:641-676,722` (replace the obsolete dedup test)
- Create: `test/test_rescan_dedup.c`
- Modify: `test/test_load_integration.c`, `test/build-tests.bat`, `test/Makefile`

**Interfaces:**
- Consumes: nothing from Tasks 1-2.
- Produces:
```c
int  db_add_dir(DriveData *drv, const char *name, int32_t parent, bool is_hidden, bool is_system); /* pure append, O(1) amortized */
int  db_path_index_get(NcdDatabase *db, const char *full_path);   /* dir index or -1; builds + caches index */
typedef struct { int *children; int child_count; int child_capacity; } DirChildren;
DirChildren *db_build_children_index(const DriveData *drv);
void db_free_children_index(DirChildren *children, int dir_count);
```
Path keys compare case-insensitively on Windows (hash on `tolower`, compare `_stricmp`) and case-sensitively on Linux (raw hash, `strcmp`). This also fixes a latent bug: the old map hashed case-sensitively but compared case-insensitively, so a lookup with different casing always missed.

- [ ] **Step 1: Write the failing functional test `test/test_rescan_dedup.c`**

```c
/* test_rescan_dedup.c -- append-only db_add_dir, path index, hashed rescans */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/scanner.h"
#include "../src/ncd.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if NCD_PLATFORM_WINDOWS
#include <direct.h>
#define TRD_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TRD_MKDIR(p) mkdir(p, 0755)
#endif

TEST(add_dir_is_append_only) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int u = db_add_dir(drv, "Users", -1, false, false);
    int a = db_add_dir(drv, "Docs", u, false, false);
    int b = db_add_dir(drv, "Docs", u, false, false);
    int c = db_add_dir(drv, "docs", u, false, false);
    ASSERT_EQ_INT(1, a);
    ASSERT_EQ_INT(2, b);
    ASSERT_EQ_INT(3, c);
    ASSERT_EQ_INT(4, drv->dir_count);
    db_free(db);
    return 0;
}

TEST(add_dir_300k_appends_fast) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    char name[32];
    clock_t t0 = clock();
    for (int i = 0; i < 300000; i++) {
        snprintf(name, sizeof(name), "n%07d", i);
        db_add_dir(drv, name, i < 100 ? -1 : (i / 100) - 1, false, false);
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  300K appends: %.3f s\n", secs);
    ASSERT_EQ_INT(300000, drv->dir_count);
    ASSERT_TRUE(secs < 0.5);   /* old linear dedup: ~30 s */
    db_free(db);
    return 0;
}

TEST(path_index_finds_every_entry) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    int b = db_add_dir(drv, "Beta", r, false, false);
    db_add_dir(drv, "Gamma", b, false, false);
    db_add_dir(drv, "Delta", r, false, false);
    char p[NCD_MAX_PATH];
    for (int i = 0; i < drv->dir_count; i++) {
        ASSERT_NOT_NULL(db_full_path(drv, i, p, sizeof(p)));
        ASSERT_EQ_INT(i, db_path_index_get(db, p));
    }
    ASSERT_EQ_INT(-1, db_path_index_get(db, "C:\\Alpha\\Nope"));
    db_free(db);
    return 0;
}

TEST(path_index_case_rules) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    db_add_dir(drv, "Beta", r, false, false);
    char p[NCD_MAX_PATH];
    db_full_path(drv, 1, p, sizeof(p));
    for (char *q = p; *q; q++) *q = (char)toupper((unsigned char)*q);
#if NCD_PLATFORM_WINDOWS
    ASSERT_EQ_INT(1, db_path_index_get(db, p));
#else
    ASSERT_EQ_INT(-1, db_path_index_get(db, p));
#endif
    db_free(db);
    return 0;
}

TEST(path_index_rebuilt_after_subtree_removal) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    int b = db_add_dir(drv, "Beta", r, false, false);
    db_add_dir(drv, "Gamma", b, false, false);
    int d = db_add_dir(drv, "Delta", r, false, false);
    char pd[NCD_MAX_PATH]; db_full_path(drv, d, pd, sizeof(pd));
    ASSERT_EQ_INT(d, db_path_index_get(db, pd));
    ASSERT_TRUE(db_drive_remove_subtree(db, drv, b));
    ASSERT_EQ_INT(1, db_path_index_get(db, pd));   /* Delta moved from 3 to 1 */
    db_free(db);
    return 0;
}

TEST(children_index_lists_children) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int r = db_add_dir(drv, "Alpha", -1, false, false);
    db_add_dir(drv, "Beta", r, false, false);
    db_add_dir(drv, "Gamma", r, false, false);
    DirChildren *ch = db_build_children_index(drv);
    ASSERT_EQ_INT(2, ch[r].child_count);
    ASSERT_EQ_INT(1, ch[r].children[0]);
    ASSERT_EQ_INT(2, ch[r].children[1]);
    db_free_children_index(ch, drv->dir_count);
    db_free(db);
    return 0;
}

/* --- real filesystem rescans ------------------------------------------ */

static char g_root[NCD_MAX_PATH];

static void mk(const char *rel)
{
    char p[NCD_MAX_PATH];
    snprintf(p, sizeof(p), "%s%s%s", g_root, NCD_PATH_SEP, rel);
    TRD_MKDIR(p);
}

static void fs_setup(void)
{
    char cwd[NCD_MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));
    snprintf(g_root, sizeof(g_root), "%s%strd_tree", cwd, NCD_PATH_SEP);
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", g_root);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", g_root);
#endif
    system(cmd);
    TRD_MKDIR(g_root);
    mk("sub");
    mk("sub" NCD_PATH_SEP "a");
    mk("sub" NCD_PATH_SEP "b");
    mk("other");
}

static int count_name(const DriveData *drv, const char *name)
{
    int n = 0;
    for (int i = 0; i < drv->dir_count; i++)
        if (strcmp(drv->name_pool + drv->dirs[i].name_off, name) == 0) n++;
    return n;
}

TEST(subdir_rescan_twice_has_no_duplicates) {
    fs_setup();
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(g_root);
    ASSERT_TRUE(scan_subdirectory(db, letter, g_root, true, true, NULL, NULL, NULL) >= 0);
    char sub[NCD_MAX_PATH];
    snprintf(sub, sizeof(sub), "%s%ssub", g_root, NCD_PATH_SEP);
    ASSERT_TRUE(scan_subdirectory(db, letter, sub, true, true, NULL, NULL, NULL) >= 0);
    ASSERT_TRUE(scan_subdirectory(db, letter, sub, true, true, NULL, NULL, NULL) >= 0);
    DriveData *drv = db_find_drive(db, letter);
    ASSERT_NOT_NULL(drv);
    ASSERT_EQ_INT(1, count_name(drv, "sub"));
    ASSERT_EQ_INT(1, count_name(drv, "a"));
    ASSERT_EQ_INT(1, count_name(drv, "b"));
    ASSERT_EQ_INT(1, count_name(drv, "other"));
    /* parent of "sub" is still the tree root entry */
    int s = db_path_index_get(db, sub);
    ASSERT_TRUE(s >= 0);
    char p[NCD_MAX_PATH];
    db_full_path(drv, s, p, sizeof(p));
    ASSERT_EQ_STR(sub, p);
    db_free(db);
    return 0;
}

TEST(subdir_rescan_picks_up_new_and_removed_dirs) {
    fs_setup();
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(g_root);
    scan_subdirectory(db, letter, g_root, true, true, NULL, NULL, NULL);
    mk("sub" NCD_PATH_SEP "c_new");
    char gone[NCD_MAX_PATH];
    snprintf(gone, sizeof(gone), "%s%ssub%sb", g_root, NCD_PATH_SEP, NCD_PATH_SEP);
#if NCD_PLATFORM_WINDOWS
    _rmdir(gone);
#else
    rmdir(gone);
#endif
    char sub[NCD_MAX_PATH];
    snprintf(sub, sizeof(sub), "%s%ssub", g_root, NCD_PATH_SEP);
    scan_subdirectory(db, letter, sub, true, true, NULL, NULL, NULL);
    DriveData *drv = db_find_drive(db, letter);
    ASSERT_EQ_INT(1, count_name(drv, "c_new"));
    ASSERT_EQ_INT(0, count_name(drv, "b"));
    ASSERT_EQ_INT(1, count_name(drv, "sub"));
    db_free(db);
    return 0;
}

#if NCD_PLATFORM_WINDOWS
TEST(subdir_rescan_with_different_case_replaces_subtree) {
    fs_setup();
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(g_root);
    scan_subdirectory(db, letter, g_root, true, true, NULL, NULL, NULL);
    char sub_upper[NCD_MAX_PATH];
    snprintf(sub_upper, sizeof(sub_upper), "%s\\SUB", g_root);
    scan_subdirectory(db, letter, sub_upper, true, true, NULL, NULL, NULL);
    DriveData *drv = db_find_drive(db, letter);
    int subs = count_name(drv, "sub") + count_name(drv, "SUB");
    ASSERT_EQ_INT(1, subs);
    ASSERT_EQ_INT(1, count_name(drv, "a"));
    db_free(db);
    return 0;
}
#endif

TEST_MAIN(
    RUN_TEST(add_dir_is_append_only);
    RUN_TEST(add_dir_300k_appends_fast);
    RUN_TEST(path_index_finds_every_entry);
    RUN_TEST(path_index_case_rules);
    RUN_TEST(path_index_rebuilt_after_subtree_removal);
    RUN_TEST(children_index_lists_children);
    RUN_TEST(subdir_rescan_twice_has_no_duplicates);
    RUN_TEST(subdir_rescan_picks_up_new_and_removed_dirs);
#if NCD_PLATFORM_WINDOWS
    RUN_TEST(subdir_rescan_with_different_case_replaces_subtree);
#endif
)
```

Register it. `test/build-tests.bat`:
```bat
:: Rescan / append-only / path index functional tests
echo Building test_rescan_dedup.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_rescan_dedup.exe test_rescan_dedup.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error
```
`test/Makefile`: add `test_rescan_dedup` to `TESTS` and `test:`, plus:
```make
test_rescan_dedup: test_rescan_dedup.c test_framework.c $(SCANNER_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 2: Build and confirm the failure**

Run: `cd test && cmd /c build-tests.bat`
Expected: unresolved `db_path_index_get`, `db_build_children_index`, `db_free_children_index`.

- [ ] **Step 3: Make `db_add_dir` append-only**

In `src/database.c`, delete the duplicate-check loop at the top of `db_add_dir` (the `for (int i = 0; i < drv->dir_count; i++) { ... return i; }` block, with its comment), so the function starts at `/* ---- append name to the per-drive string pool ---- */`. Replace the comment above the function in `src/database.h` with:
```c
/*
 * Append a directory entry to a drive.  O(1) amortized; performs NO duplicate
 * check -- bulk scans and loads never produce duplicates, and callers that
 * insert into an existing tree (rescans, agent mkdir) look the path up with
 * db_path_index_get() first.  Returns the new id (== dir_count - 1).
 */
```

- [ ] **Step 4: Move the path index + children index into `database.c`**

In `src/database.c`, replace the local `PathMapEntry`/`PathMap` typedefs and `db_invalidate_path_map` (lines 70-100) with the full implementation:
```c
/* ================================================================ path index
 * full path -> dir index, cached on db->path_map, invalidated by any
 * structural change (db_add_drive, subtree removal, filtering, ...).
 * Windows keys are case-insensitive, Linux keys are case-sensitive.
 */
typedef struct { uint64_t hash; int dir_index; char *path; } PathMapEntry;
typedef struct { PathMapEntry *entries; int count; int capacity; } PathMap;

#if NCD_PLATFORM_WINDOWS
#define PATH_KEY_CHAR(c)  ((unsigned char)tolower((unsigned char)(c)))
#define PATH_KEY_EQ(a, b) (_stricmp((a), (b)) == 0)
#else
#define PATH_KEY_CHAR(c)  ((unsigned char)(c))
#define PATH_KEY_EQ(a, b) (strcmp((a), (b)) == 0)
#endif

static uint64_t path_key_hash(const char *s)
{
    uint64_t h = 14695981039346656037ULL;   /* FNV-1a */
    while (*s) { h ^= PATH_KEY_CHAR(*s); h *= 1099511628211ULL; s++; }
    return h;
}

static void path_map_init(PathMap *m, int expected)
{
    m->capacity = expected * 2;
    if (m->capacity < 16) m->capacity = 16;
    m->entries = ncd_calloc((size_t)m->capacity, sizeof(PathMapEntry));
    m->count = 0;
}

static void path_map_put(PathMap *m, const char *path, int dir_index)
{
    uint64_t h = path_key_hash(path);
    int idx = (int)(h % (uint64_t)m->capacity);
    while (m->entries[idx].path != NULL) idx = (idx + 1) % m->capacity;
    m->entries[idx].hash = h;
    m->entries[idx].dir_index = dir_index;
    m->entries[idx].path = ncd_strdup(path);
    m->count++;
}

static int path_map_get(const PathMap *m, const char *path)
{
    uint64_t h = path_key_hash(path);
    int idx = (int)(h % (uint64_t)m->capacity);
    while (m->entries[idx].path != NULL) {
        if (m->entries[idx].hash == h && PATH_KEY_EQ(m->entries[idx].path, path))
            return m->entries[idx].dir_index;
        idx = (idx + 1) % m->capacity;
    }
    return -1;
}

void db_invalidate_path_map(NcdDatabase *db)
{
    if (db && db->path_map) {
        PathMap *pm = (PathMap *)db->path_map;
        for (int i = 0; i < pm->capacity; i++)
            if (pm->entries[i].path) free(pm->entries[i].path);
        free(pm->entries);
        free(pm);
        db->path_map = NULL;
    }
}

DirChildren *db_build_children_index(const DriveData *drv)
{
    DirChildren *children = ncd_calloc((size_t)(drv->dir_count > 0 ? drv->dir_count : 1), sizeof(DirChildren));
    for (int i = 0; i < drv->dir_count; i++) {
        int parent = drv->dirs[i].parent;
        if (parent >= 0 && parent < drv->dir_count) children[parent].child_count++;
    }
    for (int i = 0; i < drv->dir_count; i++) {
        if (children[i].child_count > 0) {
            children[i].children = ncd_malloc_array((size_t)children[i].child_count, sizeof(int));
            children[i].child_capacity = children[i].child_count;
            children[i].child_count = 0;
        }
    }
    for (int i = 0; i < drv->dir_count; i++) {
        int parent = drv->dirs[i].parent;
        if (parent >= 0 && parent < drv->dir_count) {
            DirChildren *pc = &children[parent];
            pc->children[pc->child_count++] = i;
        }
    }
    return children;
}

void db_free_children_index(DirChildren *children, int dir_count)
{
    if (!children) return;
    for (int i = 0; i < dir_count; i++) free(children[i].children);
    free(children);
}
```
Then **move** `PathMapStackItem` and `build_path_map` (main.c lines 1897-2003) verbatim into `database.c` directly after `db_free_children_index`. Make `build_path_map` `static`, and replace its `build_children_index(` / `free_children_index(` calls with `db_build_children_index(` / `db_free_children_index(`. After it, add:
```c
static PathMap *db_ensure_path_map(NcdDatabase *db)
{
    if (db->path_map) return (PathMap *)db->path_map;
    PathMap *map = ncd_malloc(sizeof(PathMap));
    int total = 0;
    for (int d = 0; d < db->drive_count; d++) total += db->drives[d].dir_count;
    path_map_init(map, total);
    for (int d = 0; d < db->drive_count; d++)
        build_path_map(map, &db->drives[d], db->drives[d].letter);
    db->path_map = map;
    return map;
}

int db_path_index_get(NcdDatabase *db, const char *full_path)
{
    if (!db || !full_path || !full_path[0]) return -1;
    return path_map_get(db_ensure_path_map(db), full_path);
}
```
In `src/database.h`, after the `db_invalidate_path_map` declaration (line 528), add:
```c
/* Directory index for an absolute path, or -1.  Builds and caches the index
 * on first use (O(n)); later lookups are O(1). */
int db_path_index_get(NcdDatabase *db, const char *full_path);

/* Per-directory child lists (O(n) to build). */
typedef struct {
    int *children;
    int  child_count;
    int  child_capacity;
} DirChildren;
DirChildren *db_build_children_index(const DriveData *drv);
void db_free_children_index(DirChildren *children, int dir_count);
```

- [ ] **Step 5: Remove the moved code from `main.c` and update call sites**

In `src/main.c`, delete lines 1758-2003: the `PathMapEntry`/`PathMap` typedefs, `hash_path`, `path_map_init`, `path_map_put`, `path_map_get`, the `build_path_map` forward declaration, `db_ensure_path_map`, the `DirChildren` typedef, `build_children_index`, `free_children_index`, `PathMapStackItem`, and `build_path_map`. Then:
- Line ~2044 (`agent_mode_tree`): delete `PathMap *path_map = db_ensure_path_map(db);` and change `found_dir_idx = path_map_get(path_map, search_path);` to `found_dir_idx = db_path_index_get(db, search_path);`.
- Lines ~2087, 2123, 2198: `build_children_index(` → `db_build_children_index(`, `free_children_index(` → `db_free_children_index(`.
- Lines ~2307-2308: replace both lines with `in_db = db_path_index_get(db, search_path) >= 0;`.
- Lines ~2572-2579: delete `PathMap *path_map = db_ensure_path_map(db);`, and change the two lookups to `parent_idx = db_path_index_get(db, parent_path);` and `in_db = db_path_index_get(db, norm_path) >= 0;`.
- Just before the `db_add_dir(drv, leaf_name, parent_idx, false, false);` at ~2589, there is a `db_make_mutable(db)`. Add `db_invalidate_path_map(db);` after the `db_add_dir` call so the cache never misses the new entry.

Run `grep -n "path_map_get\|db_ensure_path_map\|build_children_index\|free_children_index" src/main.c`. Expected output: only the `db_`-prefixed names.

- [ ] **Step 6: Use the path index in `scan_subdirectory`**

In `src/scanner.c` `scan_subdirectory`, replace the whole `} else { /* Try to find the existing entry ... */ ... }` branch (the two linear `db_full_path` loops, ~lines 793-818) with:
```c
        } else {
            /* Duplicate check for rescans: one O(n) index build, O(1) lookups. */
            int existing_idx = db_path_index_get(db, norm_path);
            if (existing_idx >= 0) {
                db_drive_remove_subtree(db, drv, existing_idx);  /* invalidates the index */
            }
            char parent_path[MAX_PATH];
            if (path_parent(norm_path, parent_path, sizeof(parent_path))) {
                parent_id = db_path_index_get(db, parent_path);  /* rebuilt post-removal */
            }
        }
```
At the end of `scan_subdirectory`, immediately before `return result;`, add:
```c
    db_invalidate_path_map(db);   /* new entries were appended */
```
Note: `db_path_index_get` returns an index into whichever drive owns the key. Keys always start with that drive's root (`C:\` or the Linux mount label), so a hit for `norm_path` always belongs to `drv`.

- [ ] **Step 7: Replace the obsolete P0 dedup test**

In `test/test_matcher_extended.c`, replace `TEST(match_db_add_dir_rejects_duplicate) { ... }` (lines 641-676) with:
```c
/*
 * db_add_dir is append-only (startup-load-performance plan, Task 3).
 * Duplicate prevention lives in the rescan path (path index) and the
 * matcher's full-path dedup (match_dedup_duplicate_paths above).
 */
TEST(match_db_add_dir_appends) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int users = db_add_dir(drv, "Users", -1, false, false);
    int first = db_add_dir(drv, "Documents", users, false, false);
    int second = db_add_dir(drv, "Documents", users, false, false);
    ASSERT_TRUE(second == first + 1);
    ASSERT_EQ_INT(3, drv->dir_count);
    int count = 0;
    NcdMatch *m = matcher_find(db, "Documents", false, false, &count);
    ASSERT_NOT_NULL(m);
    ASSERT_EQ_INT(1, count);     /* matcher still collapses identical paths */
    free(m);
    db_free(db);
    return 0;
}
```
and at line 722 change `RUN_TEST(match_db_add_dir_rejects_duplicate);` to `RUN_TEST(match_db_add_dir_appends);`.

- [ ] **Step 8: Integration test: real `-r <subdir>` twice**

Add above `TEST_MAIN` in `test/test_load_integration.c`:
```c
/* ---------------------------------------------------------------- Task 3 */

static void it_mk(const ItEnv *e, const char *rel)
{
    char p[NCD_MAX_PATH];
    snprintf(p, sizeof(p), "%s%s%s", e->fs, NCD_PATH_SEP, rel);
    it_mkdir(p);
}

static int it_count(const char *hay, const char *needle)
{
    int n = 0;
    for (const char *p = strstr(hay, needle); p; p = strstr(p + 1, needle)) n++;
    return n;
}

TEST(it_subdir_rescan_twice_no_duplicates) {
    ItEnv e; it_env_init(&e, "it_rescan");
    it_mk(&e, "proj_r1");
    it_mk(&e, "proj_r1" NCD_PATH_SEP "rescan_unique_leaf_m9");
    char args[NCD_MAX_PATH * 2], out[65536];
    snprintf(args, sizeof(args), "-r \"%s\"", e.fs);
    it_run_ncd(&e, NULL, args, out, sizeof(out));
    snprintf(args, sizeof(args), "-r \"%s%sproj_r1\"", e.fs, NCD_PATH_SEP);
    it_run_ncd(&e, NULL, args, out, sizeof(out));
    it_run_ncd(&e, NULL, args, out, sizeof(out));
#if NCD_PLATFORM_WINDOWS
    it_run_ncd(&e, NULL, "/agent query rescan_unique_leaf_m9 --json --limit 50", out, sizeof(out));
#else
    it_run_ncd(&e, NULL, "--agent:query rescan_unique_leaf_m9 --json --limit 50", out, sizeof(out));
#endif
    ASSERT_EQ_INT(1, it_count(out, "rescan_unique_leaf_m9"));
    it_env_cleanup(&e);
    return 0;
}
```
and add `RUN_TEST(it_subdir_rescan_twice_no_duplicates);` to `TEST_MAIN`. Before relying on the `it_count == 1` assertion, check the JSON format of `--agent:query` once: if each match prints its path twice (e.g. in `path` and `name`), change the needle to a separator-prefixed form such as `NCD_PATH_SEP "rescan_unique_leaf_m9"` so it counts once per match.

- [ ] **Step 9: Full rebuild and run**

Run:
```powershell
cmd /c clean.bat; cmd /c build.bat; cd test; cmd /c build-tests.bat; cd ..
python test\runner.py unit
```
Expected: `test_rescan_dedup` all PASS, `test_matcher_extended` PASS, `test_load_integration` 4/4 PASS, and `test_agent_*`/`test_odd_cases` PASS (they use agent tree/mkdir and therefore the moved index).

- [ ] **Step 10: Commit**

```bash
git add src/database.c src/database.h src/main.c src/scanner.c test/test_rescan_dedup.c test/test_matcher_extended.c test/test_load_integration.c test/build-tests.bat test/Makefile
git commit -m "perf: append-only db_add_dir; rescans dedupe via hashed path index

Initial and full-drive scans fill empty drives and never needed the O(n)
per-insert scan. Subdirectory rescans find the old subtree and parent via
db_path_index_get (moved from main.c, case rules fixed per platform).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: One compiled exclusion index (hashed above 8 patterns) used everywhere; fast `db_full_path` and filter

**Files:**
- Modify: `src/database.c` (`db_full_path` ~line 1020-1080; `db_exclusion_check` ~2340-2420; `db_filter_excluded` 1266-1420), `src/database.h`
- Modify: `src/scanner.c:180-310` (delete `match_exclusion_pattern`/`scan_ctx_is_excluded` bodies), plus `scan_mount`/`scan_mounts`/`scan_subdirectory`/workers
- Modify: `src/main.c:415-560` (delete `match_path_exclusion`/`is_path_excluded`; rewrite `filter_matches_by_exclusions`)
- Create: `test/test_exclusion_index.c`
- Modify: `test/test_load_integration.c`, `test/build-tests.bat`, `test/Makefile`

**Interfaces:**
- Consumes: none.
- Produces:
```c
#define NCD_EXCLUSION_HASH_THRESHOLD 8
typedef struct NcdExclusionIndex NcdExclusionIndex;
NcdExclusionIndex *db_exclusion_index_build(const NcdExclusionList *list);  /* NULL list/empty -> valid empty index */
void db_exclusion_index_free(NcdExclusionIndex *idx);
bool db_exclusion_index_is_hashed(const NcdExclusionIndex *idx);           /* for tests */
bool db_exclusion_index_check(const NcdExclusionIndex *idx, char drive_letter, const char *rel_path);
const char *db_exclusion_rel_path(const char *full_path);                   /* skip "X:" and one leading separator */
```
`db_exclusion_index_check` returns exactly what `db_exclusion_check(meta, drive, rel_path)` returns for the same list. `db_exclusion_check` keeps its signature and becomes a thin wrapper that builds a temporary index.

**Semantics to preserve (derived from `db_exclusion_check`).** For an entry whose drive matches (`entry->drive == 0 || entry->drive == drive`), after normalisation (on Windows: `/`→`\`, drive prefix stripped, one leading separator stripped from both path and pattern):
- **root** (`match_from_root`): `wildcard_match(P, path)`, OR `path` starts (case-insensitively) with `P` and the next char is `\`, `/` or end.
- **non-root**: `wildcard_match(P, path)`; OR, if `P` starts with `*\` or `*/`, `wildcard_match(P+2, path)`; OR, if `has_parent_match`, `wildcard_match(P, suffix)` for every suffix following a `\` in `path` (the split is on `\` only, on both platforms).

For **literal** patterns (no `*`/`?`), `wildcard_match` reduces to case-insensitive equality, giving three hash sets keyed by `(drive, lowercase pattern)`:
- `root_set`: hit if the full path, or any prefix ending just before a `\`/`/`, is in the set.
- `exact_set` (all non-root literals): hit if the full path is in the set.
- `suffix_set` (non-root literals with `has_parent_match`): hit if any suffix after a `\` is in the set.

Lookups try both the specific drive key and drive `0`. Wildcard patterns stay in a linear list evaluated with the per-entry function. With 8 or fewer entries, everything is linear.

- [ ] **Step 1: Write the failing functional test `test/test_exclusion_index.c`**

```c
/* test_exclusion_index.c -- compiled exclusion index, unified semantics */
#include "test_framework.h"
#include "../src/database.h"
#include "../src/scanner.h"
#include "../src/ncd.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if NCD_PLATFORM_WINDOWS
#include <direct.h>
#define TEI_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define TEI_MKDIR(p) mkdir(p, 0755)
#endif

static const char *PATTERNS[] = {
    "$recycle.bin", "\\Windows", "C:\\Program Files", "node_modules", "build\\obj",
    "*\\.git", "*.tmp", "\\proc", "cache", "D:\\Data\\old", "temp?", "\\Users\\x\\AppData",
    "dist", "logs\\archive", "*\\target\\*", "\\opt\\big"
};
static const char *PATHS[] = {
    "$Recycle.Bin", "$RECYCLE.BIN\\S-1", "Windows", "Windows\\System32", "WindowsApps",
    "Program Files", "Program Files\\x", "src\\node_modules", "node_modules", "a\\build\\obj",
    "build\\obj", "repo\\.git", ".git", "x.tmp", "dir\\x.tmp", "proc", "proc\\1", "cache",
    "a\\cache", "Data\\old", "temp1", "temp12", "Users\\x\\AppData\\Local", "dist", "logs\\archive",
    "p\\logs\\archive", "w\\target\\debug", "target\\debug", "opt\\big", "opt\\bigger", ""
};

/* Build metadata with the first n patterns. */
static NcdMetadata *meta_with(int n)
{
    NcdMetadata *m = db_metadata_create();
    for (int i = 0; i < n; i++) db_exclusion_add(m, PATTERNS[i]);
    return m;
}

static int differential(int n)
{
    NcdMetadata *m = meta_with(n);
    NcdExclusionIndex *idx = db_exclusion_index_build(&m->exclusions);
    ASSERT_TRUE(db_exclusion_index_is_hashed(idx) == (n > NCD_EXCLUSION_HASH_THRESHOLD));
    const char drives[] = { 'C', 'D', 0 };
    for (size_t d = 0; d < sizeof(drives); d++) {
        for (size_t i = 0; i < sizeof(PATHS) / sizeof(PATHS[0]); i++) {
            /* Oracle: the per-entry linear rules, evaluated pattern by pattern. */
            bool expect = false;
            for (int k = 0; k < m->exclusions.count && !expect; k++) {
                NcdExclusionList one = { &m->exclusions.entries[k], 1, 1 };
                NcdExclusionIndex *single = db_exclusion_index_build(&one);
                expect = db_exclusion_index_check(single, drives[d], PATHS[i]);
                db_exclusion_index_free(single);
            }
            bool got = db_exclusion_index_check(idx, drives[d], PATHS[i]);
            if (got != expect) {
                printf("  n=%d drive=%c path='%s' expect=%d got=%d\n", n, drives[d] ? drives[d] : '0', PATHS[i], expect, got);
                ASSERT_TRUE(0);
            }
        }
    }
    db_exclusion_index_free(idx);
    db_metadata_free(m);
    return 0;
}

TEST(index_linear_matches_rules_small_list) { return differential(6); }
TEST(index_hashed_matches_linear_large_list) { return differential(16); }
TEST(index_threshold_is_eight) {
    NcdMetadata *m8 = meta_with(8), *m9 = meta_with(9);
    NcdExclusionIndex *a = db_exclusion_index_build(&m8->exclusions);
    NcdExclusionIndex *b = db_exclusion_index_build(&m9->exclusions);
    ASSERT_FALSE(db_exclusion_index_is_hashed(a));
    ASSERT_TRUE(db_exclusion_index_is_hashed(b));
    db_exclusion_index_free(a); db_exclusion_index_free(b);
    db_metadata_free(m8); db_metadata_free(m9);
    return 0;
}

TEST(index_known_answers) {
    NcdMetadata *m = meta_with(16);
    NcdExclusionIndex *idx = db_exclusion_index_build(&m->exclusions);
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "$Recycle.Bin"));
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "Windows\\System32"));
    ASSERT_FALSE(db_exclusion_index_check(idx, 'C', "WindowsApps"));
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "Program Files\\x"));
    ASSERT_FALSE(db_exclusion_index_check(idx, 'D', "Program Files\\x"));   /* C: only */
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "a\\build\\obj"));        /* parent\child suffix */
    ASSERT_TRUE(db_exclusion_index_check(idx, 'C', "repo\\.git"));           /* wildcard */
#endif
    ASSERT_FALSE(db_exclusion_index_check(idx, 'C', "unrelated\\dir"));
    db_exclusion_index_free(idx);
    db_metadata_free(m);
    return 0;
}

TEST(rel_path_strips_drive_and_separator) {
    ASSERT_EQ_STR("Windows\\System32", db_exclusion_rel_path("C:\\Windows\\System32"));
    ASSERT_EQ_STR("home/x", db_exclusion_rel_path("/home/x"));
    ASSERT_EQ_STR("rel\\dir", db_exclusion_rel_path("rel\\dir"));
    return 0;
}

TEST(full_path_fast_on_large_drive) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    char name[32];
    for (int i = 0; i < 200000; i++) {
        snprintf(name, sizeof(name), "n%07d", i);
        db_add_dir(drv, name, i < 50 ? -1 : (i / 50) - 1, false, false);
    }
    char p[NCD_MAX_PATH];
    clock_t t0 = clock();
    for (int i = 0; i < drv->dir_count; i++) db_full_path(drv, i, p, sizeof(p));
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("  200K db_full_path calls: %.3f s\n", secs);
    ASSERT_TRUE(secs < 1.0);   /* old per-call heap bitset + memset: several seconds */
    db_free(db);
    return 0;
}

TEST(full_path_still_detects_cycles_and_bad_parents) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    db_add_dir(drv, "a", -1, false, false);
    db_add_dir(drv, "b", 0, false, false);
    drv->dirs[0].parent = 1;                 /* cycle a <-> b */
    char p[NCD_MAX_PATH];
    ASSERT_NULL(db_full_path(drv, 1, p, sizeof(p)));
    drv->dirs[0].parent = 99;                /* out of range */
    ASSERT_NULL(db_full_path(drv, 1, p, sizeof(p)));
    db_free(db);
    return 0;
}

TEST(filter_excluded_same_result_large_list_and_no_copy_when_clean) {
    NcdDatabase *db = db_create();
    DriveData *drv = db_add_drive(db, 'C');
    int win = db_add_dir(drv, "Windows", -1, false, false);
    db_add_dir(drv, "System32", win, false, false);
    int src = db_add_dir(drv, "src", -1, false, false);
    int nm = db_add_dir(drv, "node_modules", src, false, false);
    db_add_dir(drv, "lodash", nm, false, false);
    db_add_dir(drv, "keep", src, false, false);
    NcdMetadata *m = meta_with(16);
    int removed = db_filter_excluded(db, m);
#if NCD_PLATFORM_WINDOWS
    ASSERT_TRUE(removed >= 2);               /* Windows + System32 */
#endif
    char p[NCD_MAX_PATH];
    bool keep_found = false;
    for (int i = 0; i < drv->dir_count; i++) {
        db_full_path(drv, i, p, sizeof(p));
        if (strstr(p, "keep")) keep_found = true;
        ASSERT_TRUE(strstr(p, "System32") == NULL);
    }
    ASSERT_TRUE(keep_found);

    /* clean db: nothing excluded -> blob stays a blob (no mutable copy) */
    NcdDatabase *clean = db_create();
    DriveData *cd = db_add_drive(clean, 'C');
    db_add_dir(cd, "nothing_here", -1, false, false);
    clean->is_blob = false;
    ASSERT_EQ_INT(0, db_filter_excluded(clean, m));
    db_metadata_free(m);
    db_free(db);
    db_free(clean);
    return 0;
}

/* Scanner now uses the same rules: a literal non-root pattern skips the dir. */
TEST(scanner_skips_literal_excluded_dir) {
    char cwd[NCD_MAX_PATH], root[NCD_MAX_PATH], p[NCD_MAX_PATH];
    platform_get_current_dir(cwd, sizeof(cwd));
    snprintf(root, sizeof(root), "%s%stei_tree", cwd, NCD_PATH_SEP);
    char cmd[NCD_MAX_PATH * 2];
#if NCD_PLATFORM_WINDOWS
    snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\" 2>nul", root);
#else
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", root);
#endif
    system(cmd);
    TEI_MKDIR(root);
    snprintf(p, sizeof(p), "%s%skeepme", root, NCD_PATH_SEP); TEI_MKDIR(p);
    snprintf(p, sizeof(p), "%s%sskipme_lit", root, NCD_PATH_SEP); TEI_MKDIR(p);
    snprintf(p, sizeof(p), "%s%sskipme_lit%sinner", root, NCD_PATH_SEP, NCD_PATH_SEP); TEI_MKDIR(p);

    NcdMetadata *m = db_metadata_create();
    /* Rooted literal relative to the drive root, built from the real path. */
    char pat[NCD_MAX_PATH];
    snprintf(pat, sizeof(pat), "%s%s%sskipme_lit", NCD_PATH_SEP, db_exclusion_rel_path(root), NCD_PATH_SEP);
    ASSERT_TRUE(db_exclusion_add(m, pat));
    for (int i = 0; i < 10; i++) {             /* push it over the hash threshold */
        char filler[32]; snprintf(filler, sizeof(filler), "filler_%d", i);
        db_exclusion_add(m, filler);
    }
    NcdDatabase *db = db_create();
    char letter = platform_get_drive_letter(root);
    scan_subdirectory(db, letter, root, true, true, NULL, NULL, &m->exclusions);
    DriveData *drv = db_find_drive(db, letter);
    ASSERT_NOT_NULL(drv);
    bool keep = false, skip = false, inner = false;
    for (int i = 0; i < drv->dir_count; i++) {
        const char *n = drv->name_pool + drv->dirs[i].name_off;
        if (!strcmp(n, "keepme")) keep = true;
        if (!strcmp(n, "skipme_lit")) skip = true;
        if (!strcmp(n, "inner")) inner = true;
    }
    ASSERT_TRUE(keep);
    ASSERT_FALSE(skip);
    ASSERT_FALSE(inner);
    db_free(db);
    db_metadata_free(m);
    return 0;
}

TEST_MAIN(
    RUN_TEST(index_linear_matches_rules_small_list);
    RUN_TEST(index_hashed_matches_linear_large_list);
    RUN_TEST(index_threshold_is_eight);
    RUN_TEST(index_known_answers);
    RUN_TEST(rel_path_strips_drive_and_separator);
    RUN_TEST(full_path_fast_on_large_drive);
    RUN_TEST(full_path_still_detects_cycles_and_bad_parents);
    RUN_TEST(filter_excluded_same_result_large_list_and_no_copy_when_clean);
    RUN_TEST(scanner_skips_literal_excluded_dir);
)
```
In `differential`, the oracle evaluates a single-entry index per pattern; that is always linear, so it checks the hashed index against the linear rules.

Register it. `test/build-tests.bat`:
```bat
:: Exclusion index functional tests
echo Building test_exclusion_index.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_exclusion_index.exe test_exclusion_index.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error
```
`test/Makefile`: add `test_exclusion_index` to `TESTS` and `test:`, plus:
```make
test_exclusion_index: test_exclusion_index.c test_framework.c $(SCANNER_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
```

- [ ] **Step 2: Build and confirm the failure**

Run: `cd test && cmd /c build-tests.bat`
Expected: unresolved `db_exclusion_index_build` and related symbols.

- [ ] **Step 3: Make `db_full_path` allocation-free**

In `src/database.c` `db_full_path`, replace the first pass (from the `int visited_words` declaration through the `if (!visited_on_stack) free(visited);` after the loop) with:
```c
    int depth = 0;
    int cur = dir_index;
    bool has_cycle = false;
    while (cur >= 0) {
        if (cur >= drv->dir_count || ++depth > drv->dir_count) {
            has_cycle = true;      /* corrupt parent link or cycle */
            break;
        }
        cur = drv->dirs[cur].parent;
    }
```
The `depth > dir_count` bound already proves a cycle, so the per-call visited bitset (a heap allocation plus a memset of `dir_count/8` bytes on every call for drives over 1024 dirs) is pure overhead. The second pass is unchanged.

- [ ] **Step 4: Implement the exclusion index in `database.c`**

First refactor the per-entry logic out of `db_exclusion_check`. Move the body of its `for` loop into:
```c
/* One entry vs one drive-relative path; exact historical rules.
 * path_to_match: normalized ('/'->'\' on Windows), no drive, no leading sep. */
static bool exclusion_entry_matches(const NcdExclusionEntry *entry, char drive, const char *path_to_match);
```
The function contains the drive check (returning false where the loop said `continue`), the pattern normalisation, and the root / non-root branches, returning true where the loop returned true and false at the end.

Then add (in the `exclusion list` section, after `wildcard_match`):
```c
#define EXCL_KEY_MAX NCD_MAX_PATH

typedef struct { uint64_t hash; char *key; } ExclSlot;
typedef struct { ExclSlot *slots; int cap; int count; } ExclSet;

struct NcdExclusionIndex {
    const NcdExclusionEntry *entries;   /* borrowed: list must outlive index */
    int  count;
    bool hashed;
    ExclSet root_set, exact_set, suffix_set;
    const NcdExclusionEntry **wild;     /* wildcard entries when hashed */
    int  wild_count;
};

static uint64_t excl_hash(char drive, const char *s, size_t n)
{
    uint64_t h = 14695981039346656037ULL;
    h ^= (unsigned char)drive; h *= 1099511628211ULL;
    for (size_t i = 0; i < n; i++) { h ^= (unsigned char)tolower((unsigned char)s[i]); h *= 1099511628211ULL; }
    return h;
}

static void excl_set_init(ExclSet *s, int expected)
{
    s->cap = 16; while (s->cap < expected * 2) s->cap <<= 1;
    s->slots = ncd_calloc((size_t)s->cap, sizeof(ExclSlot));
    s->count = 0;
}

static void excl_set_free(ExclSet *s)
{
    if (!s->slots) return;
    for (int i = 0; i < s->cap; i++) free(s->slots[i].key);
    free(s->slots);
    s->slots = NULL;
}

/* key layout: [drive byte][lowercase pattern] */
static void excl_set_add(ExclSet *s, char drive, const char *pat)
{
    size_t n = strlen(pat);
    uint64_t h = excl_hash(drive, pat, n);
    int i = (int)(h & (uint64_t)(s->cap - 1));
    while (s->slots[i].key) i = (i + 1) & (s->cap - 1);
    char *k = ncd_malloc(n + 2);
    k[0] = drive ? drive : '*';
    for (size_t j = 0; j < n; j++) k[j + 1] = (char)tolower((unsigned char)pat[j]);
    k[n + 1] = '\0';
    s->slots[i].hash = h;
    s->slots[i].key = k;
    s->count++;
}

static bool excl_set_has(const ExclSet *s, char drive, const char *str, size_t n)
{
    if (!s->slots || s->count == 0) return false;
    uint64_t h = excl_hash(drive, str, n);
    int i = (int)(h & (uint64_t)(s->cap - 1));
    char want = drive ? drive : '*';
    while (s->slots[i].key) {
        const char *k = s->slots[i].key;
        if (s->slots[i].hash == h && k[0] == want && strlen(k + 1) == n &&
            _strnicmp(k + 1, str, n) == 0)
            return true;
        i = (i + 1) & (s->cap - 1);
    }
    return false;
}

static bool excl_set_has_any_drive(const ExclSet *s, char drive, const char *str, size_t n)
{
    return excl_set_has(s, 0, str, n) || (drive && excl_set_has(s, drive, str, n));
}

/* Normalize a pattern the same way exclusion_entry_matches does. */
static void excl_norm_pattern(const NcdExclusionEntry *e, char *out, size_t out_size)
{
    const char *p = e->pattern;
#if NCD_PLATFORM_WINDOWS
    if (p[0] && p[1] == ':') p += 2;
#endif
    platform_strncpy_s(out, out_size, p);
#if NCD_PLATFORM_WINDOWS
    for (char *q = out; *q; q++) if (*q == '/') *q = NCD_PATH_SEP_CHAR;
#endif
    if (e->match_from_root && (out[0] == '\\' || out[0] == '/'))
        memmove(out, out + 1, strlen(out));
}

static bool excl_is_literal(const char *p) { return strpbrk(p, "*?") == NULL; }

NcdExclusionIndex *db_exclusion_index_build(const NcdExclusionList *list)
{
    NcdExclusionIndex *idx = ncd_calloc(1, sizeof(*idx));
    if (!list || list->count == 0) return idx;
    idx->entries = list->entries;
    idx->count = list->count;
    idx->hashed = list->count > NCD_EXCLUSION_HASH_THRESHOLD;
    if (!idx->hashed) return idx;

    excl_set_init(&idx->root_set, list->count);
    excl_set_init(&idx->exact_set, list->count);
    excl_set_init(&idx->suffix_set, list->count);
    idx->wild = ncd_malloc_array((size_t)list->count, sizeof(*idx->wild));
    char norm[EXCL_KEY_MAX];
    for (int i = 0; i < list->count; i++) {
        const NcdExclusionEntry *e = &list->entries[i];
        excl_norm_pattern(e, norm, sizeof(norm));
        if (!excl_is_literal(norm)) { idx->wild[idx->wild_count++] = e; continue; }
        char drive = e->drive;
#if !NCD_PLATFORM_WINDOWS
        drive = 0;
#endif
        if (e->match_from_root) {
            excl_set_add(&idx->root_set, drive, norm);
        } else {
            excl_set_add(&idx->exact_set, drive, norm);
            if (e->has_parent_match) excl_set_add(&idx->suffix_set, drive, norm);
        }
    }
    return idx;
}

void db_exclusion_index_free(NcdExclusionIndex *idx)
{
    if (!idx) return;
    excl_set_free(&idx->root_set);
    excl_set_free(&idx->exact_set);
    excl_set_free(&idx->suffix_set);
    free(idx->wild);
    free(idx);
}

bool db_exclusion_index_is_hashed(const NcdExclusionIndex *idx) { return idx && idx->hashed; }

const char *db_exclusion_rel_path(const char *full_path)
{
    if (!full_path) return "";
    const char *p = full_path;
#if NCD_PLATFORM_WINDOWS
    if (p[0] && p[1] == ':') p += 2;
#endif
    if (*p == '\\' || *p == '/') p++;
    return p;
}

bool db_exclusion_index_check(const NcdExclusionIndex *idx, char drive_letter, const char *rel_path)
{
    if (!idx || idx->count == 0 || !rel_path) return false;
#if NCD_PLATFORM_WINDOWS
    char drive = drive_letter ? (char)toupper((unsigned char)drive_letter) : 0;
#else
    (void)drive_letter;
    char drive = 0;
#endif
    char norm[NCD_MAX_PATH];
    platform_strncpy_s(norm, sizeof(norm), rel_path);
#if NCD_PLATFORM_WINDOWS
    for (char *q = norm; *q; q++) if (*q == '/') *q = NCD_PATH_SEP_CHAR;
#endif
    const char *path = norm;
    if (path[0] == '\\' || path[0] == '/') path++;

    if (!idx->hashed) {
        for (int i = 0; i < idx->count; i++)
            if (exclusion_entry_matches(&idx->entries[i], drive, path)) return true;
        return false;
    }

    size_t len = strlen(path);
    /* exact (non-root literals) and root literals matching the whole path */
    if (excl_set_has_any_drive(&idx->exact_set, drive, path, len)) return true;
    if (excl_set_has_any_drive(&idx->root_set, drive, path, len)) return true;
    /* root literals as directory prefixes */
    for (size_t i = 0; i < len; i++) {
        if ((path[i] == '\\' || path[i] == '/') &&
            excl_set_has_any_drive(&idx->root_set, drive, path, i)) return true;
    }
    /* parent\child literals against every suffix after a backslash */
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '\\' &&
            excl_set_has_any_drive(&idx->suffix_set, drive, path + i + 1, len - i - 1)) return true;
    }
    for (int i = 0; i < idx->wild_count; i++)
        if (exclusion_entry_matches(idx->wild[i], drive, path)) return true;
    return false;
}

bool db_exclusion_check(NcdMetadata *meta, char drive_letter, const char *dir_path)
{
    if (!meta || !dir_path) return false;
    NcdExclusionIndex *idx = db_exclusion_index_build(&meta->exclusions);
    bool r = db_exclusion_index_check(idx, drive_letter, dir_path);
    db_exclusion_index_free(idx);
    return r;
}
```
The old `db_exclusion_check` body is now `exclusion_entry_matches` plus the wrapper above. Its normalisation (`norm_path`, leading-separator strip) moves into `db_exclusion_index_check`. Delete the old body.

In `src/database.h`, next to `db_exclusion_check` (line 430), add the `Produces` declarations from this task's Interfaces block (`NCD_EXCLUSION_HASH_THRESHOLD`, the opaque typedef, and the five functions).

- [ ] **Step 5: Rewrite `db_filter_excluded` as a single pass that copies only when needed**

Replace the body of `db_filter_excluded` (from `if (db->is_blob) db_make_mutable(db);` through the end of the per-drive loop) with:
```c
    NcdExclusionIndex *idx = db_exclusion_index_build(&meta->exclusions);
    int total_removed = 0;
    char full_path[NCD_MAX_PATH];

    for (int d = 0; d < db->drive_count; d++) {
        DriveData *drv = &db->drives[d];
        if (drv->dir_count == 0) continue;
        bool *is_excluded = ncd_calloc((size_t)drv->dir_count, sizeof(bool));
        int removed_here = 0;

        /* Parents are always stored before children (scanner and loader both
         * append parent-first), so one forward pass propagates exclusion. */
        for (int i = 0; i < drv->dir_count; i++) {
            int parent = drv->dirs[i].parent;
            if (parent >= 0 && parent < i && is_excluded[parent]) {
                is_excluded[i] = true; removed_here++; continue;
            }
            if (!db_full_path(drv, i, full_path, sizeof(full_path))) continue;
            if (db_exclusion_index_check(idx, drv->letter, db_exclusion_rel_path(full_path))) {
                is_excluded[i] = true; removed_here++;
            }
        }
        /* Safety net for any out-of-order parent links (none expected). */
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = 0; i < drv->dir_count; i++) {
                int parent = drv->dirs[i].parent;
                if (!is_excluded[i] && parent >= 0 && parent < drv->dir_count && is_excluded[parent]) {
                    is_excluded[i] = true; removed_here++; changed = true;
                }
            }
        }
        if (removed_here == 0) { free(is_excluded); continue; }

        if (db->is_blob) db_make_mutable(db);   /* copy only when something is removed */
        drv = &db->drives[d];
```
...then keep the existing compaction code unchanged: `index_map` construction, the new `dirs`/pool rebuild, the parent remap, and freeing the old arrays. It now runs after this point and uses `is_excluded`. Allocate `index_map` here (`int *index_map = ncd_malloc_array((size_t)drv->dir_count, sizeof(int));`), because the old allocation at the top of the loop is gone. Add `total_removed += removed_here;`. After the drive loop, call `db_exclusion_index_free(idx);` before the existing invalidation / `return total_removed;`.

Also remove the now-redundant `if (db->is_blob) db_make_mutable(db);` at the top of the function.

- [ ] **Step 6: Unify the scanner on the index**

In `src/scanner.c`:
1. Delete `match_exclusion_pattern` and the body of `scan_ctx_is_excluded` (lines ~200-310).
2. In `struct ScanCtx` (line ~187), add `const NcdExclusionIndex *excl_index;` below `exclusions`. In `ScanThreadData` (line ~315), add `const NcdExclusionIndex *excl_index;`.
3. The new `scan_ctx_is_excluded`:
```c
static bool scan_ctx_is_excluded(struct ScanCtx *ctx, char drive_letter, const char *dir_path)
{
    if (!ctx || !ctx->excl_index) return false;
    return db_exclusion_index_check(ctx->excl_index, drive_letter, db_exclusion_rel_path(dir_path));
}
```
4. In `scan_mounts`: after the `if (count == 0) return 0;` check, add `NcdExclusionIndex *excl_index = db_exclusion_index_build(exclusions);`. Set `td[i].excl_index = excl_index;` next to `td[i].exclusions = exclusions;`. Call `db_exclusion_index_free(excl_index);` just before `return total;`. In `worker_thread`, where the `ScanCtx` initialiser sets `.exclusions = td->exclusions`, add `.excl_index = td->excl_index`.
5. In `scan_mount` (line ~362): build the index at the top (`NcdExclusionIndex *excl_index = db_exclusion_index_build(exclusions);`), pass it into both `ScanCtx` initialisers (line ~441 and the `td` at ~465 via `td.excl_index = excl_index;`), and free it before every `return`.
6. In `scan_subdirectory`: next to `ctx.exclusions = exclusions;` (line ~881), add `NcdExclusionIndex *excl_index = db_exclusion_index_build(exclusions); ctx.excl_index = excl_index;`, and call `db_exclusion_index_free(excl_index);` right after `platform_scan_directory` returns.

- [ ] **Step 7: Unify the result filter in `main.c`**

Delete `match_path_exclusion` and `is_path_excluded` (main.c lines ~415-523). Replace `filter_matches_by_exclusions` with:
```c
static int filter_matches_by_exclusions(NcdMatch *matches, int match_count, const NcdExclusionList *exclusions)
{
    if (!matches || match_count <= 0 || !exclusions || exclusions->count == 0) return match_count;
    NcdExclusionIndex *idx = db_exclusion_index_build(exclusions);
    int w = 0;
    for (int r = 0; r < match_count; r++) {
        char drive = 0;
#if NCD_PLATFORM_WINDOWS
        if (matches[r].full_path[0] && matches[r].full_path[1] == ':')
            drive = (char)toupper((unsigned char)matches[r].full_path[0]);
#endif
        if (db_exclusion_index_check(idx, drive, db_exclusion_rel_path(matches[r].full_path))) continue;
        if (w != r) matches[w] = matches[r];
        w++;
    }
    db_exclusion_index_free(idx);
    return w;
}
```
Before replacing it, read the old `filter_matches_by_exclusions` body (main.c ~525-560) and keep any per-match cleanup it does when dropping a match. `NcdMatch` holds the path inline in this codebase (`full_path` array); if the old body freed fields, do the same in the `continue` branch.

- [ ] **Step 8: Run unit tests; fix expectations that relied on the old divergent matchers**

Run: `cmd /c build.bat; cd test; cmd /c build-tests.bat; cd ..; python test\runner.py unit`
Expected: `test_exclusion_index` 9/9 PASS. Then check `test_scanner`, `test_scanner_extended`, `test_ui_exclusions`, `test_database_extended`, `test_main`, `test_odd_cases`. Any failure must come from a test that encoded one of the deleted matchers' divergent rules, such as `match_path_exclusion`'s prefix-anywhere behaviour. For each one, confirm the old expectation contradicts `db_exclusion_check` semantics, then update the expectation to the unified rule and note it in the commit message. Do not change the index to accommodate a divergent test.

- [ ] **Step 9: Integration test: more than 8 exclusions, real rescan**

Add above `TEST_MAIN` in `test/test_load_integration.c`:
```c
/* ---------------------------------------------------------------- Task 4 */

TEST(it_rescan_honors_large_exclusion_list) {
    ItEnv e; it_env_init(&e, "it_excl");
    it_mk(&e, "keep_dir_e1");
    it_mk(&e, "keep_dir_e1" NCD_PATH_SEP "keep_leaf_e2");
    it_mk(&e, "skip_lit_e3");
    it_mk(&e, "skip_lit_e3" NCD_PATH_SEP "skip_inner_e4");
    it_mk(&e, "wild_e5.tmpx");
    char out[65536], args[NCD_MAX_PATH * 2];
    /* 10 exclusions: over the hash threshold; one literal, one wildcard, 8 fillers */
    it_run_ncd(&e, NULL, "-x skip_lit_e3", out, sizeof(out));
    it_run_ncd(&e, NULL, "-x *.tmpx", out, sizeof(out));
    for (int i = 0; i < 8; i++) {
        snprintf(args, sizeof(args), "-x filler_excl_%d", i);
        it_run_ncd(&e, NULL, args, out, sizeof(out));
    }
    snprintf(args, sizeof(args), "-r \"%s\"", e.fs);
    it_run_ncd(&e, NULL, args, out, sizeof(out));
#if NCD_PLATFORM_WINDOWS
    it_run_ncd(&e, NULL, "/agent query keep_leaf_e2 --json", out, sizeof(out));
    ASSERT_STR_CONTAINS(out, "keep_leaf_e2");
    it_run_ncd(&e, NULL, "/agent query skip_inner_e4 --json", out, sizeof(out));
    ASSERT_TRUE(strstr(out, "skip_inner_e4") == NULL || strstr(out, "\"path\"") == NULL);
    it_run_ncd(&e, NULL, "/agent query wild_e5 --json", out, sizeof(out));
    ASSERT_TRUE(strstr(out, "\"path\"") == NULL);
#else
    it_run_ncd(&e, NULL, "--agent:query keep_leaf_e2 --json", out, sizeof(out));
    ASSERT_STR_CONTAINS(out, "keep_leaf_e2");
    it_run_ncd(&e, NULL, "--agent:query skip_inner_e4 --json", out, sizeof(out));
    ASSERT_TRUE(strstr(out, "skip_inner_e4") == NULL || strstr(out, "\"path\"") == NULL);
    it_run_ncd(&e, NULL, "--agent:query wild_e5 --json", out, sizeof(out));
    ASSERT_TRUE(strstr(out, "\"path\"") == NULL);
#endif
    it_env_cleanup(&e);
    return 0;
}
```
Add `RUN_TEST(it_rescan_honors_large_exclusion_list);` to `TEST_MAIN`. Before finalising, confirm the CLI flag that adds an exclusion: check `ncd --help` / `src/cli.c` for the exclusion-add option (`-x` here). Use the real flag, and confirm the `--agent:query` no-match JSON shape so the "no `"path"` key" assertion matches it.

- [ ] **Step 10: Full rebuild and run**

Run:
```powershell
cmd /c clean.bat; cmd /c build.bat; cd test; cmd /c build-tests.bat; cd ..
python test\runner.py unit
```
Expected: everything PASS, including `test_load_integration` 5/5.

- [ ] **Step 11: Commit**

```bash
git add src/database.c src/database.h src/scanner.c src/main.c test/test_exclusion_index.c test/test_load_integration.c test/build-tests.bat test/Makefile <any updated test files from Step 8>
git commit -m "perf: one compiled exclusion index (hashed >8 patterns) for scan, load and results

- Scanner's separate matcher never matched literal patterns against full
  paths; scanner now uses db_exclusion_check semantics on drive-relative paths.
- db_filter_excluded: single forward pass, copies the blob only if needed.
- db_full_path: drop per-call heap bitset (depth bound already detects cycles).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Standalone client loads metadata once

**Files:**
- Modify: `src/database.c` (`db_metadata_load`, line ~2813), `src/database.h`
- Modify: `src/main.c:376-412` (`get_metadata`), `6365-6370` and `6515-6518` (search_meta), `6636-6640` (exit cleanup), `main()` start (env-gated report)
- Modify: `test/test_drive_load.c`, `test/test_load_integration.c`

**Interfaces:**
- Produces: `int db_metadata_load_call_count(void);` — the number of `db_metadata_load()` calls in this process.
- Produces: with `NCD_TEST_REPORT_LOADS=1` set, the client prints `NCD_TEST_METADATA_LOADS=<n>` to stderr at exit.

- [ ] **Step 1: Write the failing tests**

Append to `test/test_drive_load.c` (above `TEST_MAIN`) and register it:
```c
TEST(metadata_load_counter_counts_calls) {
    env_setup("metacount");
    int before = db_metadata_load_call_count();
    NcdMetadata *m = db_metadata_load();
    if (m) db_metadata_free(m);
    ASSERT_EQ_INT(before + 1, db_metadata_load_call_count());
    return 0;
}
```
`RUN_TEST(metadata_load_counter_counts_calls);`

Append to `test/test_load_integration.c` (above `TEST_MAIN`) and register it:
```c
/* ---------------------------------------------------------------- Task 5 */

TEST(it_standalone_search_loads_metadata_once) {
    ItEnv e; it_env_init(&e, "it_meta");
    it_write_case_variant_db(&e, agent_test_drive_letter(), 0);
    char out[8192];
    it_run_ncd(&e, NULL, "-x filler_meta_excl", out, sizeof(out));   /* creates ncd.metadata */
#if NCD_PLATFORM_WINDOWS
    it_run_ncd(&e, "set NCD_TEST_REPORT_LOADS=1", "it_deep_leaf_k5", out, sizeof(out));
#else
    it_run_ncd(&e, "NCD_TEST_REPORT_LOADS=1", "it_deep_leaf_k5", out, sizeof(out));
#endif
    ASSERT_STR_CONTAINS(out, "NCD_TEST_METADATA_LOADS=1");
    it_env_cleanup(&e);
    return 0;
}
```
`RUN_TEST(it_standalone_search_loads_metadata_once);`

- [ ] **Step 2: Build and confirm the failure**

Run: `cd test && cmd /c build-tests.bat`
Expected: unresolved `db_metadata_load_call_count`.

- [ ] **Step 3: Add the counter**

`src/database.c`, above `db_metadata_load`:
```c
static volatile long g_metadata_load_calls = 0;
int db_metadata_load_call_count(void) { return (int)g_metadata_load_calls; }
```
As the first statement of `db_metadata_load`: `g_metadata_load_calls++;`
`src/database.h`, next to the `db_metadata_load` prototype:
```c
/* Number of db_metadata_load() calls in this process (diagnostics/tests). */
int db_metadata_load_call_count(void);
```

- [ ] **Step 4: Reuse the backend's metadata in `main.c`**

Replace `get_metadata` (main.c ~394-400) with:
```c
static bool g_metadata_borrowed = false;   /* g_metadata owned by the local state backend */

/* Helper: Ensure metadata is loaded.  In standalone mode the local backend has
 * already loaded ncd.metadata -- borrow it instead of reading the file again. */
static NcdMetadata* get_metadata(void)
{
    if (!g_metadata) {
        if (g_state_view && !g_state_info.from_service) {
            g_metadata = (NcdMetadata *)state_view_metadata(g_state_view);
            g_metadata_borrowed = (g_metadata != NULL);
        }
        if (!g_metadata) {
            g_metadata = db_metadata_load();
            g_metadata_borrowed = false;
        }
    }
    return g_metadata;
}
```
At the search site (main.c ~6365-6370), replace the `if (is_service_backend()) {...} else { search_meta = db_metadata_load(); }` with:
```c
    NcdMetadata *search_meta = (NcdMetadata *)get_state_metadata();   /* service or local backend */
    if (!search_meta) search_meta = get_metadata();
```
and delete the block at ~6515-6518 that frees `search_meta` in standalone mode (`if (search_meta && !is_service_backend()) { db_metadata_free(search_meta); search_meta = NULL; }`), since the backend or `g_metadata` owns it now.
At exit cleanup (~6636-6640), replace with:
```c
    save_metadata_if_dirty();
    if (g_metadata) {
        if (!g_metadata_borrowed) db_metadata_free(g_metadata);
        g_metadata = NULL;
        g_metadata_borrowed = false;
    }
```
The backend frees its own metadata in `close_state_backend`, which runs from `atexit`, i.e. after `main` returns. The borrowed pointer is never used after that.

Near the top of `main()`, right after locals are declared, add:
```c
    if (getenv("NCD_TEST_REPORT_LOADS")) atexit(report_metadata_loads);
```
with this helper above `main()`:
```c
static void report_metadata_loads(void)
{
    fprintf(stderr, "NCD_TEST_METADATA_LOADS=%d\n", db_metadata_load_call_count());
}
```

- [ ] **Step 5: Run the tests**

Run:
```powershell
cmd /c build.bat; cd test; cmd /c build-tests.bat; cd ..
python test\runner.py unit
```
Expected: `test_drive_load` 9/9 and `test_load_integration` 6/6 PASS. If the integration test reports `=2`, find the remaining caller with `grep -n "db_metadata_load()" src/main.c`, then check which one runs on the plain-search path (for example `metadata_load_or_create` or the history code at ~953/1317). Route it through `get_metadata()` as long as it only reads, or only mutates and saves, the metadata, then re-run.

- [ ] **Step 6: Commit**

```bash
git add src/database.c src/database.h src/main.c test/test_drive_load.c test/test_load_integration.c
git commit -m "perf: standalone client reuses backend metadata instead of reloading ncd.metadata

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Full verification, measurement, docs

**Files:**
- Modify: `AGENTS.md` (Known Limitations / exclusion notes), `test.results` (generated)

- [ ] **Step 1: Full clean rebuild and the mandatory full suite**

Run:
```powershell
cmd /c clean.bat
cmd /c build.bat
cd test; cmd /c build-tests.bat; cmd /c build_new_tests.bat; cd ..
python test\generate_report.py
```
Run `generate_report.py` with `run_in_background=true` and timeout 3600. Expected: Windows **and** WSL sections, 0 failed, 0 skipped. Show the runner's summary output to the user unabridged. Investigate and fix any failure, including pre-existing ones (project rule).

- [ ] **Step 2: Re-measure on the real databases**

Recompile the scratchpad benchmark against the new merge (replace the `add_dir` loop with a memcpy of `dirs` and `pool`, mirroring `db_adopt_drive`) and run it against `C:\ProgramData\NCD\ncd_*.database`. Then measure the real service: stop it (`ncd_service stop`), start it with `-log2`, and read the `LOADING`→`READY` timestamps from `C:\ProgramData\NCD\ncd_service.log`. Expected: under 1 s, versus 33 s before. Note: per the AGENTS.md deploy checklist, check the IFEO key before deploying, and get the user's go-ahead before restarting their running service.

- [ ] **Step 3: Update `AGENTS.md`**

In "Known Limitations" / the exclusion description, add:
```markdown
- Exclusion matching has one implementation (`NcdExclusionIndex`, src/database.c) used by the
  scanner, the load-time filter and result filtering. Patterns match the drive-relative path.
  Lists with more than 8 patterns hash literal patterns; wildcard patterns are always matched linearly.
- `db_add_dir()` is append-only. Code inserting into an existing tree must check
  `db_path_index_get()` first; subdirectory rescans replace the subtree.
- Shared-memory snapshots use `platform_crc64` (the single slice-by-8 CRC in ../shared);
  `NCD_SHM_TYPES_VERSION` is 3. Client and service must come from the same build.
```

- [ ] **Step 4: Commit**

```bash
git add AGENTS.md test.results
git commit -m "docs: record unified exclusion index, append-only db_add_dir, SHM v3

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Self-Review (completed)

- **Spec coverage:** O(n²) merge → Task 2. "Only rescans check duplicates / hash on rescan" → Task 3 (append-only + path index in `scan_subdirectory`). Parent-index correctness bug → Task 2 (functional + integration with case-variant siblings). Item 1: slice-by-8 CRC, one function in shared, optimised → Task 1 (Step 6 optimisation pass). Item 2: exclusion hashing above 8, both matchers unified → Task 4 (plus the `db_full_path` cost found while planning). Item 3: metadata double load → Task 5. Functional + integration tests for every task → each task has both, plus the Windows + WSL full gate in Task 6.
- **Placeholder scan:** three steps tell the implementer to confirm an external fact before finalising an assertion: the exclusion CLI flag (Task 4 Step 9), the agent-query JSON shape (Task 3 Step 8, Task 4 Step 9), and the remaining metadata caller (Task 5 Step 5). Each names the exact file or command to check and the decision to make.
- **Type consistency:** `db_adopt_drive`, `db_load_all_drives_into`, `DbLoadProgressFn`, `db_path_index_get`, `DirChildren`, `db_build_children_index`/`db_free_children_index`, `NcdExclusionIndex` + `db_exclusion_index_*`, `db_exclusion_rel_path`, `NCD_EXCLUSION_HASH_THRESHOLD`, `db_metadata_load_call_count`, and the `it_*` helpers are used with the same names and signatures everywhere.
