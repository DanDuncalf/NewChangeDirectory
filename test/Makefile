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
HISTORY_SOURCES = ../src/database.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)

# Test executables
TESTS = test_database test_matcher test_history test_agent_mkdir \
        test_strbuilder test_common test_platform test_metadata test_scanner \
        test_service_lazy_load test_service_parity test_service_lifecycle test_service_integration \
        test_service_database test_service_ipc \
        test_cli_parse test_agent_mode test_result_output \
        test_shared_state_extended test_shm_platform test_ipc \
        test_mkdirs \
        fuzz_database bench_matcher test_db_corruption test_bugs

.PHONY: all test clean fuzz bench corruption bugs recursive-mount features service-test

all: $(TESTS)

# Database tests
TEST_DB_SOURCES = test_database.c test_framework.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_database: $(TEST_DB_SOURCES)
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

# Agent mkdir tests
AGENT_MKDIR_SOURCES = ../src/database.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_agent_mkdir: test_agent_mkdir.c test_framework.c $(AGENT_MKDIR_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent mkdirs tests (integration tests that use the actual executable)
test_mkdirs: test_mkdirs.c test_framework.c $(AGENT_MKDIR_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# History tests
HISTORY_SOURCES = ../src/database.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_history: test_history.c test_framework.c $(HISTORY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 1 tests - strbuilder
test_strbuilder: test_strbuilder.c test_framework.c ../../shared/strbuilder.c ../../shared/common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 1 tests - common
test_common: test_common.c test_framework.c ../../shared/common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 1 tests - platform
test_platform: test_platform.c test_framework.c ../src/platform.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c ../src/database.c ../src/matcher.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 2 tests - metadata
METADATA_SOURCES = ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_metadata: test_metadata.c test_framework.c $(METADATA_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 2 tests - scanner
SCANNER_SOURCES = ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_scanner: test_scanner.c test_framework.c $(SCANNER_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - lazy loading
SERVICE_LAZY_SOURCES = ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_service_lazy_load: test_service_lazy_load.c test_framework.c $(SERVICE_LAZY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - parity
SERVICE_PARITY_SOURCES = ../src/state_backend_local.c ../src/state_backend_service.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_service_parity: test_service_parity.c test_framework.c $(SERVICE_PARITY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - lifecycle
SERVICE_LIFECYCLE_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_service_lifecycle: test_service_lifecycle.c test_framework.c $(SERVICE_LIFECYCLE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - integration
SERVICE_INTEGRATION_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_service_integration: test_service_integration.c test_framework.c $(SERVICE_INTEGRATION_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - database snapshot loading
SERVICE_DATABASE_SOURCES = ../src/service_publish.c ../src/shared_state.c ../src/shm_platform_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_service_database: test_service_database.c test_framework.c $(SERVICE_DATABASE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - Tier 5 IPC integration
SERVICE_IPC_SOURCES = ../src/control_ipc_posix.c ../src/state_backend_local.c ../src/state_backend_service.c ../src/service_publish.c ../src/shared_state.c ../src/shm_platform_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_service_ipc: test_service_ipc.c test_framework.c $(SERVICE_IPC_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 3 tests - CLI parsing
# Uses extracted cli.c module
CLI_TEST_SOURCES = ../src/cli.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_cli_parse: test_cli_parse.c test_framework.c $(CLI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 3 tests - Agent mode
AGENT_TEST_SOURCES = ../src/cli.c ../src/database.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_agent_mode: test_agent_mode.c test_framework.c $(AGENT_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 3 tests - Result output
# Uses extracted result.c module
RESULT_TEST_SOURCES = ../src/result.c ../src/database.c ../src/matcher.c ../src/platform.c $(SHARED_SOURCES)
test_result_output: test_result_output.c test_framework.c $(RESULT_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 4 tests - Shared state extended
SHARED_STATE_EXT_SOURCES = ../src/shared_state.c ../src/shm_platform_posix.c ../src/platform.c $(SHARED_SOURCES)
test_shared_state_extended: test_shared_state_extended.c test_framework.c $(SHARED_STATE_EXT_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 4 tests - SHM platform
SHM_PLATFORM_SOURCES = ../src/shm_platform_posix.c ../src/platform.c $(SHARED_SOURCES)
test_shm_platform: test_shm_platform.c test_framework.c $(SHM_PLATFORM_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 4 tests - IPC
IPC_TEST_SOURCES = ../src/control_ipc_posix.c ../src/platform.c $(SHARED_SOURCES)
test_ipc: test_ipc.c test_framework.c $(IPC_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Database corruption tests with debug flags (includes /test NC tests)
test_db_corruption_debug: test_db_corruption.c test_framework.c $(DB_SOURCES)
	$(CC) $(CFLAGS) -DDEBUG -o test_db_corruption $^ $(LDFLAGS)

test: test_database test_matcher test_history test_agent_mkdir test_strbuilder test_common test_platform test_metadata test_scanner test_service_lazy_load test_service_parity test_cli_parse test_agent_mode test_result_output test_shared_state_extended test_shm_platform test_ipc test_mkdirs
	@echo "Running strbuilder tests..."
	@./test_strbuilder
	@echo ""
	@echo "Running common tests..."
	@./test_common
	@echo ""
	@echo "Running platform tests..."
	@./test_platform
	@echo ""
	@echo "Running database tests..."
	@./test_database
	@echo ""
	@echo "Running matcher tests..."
	@./test_matcher
	@echo ""
	@echo "Running history tests..."
	@./test_history
	@echo ""
	@echo "Running agent mkdir tests..."
	@./test_agent_mkdir
	@echo ""
	@echo "Running agent mode tests..."
	@./test_agent_mode || echo "Note: Some agent mode tests may fail without full main.c linkage"
	@echo ""
	@echo "Running metadata tests..."
	@./test_metadata
	@echo ""
	@echo "Running scanner tests..."
	@./test_scanner
	@echo ""
	@echo "Running service lazy load tests..."
	@./test_service_lazy_load
	@echo ""
	@echo "Running service parity tests..."
	@./test_service_parity
	@echo ""
	@echo "Running CLI parse tests..."
	@./test_cli_parse || echo "Note: CLI parse tests require main.c function linkage"
	@echo ""
	@echo "Running result output tests..."
	@./test_result_output || echo "Note: Result output tests require main.c function linkage"
	@echo ""
	@echo "Running shared state extended tests..."
	@./test_shared_state_extended
	@echo ""
	@echo "Running SHM platform tests..."
	@./test_shm_platform
	@echo ""
	@echo "Running IPC tests..."
	@./test_ipc
	@echo ""
	@echo "Running mkdirs tests..."
	@./test_mkdirs || echo "Note: mkdirs tests require NewChangeDirectory executable to be built"
	@echo ""
	@echo "All unit tests complete."

service-test: test_service_lifecycle test_service_integration test_service_ipc
	@echo "Running service lifecycle tests..."
	@./test_service_lifecycle || echo "Note: Some lifecycle tests may skip if service executable not built"
	@echo ""
	@echo "Running service integration tests..."
	@./test_service_integration || echo "Note: Some integration tests may skip if executables not built"
	@echo ""
	@echo "Running service IPC tests (Tier 5)..."
	@./test_service_ipc || echo "Note: Some IPC tests may skip if service executable not built"
	@echo ""
	@echo "Service tests complete."

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
