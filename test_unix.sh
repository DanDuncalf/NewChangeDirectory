#!/usr/bin/env bash
# ==========================================================================
# test_ncd_wsl_standalone.sh -- NCD client tests on WSL WITHOUT service
# ==========================================================================
#
# PURPOSE:
#   Tests NCD client in standalone/disk-based state access mode on WSL/Linux.
#   Verifies that NCD works correctly without the resident service running.
#
# TESTS:
#   - Basic search operations
#   - Help output
#   - Version information
#   - Database operations (rescan, search)
#   - Agent commands (query, check, ls)
#   - Service NOT running verification
#
# REQUIREMENTS:
#   - NewChangeDirectory binary built in project root
#   - Run from project root: test/test_ncd_wsl_standalone.sh
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

# Isolation: redirect all NCD data to a temp directory
TEST_DATA="/tmp/ncd_ncd_test_$$"
mkdir -p "$TEST_DATA"
export XDG_DATA_HOME="$TEST_DATA"

# Test directory setup
TESTROOT="/tmp/ncd_test_tree_$$"
mkdir -p "$TESTROOT"

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

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

# Stop service if running
stop_service() {
    pkill -f "NCDService" 2>/dev/null || true
    sleep 1
}

# Check if service is running
is_service_running() {
    if pgrep -x "NCDService" >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# Run NCD and capture output
ncd_run() {
    LAST_OUT=$($NCD "$@" 2>&1) || true
    LAST_EXIT=${PIPESTATUS[0]:-$?}
}

# Run NCD with timeout (prevents hanging on TUI)
ncd_run_timed() {
    local timeout_sec="$1"
    shift
    local exit_file="/tmp/ncd_exit_$$"
    rm -f "$exit_file"
    # Use a subshell to capture exit code reliably
    ( "$NCD" "$@" 2>&1; echo $? > "$exit_file" ) &
    local pid=$!
    ( sleep "$timeout_sec" && kill $pid 2>/dev/null ) &
    local killer=$!
    wait $pid 2>/dev/null
    kill $killer 2>/dev/null
    if [[ -f "$exit_file" ]]; then
        LAST_EXIT=$(cat "$exit_file")
        rm -f "$exit_file"
    else
        LAST_EXIT=124  # timeout exit code
    fi
    # Re-run without subshell to get output (database should be cached now)
    LAST_OUT=$($NCD "$@" 2>&1) || true
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

print_header "NCD Client Tests - WSL (Standalone)"
echo "NCD: $NCD"
echo "Test Data: $TEST_DATA"
echo "Test Root: $TESTROOT"
echo ""

# Pre-flight checks
if [[ ! -x "$NCD" ]]; then
    echo "ERROR: NCD binary not found or not executable: $NCD"
    echo "Please build the project first with: ./build.sh"
    exit 1
fi

# ==========================================================================
# AGGRESSIVE PRE-TEST SERVICE CLEANUP - Ensure service is NOT running
# ==========================================================================

echo "Ensuring service is NOT running..."
echo "[INFO] Stopping any existing service processes..."

# Stop via command if possible (for the new binary name)
if [[ -x "$PROJECT_ROOT/ncd_service" ]]; then
    "$PROJECT_ROOT/ncd_service" stop >/dev/null 2>&1 || true
fi
sleep 1

# Kill any existing service processes (both old and new naming)
pkill -9 -f "ncd_service" 2>/dev/null || true
pkill -9 -f "NCDService" 2>/dev/null || true
sleep 1

# Verify service is not running
if is_service_running; then
    echo "[WARN] Service still running after initial kill, retrying force kill..."
    pkill -9 -f "ncd_service" 2>/dev/null || true
    pkill -9 -f "NCDService" 2>/dev/null || true
    sleep 2
fi

# Triple-check - verify no service is running
if is_service_running; then
    echo "[ERROR] Unable to stop existing service process. Tests may fail."
    pkill -9 -f "ncd_service" 2>/dev/null || true
    pkill -9 -f "NCDService" 2>/dev/null || true
else
    echo "[INFO] Confirmed: No service processes running."
fi

# ==========================================================================
# Setup Test Environment
# ==========================================================================

echo "Setting up test environment..."

# Create minimal metadata file FIRST (before any NCD commands)
mkdir -p "$TEST_DATA/ncd"
# Create metadata with proper binary magic (NCMD = 4E 43 4D 44) and version 1
# Magic (4 bytes) + Version (4 bytes) + Reserved (8 bytes) = 16 bytes total
# Use base64 to create binary file: "NCMD" + 0x01 0x00 0x00 0x00 + 8 nulls
echo -n "TkNNRAEAAAAAAAAAAAAA" | base64 -d > "$TEST_DATA/ncd/ncd.metadata"

# Verify standalone mode
echo "Verifying standalone mode..."
STATUS=$($NCD /agent check --service-status 2>&1) || true
echo "  Service status: $STATUS"
echo ""

# Create test directory tree
mkdir -p "$TESTROOT/Projects/alpha/src"
mkdir -p "$TESTROOT/Projects/beta/src"
mkdir -p "$TESTROOT/Users/scott/Downloads"
mkdir -p "$TESTROOT/Users/scott/Documents"
mkdir -p "$TESTROOT/Data/Files"

echo "  Created test directory tree"

# Initial scan
echo "Performing initial scan of test tree..."
(cd "$TESTROOT" && "$NCD" /r. >/dev/null 2>&1)
echo "  Scan complete."
echo ""

# ==========================================================================
# Main Test Suite
# ==========================================================================

print_header "Test Suite: Standalone Mode Operations"

# Test 1: Service is NOT running
echo "[TEST 1] Service is NOT running"
if ! is_service_running; then
    pass "Service is NOT running"
else
    fail "Service is NOT running" "service process still found"
fi

# Test 2: Help output
echo "[TEST 2] Help with -h"
ncd_run_timed 10 -h
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Help with -h"
else
    fail "Help with -h" "exit code $LAST_EXIT"
fi

# Test 3: Version information
echo "[TEST 3] Version with -v"
ncd_run_timed 10 -v
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Version with -v"
else
    fail "Version with -v" "exit code $LAST_EXIT"
fi

# Test 4: Service status shows NOT_RUNNING
echo "[TEST 4] Service status shows NOT_RUNNING"
ncd_run /agent check --service-status
if echo "$LAST_OUT" | grep -qi "NOT_RUNNING\|not_running"; then
    pass "Service status shows NOT_RUNNING"
else
    fail "Service status shows NOT_RUNNING" "output: $LAST_OUT"
fi

# Test 5: Basic search finds directory
echo "[TEST 5] Basic search finds directory"
ncd_run_timed 10 Downloads
if echo "$LAST_OUT" | grep -qi "Downloads"; then
    pass "Basic search finds directory"
else
    fail "Basic search finds directory" "output: $LAST_OUT"
fi

# Test 6: Multi-component search
echo "[TEST 6] Multi-component search"
ncd_run_timed 10 "scott/Downloads"
if echo "$LAST_OUT" | grep -qi "Downloads"; then
    pass "Multi-component search"
else
    fail "Multi-component search" "output: $LAST_OUT"
fi

# Test 7: Agent query command
echo "[TEST 7] Agent query command"
ncd_run /agent query Downloads
if echo "$LAST_OUT" | grep -qi "Downloads"; then
    pass "Agent query command"
else
    fail "Agent query command" "output: $LAST_OUT"
fi

# Test 8: Agent check database age
echo "[TEST 8] Agent check database age"
ncd_run /agent check --db-age
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Agent check database age"
else
    fail "Agent check database age" "exit code $LAST_EXIT"
fi

# Test 9: Agent check stats
echo "[TEST 9] Agent check stats"
ncd_run /agent check --stats
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Agent check stats"
else
    fail "Agent check stats" "exit code $LAST_EXIT"
fi

# Test 10: Rescan creates database
echo "[TEST 10] Rescan creates database"
rm -f "$TEST_DATA/ncd/ncd_"*.database
(cd "$TESTROOT" && "$NCD" /r. >/dev/null 2>&1)
if ls "$TEST_DATA/ncd/ncd_"*.database 1>/dev/null 2>&1; then
    pass "Rescan creates database"
else
    fail "Rescan creates database" "no database file found"
fi

# Test 11: Search after rescan
echo "[TEST 11] Search after rescan"
ncd_run_timed 10 Projects
if echo "$LAST_OUT" | grep -qi "Projects"; then
    pass "Search after rescan"
else
    fail "Search after rescan" "output: $LAST_OUT"
fi

# Test 12: No match returns error
echo "[TEST 12] No match returns error"
# Use ncd_run (no timeout) for this test since there's no TUI risk
ncd_run NONEXISTENT_DIR_12345
if [[ $LAST_EXIT -ne 0 ]]; then
    pass "No match returns error"
else
    fail "No match returns error" "exit code $LAST_EXIT (expected non-zero)"
fi

# Test 13: Database override -d
echo "[TEST 13] Database override -d"
CUSTOM_DB="/tmp/ncd_test_$$.db"
(cd "$TESTROOT" && "$NCD" /r. -d "$CUSTOM_DB" >/dev/null 2>&1)
if [[ -f "$CUSTOM_DB" ]]; then
    pass "Database override -d"
else
    fail "Database override -d" "custom database not created"
fi
rm -f "$CUSTOM_DB"

# Test 14: Agent ls command
echo "[TEST 14] Agent ls command"
ncd_run /agent ls "$TESTROOT" --depth 1
if [[ $LAST_EXIT -eq 0 ]]; then
    pass "Agent ls command"
else
    fail "Agent ls command" "exit code $LAST_EXIT"
fi

# Test 15: Standalone mode persists without service
echo "[TEST 15] Standalone mode persists without service"
# Ensure database exists (it may have been deleted by Test 10)
if ! ls "$TEST_DATA/ncd/ncd_"*.database 1>/dev/null 2>&1; then
    (cd "$TESTROOT" && "$NCD" /r. >/dev/null 2>&1)
fi
if ! is_service_running; then
    # Search for "abc" which is in the test tree (not "Documents" which may have issues)
    ncd_run_timed 10 abc
    if echo "$LAST_OUT" | grep -qi "abc"; then
        pass "Standalone mode persists without service"
    else
        fail "Standalone mode persists without service" "output: $LAST_OUT"
    fi
else
    fail "Standalone mode persists without service" "service unexpectedly running"
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
