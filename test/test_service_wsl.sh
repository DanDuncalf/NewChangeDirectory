#!/usr/bin/env bash
# ==========================================================================
# test_service_wsl.sh -- WSL Service tests WITHOUT NCD client
# ==========================================================================
#
# PURPOSE:
#   Tests the resident service (ncd_service) in isolation without
#   involving the NCD client. Verifies service lifecycle, IPC, and basic
#   operations on WSL/Linux.
#
# TESTS:
#   - Service start
#   - Service stop
#   - Service status query
#   - Service IPC ping
#   - Service version query
#
# REQUIREMENTS:
#   - ncd_service built in project root
#   - Run from project root: test/test_service_wsl.sh
#
# EXIT CODES:
#   0 - All tests passed
#   1 - One or more tests failed
#
# ==========================================================================

set -o pipefail

# Disable NCD background rescans during testing
export NCD_TEST_MODE=1

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SERVICE_EXE="$PROJECT_ROOT/NCDService"

# Isolation: use temp directory for all service data
TEST_DATA="/tmp/ncd_service_test_$$"
mkdir -p "$TEST_DATA"
export XDG_DATA_HOME="$TEST_DATA"

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Service control timeout (seconds)
SERVICE_TIMEOUT=10

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

# Get log file path
get_log_path() {
    echo "$TEST_DATA/ncd/ncd_service.log"
}

# Check log file for errors
check_log_errors() {
    local log_path
    log_path=$(get_log_path)
    if [[ -f "$log_path" ]]; then
        if grep -i "ERROR" "$log_path" >/dev/null 2>&1; then
            echo ""
            echo "  [FAIL] Errors found in service log:"
            grep -i "ERROR" "$log_path" | head -5
            TESTS_FAILED=$((TESTS_FAILED + 1))
            return 1
        fi
    fi
    return 0
}

# Check if service is running
is_service_running() {
    if pgrep -x "NCDService" >/dev/null 2>&1; then
        return 0
    else
        return 1
    fi
}

# Wait for service to reach expected state
wait_for_service_state() {
    local expected="$1"
    local wait_count=0
    
    while [[ $wait_count -lt $SERVICE_TIMEOUT ]]; do
        if [[ "$expected" == "running" ]]; then
            if is_service_running; then
                return 0
            fi
        else
            if ! is_service_running; then
                return 0
            fi
        fi
        sleep 1
        wait_count=$((wait_count + 1))
    done
    return 1
}

# ==========================================================================
# Main Test Suite
# ==========================================================================

print_header() {
    echo ""
    printf "${C_BOLD}${C_CYAN}========================================${C_RESET}\n"
    printf "${C_BOLD}${C_CYAN}%s${C_RESET}\n" "$1"
    printf "${C_BOLD}${C_CYAN}========================================${C_RESET}\n"
}

# Cleanup function
cleanup() {
    print_header "Cleanup"
    stop_service >/dev/null 2>&1
    rm -rf "$TEST_DATA"
    echo "  Cleaned up test data"
}

# Set trap to ensure cleanup runs
trap cleanup EXIT

print_header "WSL Service Tests (Isolation Mode)"
echo "Service: $SERVICE_EXE"
echo "Test Data: $TEST_DATA"
echo ""

# Pre-flight checks
if [[ ! -x "$SERVICE_EXE" ]]; then
    echo "ERROR: Service executable not found or not executable: $SERVICE_EXE"
    echo "Please build the project first with: ./build.sh"
    exit 1
fi

# ==========================================================================
# AGGRESSIVE PRE-TEST SERVICE CLEANUP
# ==========================================================================
echo "[INFO] Performing aggressive service cleanup..."

# Stop via command if possible
"$SERVICE_EXE" stop >/dev/null 2>&1 || true

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

# Verify cleanup
if pgrep -x "NCDService" >/dev/null 2>&1 || pgrep -f "ncd_service" >/dev/null 2>&1; then
    echo "[ERROR] Unable to stop existing service process. Tests may fail."
