# Testing NCD

This directory contains the test suite for NewChangeDirectory (NCD).

## Test Structure

```
test/
├── test_framework.h      # Minimal unit testing framework
├── test_framework.c      # Test framework implementation
├── test_database.c       # Database module tests
├── test_matcher.c        # Matcher module tests
├── fuzz_database.c       # Fuzz testing for database loading
├── bench_matcher.c       # Performance benchmarks
├── Makefile              # Test build system
├── TESTING.md            # This file
├── Win/                  # Windows-specific tests
├── Wsl/                  # WSL-specific tests
└── PowerShell/           # PowerShell-specific tests
```

## Running Unit Tests

### Linux/WSL

```bash
cd test
make test
```

### Windows (MSVC)

```batch
cd test
cl /nologo /W3 /O2 /Isrc /I. test_database.c test_framework.c src\database.c /Fe:test_database.exe
cl /nologo /W3 /O2 /Isrc /I. test_matcher.c test_framework.c src\matcher.c src\database.c /Fe:test_matcher.exe
test_database.exe
test_matcher.exe
```

### Windows (MinGW)

```batch
cd test
gcc -Wall -Wextra -I../src -I. -o test_database.exe test_database.c test_framework.c ../src/database.c -lpthread
gcc -Wall -Wextra -I../src -I. -o test_matcher.exe test_matcher.c test_framework.c ../src/matcher.c ../src/database.c -lpthread
test_database.exe
test_matcher.exe
```

## Running Fuzz Tests

```bash
cd test
make fuzz
```

The fuzz test runs 10,000 iterations of random binary file loading and 5,000 iterations of random JSON parsing to verify the database loader handles malformed input gracefully.

## Running Benchmarks

```bash
cd test
make bench
```

This runs performance benchmarks on databases of varying sizes:
- Small: 1 drive, 1,000 directories
- Medium: 4 drives, 10,000 directories each
- Large: 8 drives, 50,000 directories each

## Test Coverage

### Database Tests (test_database.c)

- `create_and_free` - Database creation and cleanup
- `add_drive` - Adding drives to database
- `add_directory` - Adding directories with parent relationships
- `full_path_reconstruction` - Reconstructing full paths from parent chain
- `binary_save_load_roundtrip` - Binary format serialization/deserialization
- `binary_load_corrupted_rejected` - Rejection of corrupted binary files
- `binary_load_truncated_rejected` - Rejection of truncated binary files

### Matcher Tests (test_matcher.c)

- `match_single_component` - Single-component search matching
- `match_two_components` - Multi-component path matching
- `match_case_insensitive` - Case-insensitive matching
- `match_with_hidden_filter` - Hidden directory filtering
- `match_no_results` - Empty result handling
- `match_empty_search` - Empty search handling

## Adding New Tests

1. Create a new test function using the `TEST(name)` macro
2. Use assertion macros (`ASSERT_TRUE`, `ASSERT_EQ_INT`, etc.)
3. Return 0 for success, non-zero for failure
4. Register the test in the appropriate suite with `RUN_TEST(name)`

Example:

```c
TEST(my_new_test) {
    NcdDatabase *db = db_create();
    ASSERT_NOT_NULL(db);
    
    /* Your test logic here */
    ASSERT_EQ_INT(1, db->version);
    
    db_free(db);
    return 0;
}

void suite_database(void) {
    RUN_TEST(create_and_free);
    /* ... other tests ... */
    RUN_TEST(my_new_test);  /* Add here */
}
```

## Continuous Integration

These tests are designed to run in CI environments:
- Return code 0 indicates all tests passed
- Return code non-zero indicates failures
- No interactive input required
- No system modifications (uses temp files only)
