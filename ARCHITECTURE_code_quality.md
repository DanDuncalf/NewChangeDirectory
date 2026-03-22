# Code Quality Fixes Plan

All 14 items from the original audit have been addressed.

Completed:
1. Buffer overflow in fuzzy matching variation generation — bounded recursion
2. Wrong offset for skipped_rescan on Linux — offsetof() fix
3. Inconsistent fread/fwrite/fseek return checking — all paths now check immediately
4. File descriptor leak in Linux console cursor query — goto cleanup pattern
5. Scanner global exclusion list thread-safety — passed through context struct
6. StringBuilder JSON escaping O(n) reallocations — two-pass approach
7. Duplicate allocation wrappers — removed, using ncd_malloc/ncd_realloc
8. path_get_drive() lowercase drive letters — isalpha() + toupper()
9. path_leaf() NULL crash — guard added
10. sb_append silent OOM — returns bool
11. Memory leaks on assertion failure in test_bugs.c — free before assert
12. Temp files not cleaned up on assertion failure — cleanup labels + remove at start
13. test_matcher.c missing content assertions — ASSERT_STR_CONTAINS added
14. Random bit-flip tests non-reproducible — seed from env var, printed at start
