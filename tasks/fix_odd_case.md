# Root Cause Analysis: `odd_case_sensitivity_on_case_insensitive_fs` Test Failure

## Failure

```
FAIL: test_odd_cases.c:162: Expected 3, got 1
```

## Root Cause

The test `odd_case_sensitivity_on_case_insensitive_fs` was written **before** the dedup feature was added to `db_add_dir()`. Commit `0125ee6` ("Fix duplicate search results...") introduced dedup in **two** places:

1. **`db_add_dir()` (database.c, lines 975-986)**: Prevents duplicate entries at insertion time. On Windows, `_stricmp` matches case-insensitively, so `"Documents"`, `"DOCUMENTS"`, and `"documents"` are all treated as the same entry. Only the **first** one is stored; subsequent calls return the existing index without incrementing `dir_count`.

2. **`matcher_find()` (matcher.c, lines 463-488)**: Safety-net dedup at query time, for any pre-existing duplicates that may have been stored in old databases.

**The problem**: The test calls `db_add_dir` three times with different case variants and asserts `dir_count == 3`. But on Windows, `db_add_dir` now deduplicates and only stores 1 entry. The test was never updated when the dedup was added.

## Current Test Code (test_odd_cases.c:150-166)

```c
TEST(odd_case_sensitivity_on_case_insensitive_fs) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);

    DriveData *drv = db_add_drive(db, 'C');

    /* Add directories with different cases */
    db_add_dir(drv, "Documents", -1, false, false);
    db_add_dir(drv, "DOCUMENTS", -1, false, false);
    db_add_dir(drv, "documents", -1, false, false);

    /* All three should be stored (database is case-preserving) */
    ASSERT_EQ_INT(3, drv->dir_count);   // <-- FAILS: dir_count is 1 on Windows

    db_free(db);
    return 0;
}
```

The comment "All three should be stored (database is case-preserving)" is now **incorrect** on Windows. The dedup feature intentionally prevents duplicate entries because a case-insensitive filesystem (NTFS) cannot have separate directories named "Documents", "DOCUMENTS", and "documents" at the same parent — they resolve to the same inode.

## What Needs to Change

The test must be updated to be **platform-aware**, matching the pattern already established in the sibling regression test `match_db_add_dir_rejects_duplicate` (test_matcher_extended.c:646-674).

### The Fix (Replace test_odd_cases.c:150-166)

Replace the test function with:

```c
TEST(odd_case_sensitivity_on_case_insensitive_fs) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);

    DriveData *drv = db_add_drive(db, 'C');

    /* Add directories with different cases.
     * On case-insensitive filesystems (Windows), db_add_dir
     * deduplicates by (parent, name) using case-insensitive comparison,
     * so only the first entry is stored and subsequent calls return
     * the existing index.
     * On case-sensitive filesystems (Linux), all three are distinct. */
    int id1 = db_add_dir(drv, "Documents", -1, false, false);
    int id2 = db_add_dir(drv, "DOCUMENTS", -1, false, false);
    int id3 = db_add_dir(drv, "documents", -1, false, false);

#if NCD_PLATFORM_WINDOWS
    /* On Windows: all three case variants resolve to the same entry */
    ASSERT_EQ_INT(1, drv->dir_count);
    ASSERT_EQ_INT(id1, id2);
    ASSERT_EQ_INT(id1, id3);
#else
    /* On Linux: each case variant is a distinct entry */
    ASSERT_EQ_INT(3, drv->dir_count);
    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(id1 != id3);
    ASSERT_TRUE(id2 != id3);
#endif

    db_free(db);
    return 0;
}
```

## Additional Context

The regression test `match_db_add_dir_rejects_duplicate` in `test/test_matcher_extended.c` (added in the same commit `0125ee6`) already validates this dedup behavior with the same platform-aware pattern. The `odd_case_sensitivity_on_case_insensitive_fs` test simply wasn't updated at the time.

## Summary

| Item | Detail |
|------|--------|
| **What failed** | `ASSERT_EQ_INT(3, drv->dir_count)` — expected 3, got 1 |
| **Root cause** | `db_add_dir()` now deduplicates by (parent, name) with case-insensitive comparison on Windows (commit `0125ee6`). Three case-variant names resolve to 1 entry. |
| **Is the test logic correct?** | No. The test expected pre-dedup behavior. |
| **Is the code correct?** | Yes. The dedup in `db_add_dir` is intentional and correct — a case-insensitive FS cannot host three directories differing only in case at the same parent. |
| **Fix type** | Test update only — no production code change needed. |
| **Risk** | Minimal. The test becomes platform-aware and matches existing patterns in the same commit's regression tests. |
