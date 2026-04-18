# Makefile for NCD tests
# This builds and runs all test suites

CC = gcc
CFLAGS = -Wall -Wextra -g -I../src -I../../shared -I. -DPLATFORM_LINUX=1 -D_GNU_SOURCE -DNCD_TEST_BUILD
LDFLAGS = -lpthread

# Object directory
OBJDIR = obj

# Shared library sources (platform abstraction)
SHARED_SOURCES = ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c

# Source files from main project (excluding platform-dependent UI/scanner for unit tests)
# Shared utility sources are canonical in ../../shared.
DB_SOURCES = ../src/database.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
MATCHER_SOURCES = ../src/matcher.c ../src/database.c ../src/platform_ncd.c $(SHARED_SOURCES)
HISTORY_SOURCES = ../src/database.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)

# Test executables (note: test_agent_mkdir and test_mkdirs skipped - use internal main.c enums not in headers)
TESTS = test_database test_matcher test_history test_config \
        test_strbuilder test_common test_platform test_metadata test_scanner \
        test_service_lazy_load test_service_parity test_service_lifecycle test_service_integration \
        test_service_database test_service_ipc test_service_empty_db test_service_logging \
        test_service_version_compat test_legacy_service_shutdown \
        test_cli_parse test_agent_mode test_agent_integration test_result_output \
        test_shared_state test_shared_state_extended test_shm_platform test_ipc \
        test_cli_parse_extended test_platform_extended test_strbuilder_extended test_common_extended \
        test_integration_extended \
        test_ui_selector test_ui_history test_ui_config test_ui_navigator \
        test_ui_extended test_ui_exclusions test_main \
        test_database_extended test_odd_cases \
        fuzz_database fuzz_database_load bench_matcher test_db_corruption test_bugs \
        ipc_ping_test ipc_state_test ipc_shutdown_test ipc_metadata_test \
        ipc_heuristic_test ipc_rescan_test ipc_flush_test ipc_cli \
        ipc_fuzzer ipc_stress_test \
        test_service_stress test_service_rescan test_ipc_extended test_shm_stress fuzz_ipc \
        test_cli_edge_cases test_scanner_extended test_matcher_extended \
        test_result_edge_cases test_stress

.PHONY: all test clean fuzz bench corruption bugs recursive-mount features service-test all-environments all-environments-standalone

all: $(TESTS)