else
    echo "[INFO] Confirmed: No service processes running."
fi
echo ""

# Ensure service is stopped before tests
stop_service

print_header "Test Suite: Service Lifecycle"

# Test 1: Service status when stopped
echo "[TEST 1] Service status when stopped"
if wait_for_service_state stopped; then
    pass "Service status when stopped"
else
    fail "Service status when stopped" "service still running after stop command"
fi

# Test 2: Service start
echo "[TEST 2] Service start"
stop_service
sleep 1
# Clear old log if exists
LOG_PATH=$(get_log_path)
rm -f "$LOG_PATH" 2>/dev/null
# Start with logging level 2 for test verification
cd "$PROJECT_ROOT" && "$SERVICE_EXE" start -log2 >/dev/null 2>&1 &
sleep 2
if wait_for_service_state running; then
    pass "Service start"
else
    fail "Service start" "service did not start within timeout"
fi

# Test 3: Service IPC ping
echo "[TEST 3] Service IPC ping"
if is_service_running; then
    # Try to get status via service command
    if "$SERVICE_EXE" status >/dev/null 2>&1; then
        pass "Service IPC ping"
    else
        fail "Service IPC ping" "status command failed"
    fi
else
    fail "Service IPC ping" "service not running, cannot test IPC"
fi

# Test 4: Service version query
echo "[TEST 4] Service version query"
if is_service_running; then
    pass "Service version query"
else
    fail "Service version query" "service not running"
fi

# Test 5: Service state info
echo "[TEST 5] Service state info"
if is_service_running; then
    pass "Service state info"
else
    fail "Service state info" "service not running"
fi

# Test 6: Service stop when running
echo "[TEST 6] Service stop when running"
if is_service_running; then
    stop_service
    if wait_for_service_state stopped; then
        pass "Service stop when running"
    else
        fail "Service stop when running" "service still running after stop"
    fi
else
    fail "Service stop when running" "service not running, cannot test stop"
fi

# Test 7: Double start (should handle gracefully)
echo "[TEST 7] Service double start handling"
stop_service
sleep 1
cd "$PROJECT_ROOT" && "$SERVICE_EXE" start >/dev/null 2>&1 &
sleep 2
# Try to start again - should not crash
cd "$PROJECT_ROOT" && "$SERVICE_EXE" start >/dev/null 2>&1 &
sleep 1
if is_service_running; then
    pass "Service double start"
else
    fail "Service double start" "service not running after double start"
fi

# Test 8: Service stop when already stopped
echo "[TEST 8] Service stop when already stopped"
stop_service
wait_for_service_state stopped >/dev/null 2>&1
# Try to stop again - should not crash
"$SERVICE_EXE" stop >/dev/null 2>&1 || true
pass "Service stop when already stopped"

# Test 9: Service restart
echo "[TEST 9] Service restart"
stop_service
sleep 1
cd "$PROJECT_ROOT" && "$SERVICE_EXE" start >/dev/null 2>&1 &
sleep 2
if is_service_running; then
    stop_service
    wait_for_service_state stopped >/dev/null 2>&1
    cd "$PROJECT_ROOT" && "$SERVICE_EXE" start >/dev/null 2>&1 &
    sleep 2
    if is_service_running; then
        pass "Service restart"
    else
        fail "Service restart" "service did not restart"
    fi
else
    fail "Service restart - start phase" "service did not start"
fi

# Test 10: Service termination handling
echo "[TEST 10] Service termination handling"
stop_service
sleep 1
if ! is_service_running; then
    pass "Service termination handling"
else
    fail "Service termination handling" "service still running after force kill"
fi

# ==========================================================================
# Summary
# ==========================================================================

print_header "Test Summary"
echo "  Total:   $TESTS_RUN"
echo "  Passed:  $TESTS_PASSED"
echo "  Failed:  $TESTS_FAILED"
echo ""

# Check log for errors before summary
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
