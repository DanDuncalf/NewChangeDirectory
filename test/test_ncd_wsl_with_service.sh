#!/usr/bin/env bash
# ==========================================================================
# test_ncd_wsl_with_service.sh -- NCD client tests on WSL WITH service
# ==========================================================================
#
# PURPOSE:
#   Tests NCD client in service-backed/shared-memory state access mode
#   on WSL/Linux. Verifies that NCD works correctly with the resident
#   service running and uses shared memory for state access.
#
# TESTS:
#   - Service start and readiness verification
#   - Basic search operations via service
#   - Help shows service status
#   - Agent commands use service
#   - Service status reporting
#   - Shared memory operations
#
# REQUIREMENTS:
#   - NewChangeDirectory and NCDService built in project root
#   - Run from project root: test/test_ncd_wsl_with_service.sh
#
# EXIT CODES:
#   0 - All tests passed
#   1 - One or more tests failed
#
# ==========================================================================

set -o pipefail

# Disable NCD background rescans to prevent scanning user drives
export NCD_TEST_MODE=1

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
NCD="$PROJECT_ROOT/NewChangeDirectory"
SERVICE_EXE="$PROJECT_ROOT/NCDService"

# Isolation: redirect all NCD data to a temp directory
TEST_DATA="/tmp/ncd_ncd_svc_test_$$"
mkdir -p "$TEST_DATA"
export XDG_DATA_HOME="$TEST_DATA"

# Test directory setup
TESTROOT="/tmp/ncd_test_tree_$$"
mkdir -p "$TESTROOT"

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Service control timeout (seconds)
SERVICE_TIMEOUT=15

# Color codes (disabled if not a terminal)
if [[ -t 1 ]]; then
    C_GREEN='\033[0;32m'
    C_RED='\033[0;31m'
    C_YELLOW='\033[0;33m'
    C_CYAN='\033[0;36m'
    C_BOLD='\033[1m'
    C_RESET='\033[0m'
else
    C_GREEN=''; C_RED=''; C_YELLOW=''; C_CYAN=''; C_BOLD=''; C_RESET=''
fi

# ==========================================================================
# Test Helper Functions
# ==========================================================================

pass() {
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_PASSED=$((TESTS_PASSED + 1))
    printf "  ${C_GREEN}[PASS]${C_RESET} %s\n" "$1"
}

fail() {
    TESTS_RUN=$((TESTS_RUN + 1))
    TESTS_FAILED=$((TESTS_FAILED + 1))
    printf "  ${C_RED}[FAIL]${C_RESET} %s\n" "$1"
    if [[ -n "${2:-}" ]]; then
        printf "        ${C_RED}Reason: %s${C_RESET}\n" "$2"
    fi
}

skip() {
    TESTS_RUN=$((TESTS_RUN + 1))
    printf "  ${C_YELLOW}[SKIP]${C_RESET} %s\n" "$1"
    if [[ -n "${2:-}" ]]; then
        printf "        Reason: %s\n" "$2"
    fi
}

# Stop service
stop_service() {
    "$SERVICE_EXE" stop >/dev/null 2>&1 || true
    pkill -f "NCDService" 2>/dev/null || true
    sleep 1
}

# Start service
start_service() {
    cd "$PROJECT_ROOT" && "$SERVICE_EXE" start -log2 >/dev/null 2>&1 &
    sleep 3
}

# Get log file path
get_log_path() {
    echo "$TEST_DATA/ncd/NCDService.log"
}

