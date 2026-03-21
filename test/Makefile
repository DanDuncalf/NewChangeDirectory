# Makefile for NCD tests
# This builds and runs all test suites

CC = gcc
CFLAGS = -Wall -Wextra -g -I../src -I../../shared -I. -DPLATFORM_LINUX=1 -D_GNU_SOURCE
LDFLAGS = -lpthread

# Object directory
OBJDIR = obj

# Shared library sources (platform abstraction)
SHARED_SOURCES = ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c

# Source files from main project (excluding platform-dependent UI/scanner for unit tests)
# Note: Using shared/common.c instead of ../src/common.c to avoid duplicate definitions
DB_SOURCES = ../src/database.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
MATCHER_SOURCES = ../src/matcher.c ../src/database.c ../src/platform.c $(SHARED_SOURCES)

# Test executables
TESTS = test_database test_matcher fuzz_database bench_matcher test_db_corruption test_bugs

.PHONY: all test clean fuzz bench corruption bugs recursive-mount features

all: $(TESTS)

# Database tests
test_database: test_database.c test_framework.c $(DB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Matcher tests
test_matcher: test_matcher.c test_framework.c $(MATCHER_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Fuzz tests
fuzz_database: fuzz_database.c $(DB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Benchmarks
bench_matcher: bench_matcher.c $(MATCHER_SOURCES)
	$(CC) $(CFLAGS) -O2 -o $@ $^ $(LDFLAGS)

# Database corruption tests
test_db_corruption: test_db_corruption.c test_framework.c $(DB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Bug detection tests (exposes known bugs without fixing them)
test_bugs: test_bugs.c test_framework.c $(MATCHER_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Database corruption tests with debug flags (includes /test NC tests)
test_db_corruption_debug: test_db_corruption.c test_framework.c $(DB_SOURCES)
	$(CC) $(CFLAGS) -DDEBUG -o test_db_corruption $^ $(LDFLAGS)

test: test_database test_matcher
	@echo "Running database tests..."
	@./test_database
	@echo ""
	@echo "Running matcher tests..."
	@./test_matcher
	@echo ""
	@echo "All unit tests complete."

corruption: test_db_corruption
	@echo "Running database corruption tests..."
	@./test_db_corruption
	@echo ""
	@echo "Corruption tests complete."

bugs: test_bugs
	@echo "Running bug detection tests..."
	@./test_bugs
	@echo ""
	@echo "Bug detection tests complete."

fuzz: fuzz_database
	@echo "Running fuzz tests (quick run)..."
	@timeout 60 ./fuzz_database || true

bench: bench_matcher
	@echo "Running benchmarks..."
	@./bench_matcher

features:
	@echo "Running comprehensive feature tests (requires root for ramdisk)..."
	@sudo bash Wsl/test_features.sh

features-noroot:
	@echo "Running comprehensive feature tests (no ramdisk, using /tmp)..."
	@bash Wsl/test_features.sh

recursive-mount:
	@echo "Running recursive mount tests (requires root)..."
	@sudo Wsl/test_recursive_mount.sh

clean:
	rm -rf $(OBJDIR) $(TESTS) *.tmp *.database

# Windows-specific test builds
win-test:
	@echo "Use build.bat in parent directory for Windows builds"
	@echo "Or compile manually with MSVC:"
	@echo "  cl /nologo /W3 /O2 /Isrc /I. test_database.c test_framework.c src\database.c src\common.c /Fe:test_database.exe"
	@echo "  cl /nologo /W3 /O2 /Isrc /I. test_db_corruption.c test_framework.c src\database.c src\common.c /Fe:test_db_corruption.exe"
	@echo "For debug build with /test NC tests:"
	@echo "  cl /nologo /W3 /O2 /DDEBUG /Isrc /I. test_db_corruption.c test_framework.c src\database.c src\common.c /Fe:test_db_corruption.exe"