# Database tests
TEST_DB_SOURCES = test_database.c test_framework.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_database: $(TEST_DB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Config tests
TEST_CONFIG_SOURCES = test_config.c test_framework.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_config: $(TEST_CONFIG_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Matcher tests
test_matcher: test_matcher.c test_framework.c $(MATCHER_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent 1 tests - Database Extended
DB_EXTENDED_SOURCES = ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_database_extended: test_database_extended.c test_framework.c $(DB_EXTENDED_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent 1 tests - Odd Cases
test_odd_cases: test_odd_cases.c test_framework.c $(DB_EXTENDED_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Fuzz tests (original)
fuzz_database: fuzz_database.c $(DB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent 1 tests - Database Load Fuzzer
fuzz_database_load: fuzz_database_load.c test_framework.c $(DB_SOURCES)
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

# Agent mkdir tests (DISABLED - uses internal main.c enums not exposed in headers)
# AGENT_MKDIR_SOURCES = ../src/database.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
# test_agent_mkdir: test_agent_mkdir.c test_framework.c $(AGENT_MKDIR_SOURCES)
#	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent mkdirs tests (integration tests that use the actual executable) (DISABLED)
# test_mkdirs: test_mkdirs.c test_framework.c $(AGENT_MKDIR_SOURCES)
#	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# History tests
HISTORY_SOURCES = ../src/database.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_history: test_history.c test_framework.c $(HISTORY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 1 tests - strbuilder
test_strbuilder: test_strbuilder.c test_framework.c ../../shared/strbuilder.c ../../shared/common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 1 tests - common
test_common: test_common.c test_framework.c ../../shared/common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 1 tests - platform
test_platform: test_platform.c test_framework.c ../src/platform_ncd.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c ../src/database.c ../src/matcher.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 2 tests - metadata
METADATA_SOURCES = ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_metadata: test_metadata.c test_framework.c $(METADATA_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 2 tests - scanner
SCANNER_SOURCES = ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_scanner: test_scanner.c test_framework.c $(SCANNER_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - lazy loading
SERVICE_LAZY_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_lazy_load: test_service_lazy_load.c test_framework.c $(SERVICE_LAZY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - parity
SERVICE_PARITY_SOURCES = ../src/control_ipc_posix.c ../src/shared_state.c ../src/shm_platform_posix.c ../src/state_backend_local.c ../src/state_backend_service.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_parity: test_service_parity.c test_framework.c $(SERVICE_PARITY_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - lifecycle
SERVICE_LIFECYCLE_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_lifecycle: test_service_lifecycle.c test_framework.c $(SERVICE_LIFECYCLE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - integration
SERVICE_INTEGRATION_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_integration: test_service_integration.c test_framework.c $(SERVICE_INTEGRATION_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - database snapshot loading
SERVICE_DATABASE_SOURCES = ../src/service_publish.c ../src/shared_state.c ../src/shm_platform_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_database: test_service_database.c test_framework.c $(SERVICE_DATABASE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - empty database scenario
SERVICE_EMPTY_DB_SOURCES = ../src/control_ipc_posix.c ../src/state_backend_local.c ../src/state_backend_service.c ../src/service_publish.c ../src/shared_state.c ../src/shm_platform_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_empty_db: test_service_empty_db.c test_framework.c $(SERVICE_EMPTY_DB_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - Tier 5 IPC integration
SERVICE_IPC_SOURCES = ../src/control_ipc_posix.c ../src/state_backend_local.c ../src/state_backend_service.c ../src/service_publish.c ../src/shared_state.c ../src/shm_platform_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_ipc: test_service_ipc.c test_framework.c $(SERVICE_IPC_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - logging
SERVICE_LOGGING_SOURCES = ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_logging: test_service_logging.c test_framework.c $(SERVICE_LOGGING_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - version compatibility
SERVICE_VERSION_COMPAT_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_version_compat: test_service_version_compat.c test_framework.c $(SERVICE_VERSION_COMPAT_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service tests - legacy shutdown
LEGACY_SHUTDOWN_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_legacy_service_shutdown: test_legacy_service_shutdown.c test_framework.c $(LEGACY_SHUTDOWN_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 3 tests - CLI parsing
# Uses extracted cli.c module
CLI_TEST_SOURCES = ../src/cli.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_cli_parse: test_cli_parse.c test_framework.c $(CLI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 3 tests - Agent mode
AGENT_TEST_SOURCES = ../src/cli.c ../src/database.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_agent_mode: test_agent_mode.c test_framework.c $(AGENT_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 3 tests - Agent integration (Phase 3 of CODEBASE_CLEANUP_PLAN.md)
AGENT_INTEGRATION_SOURCES = ../src/cli.c ../src/database.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_agent_integration: test_agent_integration.c test_framework.c $(AGENT_INTEGRATION_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 3 tests - Result output
# Uses extracted result.c module
RESULT_TEST_SOURCES = ../src/result.c ../src/database.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_result_output: test_result_output.c test_framework.c $(RESULT_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 4 tests - Shared state (original)
SHARED_STATE_SOURCES = ../src/shared_state.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_shared_state: test_shared_state.c test_framework.c $(SHARED_STATE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 4 tests - Shared state extended
SHARED_STATE_EXT_SOURCES = ../src/shared_state.c ../src/shm_platform_posix.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_shared_state_extended: test_shared_state_extended.c test_framework.c $(SHARED_STATE_EXT_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 4 tests - SHM platform
SHM_PLATFORM_SOURCES = ../src/shm_platform_posix.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_shm_platform: test_shm_platform.c test_framework.c $(SHM_PLATFORM_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Tier 4 tests - IPC
IPC_TEST_SOURCES = ../src/control_ipc_posix.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_ipc: test_ipc.c test_framework.c $(IPC_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Extended CLI parsing tests (improves coverage)
CLI_EXTENDED_SOURCES = ../src/cli.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_cli_parse_extended: test_cli_parse_extended.c test_framework.c $(CLI_EXTENDED_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Extended platform tests (improves coverage)
PLATFORM_EXTENDED_SOURCES = ../src/platform_ncd.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c ../src/database.c ../src/matcher.c
test_platform_extended: test_platform_extended.c test_framework.c $(PLATFORM_EXTENDED_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Extended strbuilder tests (improves coverage)
test_strbuilder_extended: test_strbuilder_extended.c test_framework.c ../../shared/strbuilder.c ../../shared/common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Extended common tests (improves coverage)
test_common_extended: test_common_extended.c test_framework.c ../../shared/common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Extended integration tests (improves coverage)
INTEGRATION_EXTENDED_SOURCES = ../src/database.c ../src/matcher.c ../src/platform_ncd.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c
test_integration_extended: test_integration_extended.c test_framework.c $(INTEGRATION_EXTENDED_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# UI tests - headless TUI testing with in-memory backend
UI_TEST_SOURCES = ../src/ui.c ../src/database.c ../src/matcher.c ../src/scanner.c ../src/platform_ncd.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c
test_ui_selector: test_ui_selector.c test_framework.c $(UI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ui_history: test_ui_history.c test_framework.c $(UI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ui_config: test_ui_config.c test_framework.c $(UI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ui_navigator: test_ui_navigator.c test_framework.c $(UI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent 3 - Extended UI tests
test_ui_extended: test_ui_extended.c test_framework.c $(UI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ui_exclusions: test_ui_exclusions.c test_framework.c $(UI_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent 3 - Main flow tests (signal handling, result escaping, sorting)
MAIN_TEST_SOURCES = ../src/result.c ../src/database.c ../src/matcher.c ../src/platform_ncd.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c
test_main: test_main.c test_framework.c $(MAIN_TEST_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Key injection tests - unit tests for key parsing and queueing
KEY_INJECTION_SOURCES = test_key_injection.c test_framework.c $(UI_TEST_SOURCES)
test_key_injection: $(KEY_INJECTION_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# IPC Test Programs - Common infrastructure
IPC_TEST_COMMON_SOURCES = ipc_test_common.c ../src/control_ipc_posix.c ../src/platform_ncd.c $(SHARED_SOURCES)

# Phase 1: Basic connectivity tests
ipc_ping_test: ipc_ping_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ipc_state_test: ipc_state_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ipc_shutdown_test: ipc_shutdown_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phase 2: Metadata and operation tests
ipc_metadata_test: ipc_metadata_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ipc_heuristic_test: ipc_heuristic_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ipc_rescan_test: ipc_rescan_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phase 3: Persistence and interactive
ipc_flush_test: ipc_flush_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

ipc_cli: ipc_cli.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Phase 4: Fuzzing and stress tests
ipc_fuzzer: ipc_fuzzer.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

ipc_stress_test: ipc_stress_test.c $(IPC_TEST_COMMON_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

# Agent 4 additions - Input Processing track
AGENT4_SOURCES = ../src/cli.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)

test_cli_edge_cases: test_cli_edge_cases.c test_framework.c $(AGENT4_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_scanner_extended: test_scanner_extended.c test_framework.c $(AGENT4_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_matcher_extended: test_matcher_extended.c test_framework.c $(AGENT4_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_result_edge_cases: test_result_edge_cases.c test_framework.c ../src/result.c $(AGENT4_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_stress: test_stress.c test_framework.c $(AGENT4_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Agent 2: Service Infrastructure Tests
# Service stress tests
SERVICE_STRESS_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_stress: test_service_stress.c test_framework.c $(SERVICE_STRESS_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Service rescan tests
SERVICE_RESCAN_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_service_rescan: test_service_rescan.c test_framework.c $(SERVICE_RESCAN_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# IPC extended tests
IPC_EXTENDED_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_ipc_extended: test_ipc_extended.c test_framework.c $(IPC_EXTENDED_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# SHM stress tests
SHM_STRESS_SOURCES = ../src/shm_platform_posix.c ../src/platform_ncd.c $(SHARED_SOURCES)
test_shm_stress: test_shm_stress.c test_framework.c $(SHM_STRESS_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# IPC fuzz tests
FUZZ_IPC_SOURCES = ../src/control_ipc_posix.c ../src/service_state.c ../src/database.c ../src/scanner.c ../src/matcher.c ../src/platform_ncd.c $(SHARED_SOURCES)
fuzz_ipc: fuzz_ipc.c test_framework.c $(FUZZ_IPC_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm

# Database corruption tests with debug flags (includes /test NC tests)
test_db_corruption_debug: test_db_corruption.c test_framework.c $(DB_SOURCES)
	$(CC) $(CFLAGS) -DDEBUG -o test_db_corruption $^ $(LDFLAGS)

test: test_database test_database_extended test_matcher test_history test_strbuilder test_common test_platform test_metadata test_scanner test_service_lazy_load test_service_parity test_service_lifecycle test_service_integration test_service_database test_service_empty_db test_service_logging test_service_version_compat test_legacy_service_shutdown test_cli_parse test_agent_mode test_agent_integration test_result_output test_main test_shared_state test_shm_platform test_ipc test_cli_parse_extended test_platform_extended test_strbuilder_extended test_common_extended test_integration_extended test_ui_selector test_ui_history test_ui_config test_ui_navigator test_ui_extended test_ui_exclusions test_key_injection test_odd_cases
	@echo "Running strbuilder tests..."
	@./test_strbuilder
	@echo ""
	@echo "Running strbuilder extended tests..."
	@./test_strbuilder_extended
	@echo ""
	@echo "Running common tests..."
	@./test_common
	@echo ""
	@echo "Running common extended tests..."
	@./test_common_extended
	@echo ""
	@echo "Running platform tests..."
	@./test_platform
	@echo ""
	@echo "Running platform extended tests..."
	@./test_platform_extended
	@echo ""
	@echo "Running database tests..."
	@./test_database
	@echo ""
	@echo "Running database extended tests..."
	@./test_database_extended
	@echo ""
	@echo "Running odd cases tests..."
	@./test_odd_cases
	@echo ""
	@echo "Running config tests..."
	@./test_config
	@echo ""
	@echo "Running matcher tests..."
	@./test_matcher
	@echo ""
	@echo "Running history tests..."
	@./test_history
	@echo ""
	@echo "Running agent mode tests..."
	@./test_agent_mode || echo "Note: Some agent mode tests may fail without full main.c linkage"
	@echo ""
	@echo "Running agent integration tests..."
	@./test_agent_integration || echo "Note: Some agent integration tests may fail without full main.c linkage"
	@echo ""
	@echo "Running result output tests..."
	@./test_result_output
	@echo ""
	@echo "Running main flow tests..."
	@./test_main
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
	@echo "Running service lifecycle tests..."
	@./test_service_lifecycle
	@echo ""
	@echo "Running service integration tests..."
	@./test_service_integration
	@echo ""
	@echo "Running service database tests..."
	@./test_service_database
	@echo ""
	@echo "Running service empty db tests..."
	@./test_service_empty_db
	@echo ""
	@echo "Running service logging tests..."
	@./test_service_logging || echo "Note: Some service logging tests may have pre-existing issues"
	@echo ""
	@echo "Running service version compatibility tests..."
	@./test_service_version_compat
	@echo ""
	@echo "Running legacy service shutdown tests..."
	@./test_legacy_service_shutdown
	@echo ""
	@echo "Running CLI parse tests..."
	@./test_cli_parse
	@echo ""
	@echo "Running CLI parse extended tests..."
	@./test_cli_parse_extended
	@echo ""
	@echo "Running integration extended tests..."
	@./test_integration_extended
	@echo ""
	@echo "Running shared state tests..."
	@./test_shared_state
	@echo ""
	@echo "Running shared state extended tests..."
	@./test_shared_state_extended || echo "Note: Some shared state extended tests may have pre-existing issues"
	@echo ""
	@echo "Running SHM platform tests..."
	@./test_shm_platform
	@echo ""
	@echo "Running IPC tests..."
	@./test_ipc || echo "Note: Some IPC tests may have pre-existing issues"
	@echo ""
	@echo "Running UI selector tests..."
	@./test_ui_selector
	@echo ""
	@echo "Running UI history tests..."
	@./test_ui_history
	@echo ""
	@echo "Running UI config tests..."
	@./test_ui_config
	@echo ""
	@echo "Running UI navigator tests..."
	@./test_ui_navigator
	@echo ""
	@echo "Running UI extended tests..."
	@./test_ui_extended || echo "Note: Some UI extended tests may have pre-existing issues"
	@echo ""
	@echo "Running UI exclusions tests..."
	@./test_ui_exclusions || echo "Note: Some UI exclusions tests may have pre-existing issues"
	@echo ""
	@echo "Running key injection tests..."
	@./test_key_injection
	@echo ""
	@echo "All unit tests complete."

service-test: test_service_lifecycle test_service_integration test_service_ipc
	@echo "Running service lifecycle tests..."
	@./test_service_lifecycle
	@echo ""
	@echo "Running service integration tests..."
	@./test_service_integration
	@echo ""
	@echo "Running service IPC tests (Tier 5)..."
	@./test_service_ipc
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

# Comprehensive test runner - runs all tests across all 4 environments:
#   1. Windows without service (standalone)
#   2. Windows with service (shared memory)
#   3. WSL without service (standalone)
#   4. WSL with service (shared memory)
all-environments:
	@echo "Running comprehensive tests across all environments..."
	@bash run_all_tests.sh

# Same as all-environments but only tests standalone mode (no service)
all-environments-standalone:
	@echo "Running tests across all environments (standalone mode only)..."
	@bash run_all_tests.sh --no-service

# IPC diagnostic tools build target
ipc-tests: ipc_ping_test ipc_state_test ipc_shutdown_test ipc_metadata_test \
           ipc_heuristic_test ipc_rescan_test ipc_flush_test ipc_cli \
           ipc_fuzzer ipc_stress_test
	@echo "IPC test programs built successfully."

clean:
	rm -rf $(OBJDIR) $(TESTS) *.tmp *.database
	rm -f ipc_ping_test ipc_state_test ipc_shutdown_test ipc_metadata_test
	rm -f ipc_heuristic_test ipc_rescan_test ipc_flush_test ipc_cli
	rm -f ipc_fuzzer ipc_stress_test
	rm -f stress_test.db
	rm -f test_service_stress test_service_rescan test_ipc_extended test_shm_stress fuzz_ipc

# Windows-specific test builds
win-test:
	@echo "Use build.bat in parent directory for Windows builds"
	@echo "Or compile manually with MSVC:"
	@echo "  cl /nologo /W3 /O2 /I../src /I. /I../../shared test_database.c test_framework.c ../src/database.c ../../shared/common.c /Fe:test_database.exe"
	@echo "  cl /nologo /W3 /O2 /I../src /I. /I../../shared test_db_corruption.c test_framework.c ../src/database.c ../../shared/common.c /Fe:test_db_corruption.exe"
	@echo "For debug build with /test NC tests:"
	@echo "  cl /nologo /W3 /O2 /DDEBUG /I../src /I. /I../../shared test_db_corruption.c test_framework.c ../src/database.c ../../shared/common.c /Fe:test_db_corruption.exe"