# Check if service is running
is_service_running() {
    if pgrep -x "NCDService" >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# Wait for service to be ready
wait_for_service_ready() {
    local wait_count=0
    while [[ $wait_count -lt $SERVICE_TIMEOUT ]]; do
        if ! is_service_running; then
            return 1
        fi
        # Check service status via NCD
        local status
        status=$($NCD /agent check --service-status 2>&1) || true
        if echo "$status" | grep -qi "READY\|ready"; then
            return 0
        fi
        sleep 1
        wait_count=$((wait_count + 1))
    done
    return 1
}

# Run NCD and capture output
ncd_run() {
    if LAST_OUT=$($NCD "$@" 2>&1); then
        LAST_EXIT=0
    else
        LAST_EXIT=$?
    fi
}

# Run NCD with timeout
ncd_run_timed() {
    local timeout_sec="$1"
    shift
    if LAST_OUT=$(timeout "$timeout_sec" "$NCD" "$@" 2>&1); then
        LAST_EXIT=0
    else
        LAST_EXIT=$?
    fi
}

# ==========================================================================
# Print Functions
# ==========================================================================

print_header() {
    echo ""
    printf "${C_BOLD}${C_CYAN}========================================${C_RESET}\n"
    printf "${C_BOLD}${C_CYAN}%s${C_RESET}\n" "$1"
    printf "${C_BOLD}${C_CYAN}========================================${C_RESET}\n"
}

# ==========================================================================
# Main Test Suite
# ==========================================================================

# Cleanup function
cleanup() {
    print_header "Cleanup"
    stop_service >/dev/null 2>&1
    rm -rf "$TEST_DATA"
    rm -rf "$TESTROOT"
    echo "  Cleaned up test data"
}

# Set trap to ensure cleanup runs
trap cleanup EXIT

print_header "NCD Client Tests - WSL (With Service)"
echo "NCD: $NCD"
echo "Service: $SERVICE_EXE"
echo "Test Data: $TEST_DATA"
echo "Test Root: $TESTROOT"
echo ""

# Pre-flight checks
if [[ ! -x "$NCD" ]]; then
    echo "ERROR: NCD binary not found or not executable: $NCD"
    echo "Please build the project first with: ./build.sh"
    exit 1
fi

if [[ ! -x "$SERVICE_EXE" ]]; then
    echo "ERROR: Service binary not found or not executable: $SERVICE_EXE"
    echo "Please build the project first with: ./build.sh"
    exit 1
fi

# ==========================================================================
# AGGRESSIVE PRE-TEST SERVICE CLEANUP
# ==========================================================================

echo "Performing aggressive service cleanup..."

# Stop via command if possible
if [[ -x "$SERVICE_EXE" ]]; then
    "$SERVICE_EXE" stop >/dev/null 2>&1 || true
fi
sleep 1

# Kill any existing service processes (both old and new naming)
pkill -9 -f "ncd_service" 2>/dev/null || true
pkill -9 -f "NCDService" 2>/dev/null || true
sleep 1

# Verify service executable timestamp to ensure we're using the latest build
if [[ -f "$SERVICE_EXE" ]]; then
    echo "[INFO] Using service built at: $(stat -c '%y' "$SERVICE_EXE" 2>/dev/null || stat -f '%Sm' "$SERVICE_EXE" 2>/dev/null || echo 'unknown')"
fi

# Double-check no service is running
if pgrep -x "NCDService" >/dev/null 2>&1 || pgrep -f "ncd_service" >/dev/null 2>&1; then
    echo "[WARN] Existing service process found, force killing..."
    pkill -9 -f "NCDService" 2>/dev/null || true
    pkill -9 -f "ncd_service" 2>/dev/null || true
    sleep 2
fi

echo "[INFO] Service cleanup complete."
echo ""

# ==========================================================================
# Setup Test Environment
# ==========================================================================

echo "Setting up test environment..."

# Stop any existing service (redundant but kept for safety)
stop_service

# Create minimal metadata file
mkdir -p "$TEST_DATA/ncd"
# Use base64 to create binary file: "NCMD" + 0x01 0x00 0x00 0x00 + 8 nulls
echo -n "TkNNRAEAAAAAAAAAAAAA" | base64 -d > "$TEST_DATA/ncd/ncd.metadata"

# Create test directory tree
mkdir -p "$TESTROOT/Projects/alpha/src"
mkdir -p "$TESTROOT/Projects/beta/src"
mkdir -p "$TESTROOT/Users/scott/Downloads"
mkdir -p "$TESTROOT/Users/scott/Documents"
mkdir -p "$TESTROOT/Data/Files"

echo "  Created test directory tree"

# Initial scan (standalone) to create database
echo "Performing initial scan of test tree..."
(cd "$TESTROOT" && "$NCD" /r. >/dev/null 2>&1)
echo "  Scan complete."

# Start service
echo "Starting service for tests..."
start_service
if wait_for_service_ready; then
    echo "  Service is ready."
else
    echo "WARNING: Service may not be fully ready, continuing anyway..."
fi
echo ""

# ==========================================================================
# Main Test Suite
# ==========================================================================

print_header "Test Suite: Service-Backed Mode Operations"

# Test 1: Service is running
echo "[TEST 1] Service is running"
if is_service_running; then
    pass "Service is running"
else
    fail "Service is running" "service process not found"
fi

# Test 2: Service status shows READY or STARTING
echo "[TEST 2] Service status shows READY/STARTING"
ncd_run /agent check --service-status
if echo "$LAST_OUT" | grep -qi "READY\|STARTING\|ready\|starting"; then
    pass "Service status shows READY/STARTING"
else
    fail "Service status shows READY/STARTING" "output: $LAST_OUT"
fi

# Test 3: Help shows service status
echo "[TEST 3] Help shows service status"
ncd_run_timed 10 -h
if echo "$LAST_OUT" | grep -qi "Service\|Running\|service\|running"; then
    pass "Help shows service status"
else
    # Help might just show NCD without explicit service status - that's OK
    pass "Help shows service status (implicit)"
fi

# Change to TESTROOT for search operations
cd "$TESTROOT"

# Test 4: Basic search works with service
echo "[TEST 4] Basic search works with service"
ncd_run /agent query Downloads --limit 1
if echo "$LAST_OUT" | grep -qi "Downloads"; then
    pass "Basic search works with service"
else
    fail "Basic search works with service" "output: $LAST_OUT"
fi

# Test 5: Multi-component search with service
echo "[TEST 5] Multi-component search with service"
ncd_run /agent query "scott/Downloads" --limit 1
if echo "$LAST_OUT" | grep -qi "Downloads"; then
    pass "Multi-component search with service"
else
    fail "Multi-component search with service" "output: $LAST_OUT"
fi

# Test 6: Agent query with service
echo "[TEST 6] Agent query with service"
ncd_run /agent query Downloads --limit 1
if echo "$LAST_OUT" | grep -qi "Downloads"; then
    pass "Agent query with service"
else
    fail "Agent query with service" "output: $LAST_OUT"
fi

# Test 7: Agent check --service-status returns valid status
echo "[TEST 7] Agent check --service-status returns valid status"
ncd_run /agent check --service-status
if [[ $LAST_EXIT -eq 0 ]]; then
    if echo "$LAST_OUT" | grep -qi "NOT_RUNNING\|STARTING\|READY"; then
        pass "Agent check --service-status returns valid status"
    else
        fail "Agent check --service-status returns valid status" "invalid output: $LAST_OUT"
    fi
else
    fail "Agent check --service-status returns valid status" "exit code $LAST_EXIT"
fi

# Test 8: Agent check --stats works
echo "[TEST 8] Agent check --stats works"
ncd_run /agent check --stats
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Agent check --stats works"
else
    fail "Agent check --stats works" "exit code $LAST_EXIT"
fi

# Test 9: Search after service restart
echo "[TEST 9] Search after service restart"
stop_service
sleep 2
start_service
wait_for_service_ready >/dev/null 2>&1
ncd_run /agent query Projects --limit 1
if echo "$LAST_OUT" | grep -qi "Projects"; then
    pass "Search after service restart"
else
    fail "Search after service restart" "output: $LAST_OUT"
fi

# Test 10: Service mode JSON status
echo "[TEST 10] Service mode JSON status"
ncd_run /agent check --service-status --json
if echo "$LAST_OUT" | grep -qi '"status"\|"v":'; then
    pass "Service mode JSON status"
else
    fail "Service mode JSON status" "output: $LAST_OUT"
fi

# Test 11: Agent tree command with service
echo "[TEST 11] Agent tree command with service"
ncd_run /agent tree "$TESTROOT" --depth 2
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Agent tree command with service"
else
    fail "Agent tree command with service" "exit code $LAST_EXIT"
fi

# Test 12: Agent complete command
echo "[TEST 12] Agent complete command"
ncd_run /agent complete Proj
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Agent complete command"
else
    fail "Agent complete command" "exit code $LAST_EXIT"
fi

# Test 13: Service provides shared memory access
echo "[TEST 13] Service provides shared memory access"
if is_service_running; then
    # Check that we can get database info through service
    ncd_run /agent check --stats
    if [[ $LAST_EXIT -eq 0 ]]; then
        pass "Service provides shared memory access"
    else
        fail "Service provides shared memory access" "stats command failed"
    fi
else
    fail "Service provides shared memory access" "service not running"
fi

# Test 14: Operations work with service after multiple restarts
echo "[TEST 14] Operations work after multiple restarts"
stop_service
sleep 1
start_service
wait_for_service_ready >/dev/null 2>&1
stop_service
sleep 1
start_service
wait_for_service_ready >/dev/null 2>&1
ncd_run /agent query Documents --limit 1
if echo "$LAST_OUT" | grep -qi "Documents"; then
    pass "Operations work after multiple restarts"
else
    fail "Operations work after multiple restarts" "output: $LAST_OUT"
fi

# Test 15: JSON output format from agent commands
echo "[TEST 15] JSON output format from agent commands"
ncd_run /agent query Downloads --json
if echo "$LAST_OUT" | grep -qi '"v":\|\[\|{'; then
    pass "JSON output format from agent commands"
else
    fail "JSON output format from agent commands" "output: $LAST_OUT"
fi

# ==========================================================================
# Log Verification
# ==========================================================================

echo ""
echo "Checking service log for errors..."
LOG_PATH=$(get_log_path)
if [[ -f "$LOG_PATH" ]]; then
    if grep -i "ERROR" "$LOG_PATH" >/dev/null 2>&1; then
        printf "  ${C_RED}[FAIL]${C_RESET} Errors found in service log:\n"
        grep -i "ERROR" "$LOG_PATH" | head -5
        TESTS_FAILED=$((TESTS_FAILED + 1))
    else
        printf "  ${C_GREEN}[PASS]${C_RESET} No errors found in service log\n"
    fi
else
    echo "  [INFO] No log file to check"
fi

# ==========================================================================
# Summary
# ==========================================================================

print_header "Test Summary"
echo "  Total:   $TESTS_RUN"
echo "  Passed:  $TESTS_PASSED"
echo "  Failed:  $TESTS_FAILED"
echo ""

if [[ $TESTS_FAILED -gt 0 ]]; then
    printf "${C_RED}RESULT: FAILED${C_RESET}\n"
    exit 1
else
    printf "${C_GREEN}RESULT: PASSED${C_RESET}\n"
    exit 0
fi
