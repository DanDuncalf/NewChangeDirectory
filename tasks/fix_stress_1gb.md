# Fix: `stress_database_name_pool_1gb` Test Failure

## Failure

```
FAIL: test_stress.c:124: Assertion failed: drv->dir_count == count
```

The test adds `count` (default 1000) directories via `db_add_dir` but `drv->dir_count` never reaches 1000.

## Root Cause

**The test generates only 26 unique names, and `db_add_dir` deduplicates them.**

### The test code (lines 100-129 of `test/test_stress.c`):

The name is filled with 255 `'a'` chars, and only `long_name[0]` is varied using `i % 26`. This means the first character cycles through `a` through `z` -- only **26 unique names** exist across all 1000 iterations.

### The deduplication in `db_add_dir` (lines 975-986 of `src/database.c`):

```c
for (int i = 0; i < drv->dir_count; i++) {
    if (drv->dirs[i].parent != parent) continue;
    const char *existing = drv->name_pool + drv->dirs[i].name_off;
#if NCD_PLATFORM_WINDOWS
    if (_stricmp(existing, name) == 0) return i;
#else
    if (strcmp(existing, name) == 0) return i;
#endif
}
```

On iterations 26+ (the second occurrence of each unique name), `db_add_dir` finds the existing entry and returns its index **without incrementing `dir_count`**. `dir_count` stops at 26, but the test asserts it should equal 1000.

**This is NOT a bug in the database code.** The deduplication is intentional and correct. The test is the problem.

## Fix

The test needs to generate **unique** names. The simplest fix: encode the iteration index into the name.

**Current code (lines 115-118):**
```c
    for (int i = 0; i < count; i++) {
        /* Vary the name slightly */
        long_name[0] = 'a' + (i % 26);
        db_add_dir(drv, long_name, -1, false, false);
    }
```

**Replace with:**
```c
    for (int i = 0; i < count; i++) {
        /* Embed index for uniqueness; names are long to stress the pool */
        snprintf(long_name, sizeof(long_name),
                 "very_long_directory_name_%06d_"
                 "padding_padding_padding_padding_padding_padding_padding_"
                 "padding_padding_padding_padding_padding_padding_padding",
                 i);
        db_add_dir(drv, long_name, -1, false, false);
    }
```

This produces ~190-char unique names per iteration, still stressing the name pool with large strings but avoiding deduplication.

## Impact

- **No production code changes needed.** `db_add_dir` deduplication is correct.
- **Test-only fix.** 1 block change in `test/test_stress.c`.
- After fix: `dir_count == count` (1000 by default), assertion passes.

## Verification

Run after fix:
```
cd E:\llama\NewChangeDirectory
set NCD_TEST_MODE=1
test\test_stress.exe
```

Expected: `stress_database_name_pool_1gb` PASSED, 25/25 tests pass.
