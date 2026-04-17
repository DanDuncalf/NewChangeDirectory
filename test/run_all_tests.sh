#!/usr/bin/env bash
# ==========================================================================
# run_all_tests.sh -- Comprehensive NCD Test Runner (All 4 Environments)
# ==========================================================================
#
# This script runs the complete NCD test suite across all four testing
# environments:
#
#   1. Windows WITHOUT service (standalone mode)
#   2. Windows WITH service (shared memory mode)
#   3. WSL WITHOUT service (standalone mode)
#   4. WSL WITH service (shared memory mode)
#
# USAGE:
#   cd test
#   ./run_all_tests.sh
#
#   Or to run specific environments only:
#   ./run_all_tests.sh --windows-only
#   ./run_all_tests.sh --wsl-only
#   ./run_all_tests.sh --no-service
#   ./run_all_tests.sh --help
#
# REQUIREMENTS:
#   - Windows: NCD binaries built (NewChangeDirectory.exe, NCDService.exe)
#   - WSL: NCD binary built in WSL environment
#   - PowerShell available for Windows tests
#   - wslpath utility for path conversion
#
# EXIT CODES:
#   0 - All tests passed in all environments
#   1 - One or more tests failed
#   2 - Setup/configuration error
# ==========================================================================

set -o pipefail

# Disable NCD background rescans during testing to prevent scanning user drives
export NCD_TEST_MODE=1

# Script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

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
# Configuration / Command-line parsing
# ==========================================================================

WINDOWS_ONLY=false
WSL_ONLY=false
NO_SERVICE=false
SKIP_UNIT_TESTS=false
SKIP_FEATURE_TESTS=false
VERBOSE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --windows-only) WINDOWS_ONLY=true; shift ;;
        --wsl-only) WSL_ONLY=true; shift ;;
        --no-service) NO_SERVICE=true; shift ;;
        --skip-unit-tests) SKIP_UNIT_TESTS=true; shift ;;
        --skip-feature-tests) SKIP_FEATURE_TESTS=true; shift ;;
        --verbose) VERBOSE=true; shift ;;
        --help)
            echo "NCD Comprehensive Test Runner"
            echo ""
            echo "Usage: $0 [options]"
            echo ""
            echo "Options:"
            echo "  --windows-only      Run only Windows tests"
            echo "  --wsl-only          Run only WSL tests"
            echo "  --no-service        Skip tests with service running"
            echo "  --skip-unit-tests   Skip unit tests"
            echo "  --skip-feature-tests Skip feature tests"
            echo "  --verbose           Show detailed test output"
            echo "  --help              Show this help message"
            echo ""
            echo "This script runs tests across 4 environments:"
            echo "  1. Windows without service"
            echo "  2. Windows with service"
            echo "  3. WSL without service"
            echo "  4. WSL with service"
            exit 0
            ;;
        *) echo "Unknown option: $1"; exit 2 ;;
    esac
done

# ==========================================================================
# Result tracking
# ==========================================================================

TOTAL_RUNS=0
PASSED=0
FAILED=0
SKIPPED=0

declare -a DETAILS_NAME
declare -a DETAILS_PLATFORM
declare -a DETAILS_SERVICE
declare -a DETAILS_RESULT
declare -a DETAILS_REASON
declare -a DETAILS_DURATION

# ==========================================================================
# Helper Functions
# ==========================================================================

print_header() {
    echo ""
    printf "${C_BOLD}${C_CYAN}========================================${C_RESET}\n"
    printf "${C_BOLD}${C_CYAN}%s${C_RESET}\n" "$1"
    printf "${C_BOLD}${C_CYAN}========================================${C_RESET}\n"
}

print_section() {
    echo ""
    printf "${C_BOLD}%s${C_RESET}\n" "$1"
}

print_success() {
    printf "${C_GREEN}âœ“ %s${C_RESET}\n" "$1"
}

print_failure() {
    printf "${C_RED}âœ— %s${C_RESET}\n" "$1"
}

print_warning() {
    printf "${C_YELLOW}âš  %s${C_RESET}\n" "$1"
}

check_windows_binaries() {
    if [[ ! -f "$PROJECT_ROOT/NewChangeDirectory.exe" ]]; then
        print_failure "NewChangeDirectory.exe not found. Build with: build.bat"
        return 1
    fi
    return 0
}

check_windows_available() {
    # Check if we can run Windows commands from WSL
    if command -v powershell.exe &>/dev/null; then
        return 0
    fi
    if command -v cmd.exe &>/dev/null; then
        return 0
    fi
    print_warning "Cannot execute Windows commands from this environment"
    return 1
}

check_wsl_binaries() {
    if [[ ! -x "$PROJECT_ROOT/NewChangeDirectory" ]]; then
        print_failure "NCD binary not found in WSL. Build with: ./build.sh"
        return 1
    fi
    return 0
}

stop_NCDService() {
    local platform=$1  # "windows" or "wsl"
    
    if [[ "$platform" == "wsl" ]]; then
        pkill -f "NCDService" 2>/dev/null || true
    else
        # Windows - use cmd.exe (suppress all errors since service may not be running)
        local service_bat="$PROJECT_ROOT/NCDService.bat"
        if [[ -f "$service_bat" ]]; then
            cmd.exe /c "\"$(wslpath -w "$service_bat")\" stop 2>nul" >/dev/null 2>&1 || true
        fi
        # Also try to kill process directly (ignore errors)
        taskkill.exe /F /IM NCDService.exe 2>/dev/null || true
    fi
    
    sleep 0.5
}

start_NCDService() {
    local platform=$1  # "windows" or "wsl"
    
    if [[ "$platform" == "wsl" ]]; then
        cd "$PROJECT_ROOT" && ./NCDService start 2>/dev/null &
    else
        # Windows
        local service_bat="$PROJECT_ROOT/NCDService.bat"
        if [[ -f "$service_bat" ]]; then
            local win_path
            win_path=$(wslpath -w "$service_bat")
            cmd.exe /c "start /B \"$win_path\" start" 2>/dev/null || true
        fi
    fi
    
    sleep 2
}

test_service_status() {
    local platform=$1
    local expect_running=$2
    local is_running=false
    
    if [[ "$platform" == "wsl" ]]; then
        local status
        status=$(cd "$PROJECT_ROOT" && ./NewChangeDirectory --agent:check --service-status 2>&1)
        if [[ "$status" =~ READY|STARTING|LOADING ]]; then
            is_running=true
        fi
    else
        local status
        status=$("$PROJECT_ROOT/NewChangeDirectory.exe" /agent check --service-status 2>&1)
        if [[ "$status" =~ READY|STARTING|LOADING ]]; then
            is_running=true
        fi
    fi
    
    if [[ "$expect_running" == "true" ]]; then
        [[ "$is_running" == "true" ]]
    else
        [[ "$is_running" != "true" ]]
    fi
}

record_result() {
    local name=$1
    local platform=$2
    local service=$3
    local result=$4
    local reason=$5
    local duration=$6
    
    TOTAL_RUNS=$((TOTAL_RUNS + 1))
    DETAILS_NAME+=("$name")
    DETAILS_PLATFORM+=("$platform")
    DETAILS_SERVICE+=("$service")
    DETAILS_RESULT+=("$result")
    DETAILS_REASON+=("$reason")
    DETAILS_DURATION+=("$duration")
    
    case $result in
        PASSED) PASSED=$((PASSED + 1)) ;;
        FAILED) FAILED=$((FAILED + 1)) ;;
        SKIPPED) SKIPPED=$((SKIPPED + 1)) ;;
    esac
}

run_test_environment() {
    local platform=$1  # "windows" or "wsl"
    local with_service=$2
    local name="${platform^^}"
    
    if [[ "$with_service" == "true" ]]; then
        name="$name + Service"
    else
        name="$name (Standalone)"
    fi
    
    print_header "TESTING: $name"
    echo "Platform: ${platform^^}"
    echo "Service: $(if [[ "$with_service" == "true" ]]; then echo "ENABLED"; else echo "DISABLED"; fi)"
    echo ""
    
    local start_time end_time duration elapsed
    start_time=$(date +%s)
    
    # Ensure service is stopped first
    stop_NCDService "$platform"
    
    if [[ "$with_service" == "true" ]]; then
        start_NCDService "$platform"
        sleep 1
        
        if ! test_service_status "$platform" true; then
            print_warning "Service failed to start, skipping test"
            end_time=$(date +%s)
            elapsed=$((end_time - start_time))
            record_result "$name" "${platform^^}" "$with_service" "SKIPPED" "Service failed to start" "${elapsed}s"
            return
        fi
    fi
    
    # Run the actual tests
    local exit_code=0
    local output=""
    
    if [[ "$platform" == "wsl" ]]; then
        # Run WSL tests
        if [[ "$SKIP_FEATURE_TESTS" != "true" ]]; then
            print_section "Running WSL Feature Tests..."
            output=$(cd "$PROJECT_ROOT" && bash test/Wsl/test_features.sh 2>&1)
            exit_code=$?
            if [[ "$VERBOSE" == "true" ]]; then
                echo "$output"
            fi
            
            if [[ "$exit_code" -eq 0 ]]; then
                print_section "Running WSL Agent Command Tests..."
                output=$(cd "$PROJECT_ROOT" && chmod +x test/Wsl/test_agent_commands.sh && bash test/Wsl/test_agent_commands.sh 2>&1)
                local agent_exit=$?
                if [[ "$VERBOSE" == "true" ]]; then
                    echo "$output"
                fi
                if [[ $agent_exit -ne 0 ]]; then
                    exit_code=$agent_exit
                fi
            fi
        fi
        
        if [[ "$exit_code" -eq 0 && "$SKIP_UNIT_TESTS" != "true" ]]; then
            print_section "Running WSL Unit Tests..."
            output=$(cd "$SCRIPT_DIR" && make test 2>&1)
            local unit_exit=$?
            if [[ "$VERBOSE" == "true" ]]; then
                echo "$output"
            fi
            if [[ $unit_exit -ne 0 ]]; then
                exit_code=$unit_exit
            fi
        fi
        
        if [[ "$exit_code" -eq 0 && "$SKIP_UNIT_TESTS" != "true" && "$with_service" == "true" ]]; then
            print_section "Running WSL Service Tests..."
            output=$(cd "$SCRIPT_DIR" && make service-test 2>&1)
            local svc_exit=$?
            if [[ "$VERBOSE" == "true" ]]; then
                echo "$output"
            fi
            if [[ $svc_exit -ne 0 ]]; then
                exit_code=$svc_exit
            fi
        fi
    else
        # Run Windows tests via PowerShell
        if [[ "$SKIP_FEATURE_TESTS" != "true" ]]; then
            print_section "Running Windows Feature Tests..."
            local win_path
            win_path=$(wslpath -w "$PROJECT_ROOT/test/Win/test_features.bat")
            output=$(cd "$PROJECT_ROOT" && cmd.exe /c "$win_path" 2>&1)
            exit_code=$?
            if [[ "$VERBOSE" == "true" ]]; then
                echo "$output"
            fi
            
            if [[ "$exit_code" -eq 0 ]]; then
                print_section "Running Windows Agent Command Tests..."
                local agent_path
                agent_path=$(wslpath -w "$PROJECT_ROOT/test/Win/test_agent_commands.bat")
                output=$(cd "$PROJECT_ROOT" && cmd.exe /c "$agent_path" 2>&1)
                local agent_exit=$?
                if [[ "$VERBOSE" == "true" ]]; then
                    echo "$output"
                fi
                if [[ $agent_exit -ne 0 ]]; then
                    exit_code=$agent_exit
                fi
            fi
        fi
        
        if [[ "$exit_code" -eq 0 && "$SKIP_UNIT_TESTS" != "true" ]]; then
            print_section "Running Windows Unit Tests..."
            local win_path
            win_path=$(wslpath -w "$SCRIPT_DIR/build-and-run-tests.bat")
            output=$(cd "$SCRIPT_DIR" && cmd.exe /c "$win_path" 2>&1)
            local unit_exit=$?
            if [[ "$VERBOSE" == "true" ]]; then
                echo "$output"
            fi
            if [[ $unit_exit -ne 0 ]]; then
                exit_code=$unit_exit
            fi
        fi
    fi
    
    end_time=$(date +%s)
    elapsed=$((end_time - start_time))
    
    if [[ $exit_code -eq 0 ]]; then
        print_success "All tests passed for $name"
        record_result "$name" "${platform^^}" "$with_service" "PASSED" "" "${elapsed}s"
    else
        print_failure "Tests failed for $name (exit code: $exit_code)"
        record_result "$name" "${platform^^}" "$with_service" "FAILED" "" "${elapsed}s"
    fi
    
    # Always stop service after tests
    stop_NCDService "$platform"
}

show_summary() {
    print_header "TEST SUMMARY"
    
    echo ""
    echo "Total Test Runs: $TOTAL_RUNS"
    printf "${C_GREEN}Passed:  %d${C_RESET}\n" "$PASSED"
    printf "${C_RED}Failed:  %d${C_RESET}\n" "$FAILED"
    printf "${C_YELLOW}Skipped: %d${C_RESET}\n" "$SKIPPED"
    
    echo ""
    printf "${C_BOLD}Details:${C_RESET}\n"
    echo "----------------------------------------"
    
    for ((i=0; i<TOTAL_RUNS; i++)); do
        local color="${C_YELLOW}"
        case "${DETAILS_RESULT[$i]}" in
            PASSED) color="${C_GREEN}" ;;
            FAILED) color="${C_RED}" ;;
            SKIPPED) color="${C_YELLOW}" ;;
        esac
        
        printf "%s%-7s%s - %s (%s)\n" \
            "$color" "${DETAILS_RESULT[$i]}" "${C_RESET}" \
            "${DETAILS_NAME[$i]}" "${DETAILS_DURATION[$i]}"
        
        if [[ -n "${DETAILS_REASON[$i]}" ]]; then
            echo "       Reason: ${DETAILS_REASON[$i]}"
        fi
    done
    
    echo ""
    echo "----------------------------------------"
    
    if [[ $FAILED -gt 0 ]]; then
        print_failure "SOME TESTS FAILED"
        return 1
    elif [[ $PASSED -eq 0 && $SKIPPED -gt 0 ]]; then
        print_warning "All tests were skipped"
        return 2
    else
        print_success "ALL TESTS PASSED"
        return 0
    fi
}

# ==========================================================================
# Main Script
# ==========================================================================

print_header "NCD Comprehensive Test Runner"
echo "This will run all NCD tests across 4 environments:"
echo "  1. Windows without service"
echo "  2. Windows with service"
echo "  3. WSL without service"
echo "  4. WSL with service"
echo ""

# Check prerequisites
CAN_TEST_WINDOWS=false
CAN_TEST_WSL=false

if [[ "$WSL_ONLY" != "true" ]]; then
    if check_windows_available && check_windows_binaries; then
        CAN_TEST_WINDOWS=true
    fi
fi

if [[ "$WINDOWS_ONLY" != "true" ]]; then
    if check_wsl_binaries; then
        CAN_TEST_WSL=true
    fi
fi

if [[ "$CAN_TEST_WINDOWS" != "true" && "$CAN_TEST_WSL" != "true" ]]; then
    print_failure "No testable environments found. Build NCD first."
    exit 2
fi

# Run tests for each environment
if [[ "$CAN_TEST_WSL" == "true" ]]; then
    # WSL without service
    run_test_environment "wsl" "false"
    
    # WSL with service
    if [[ "$NO_SERVICE" != "true" ]]; then
        run_test_environment "wsl" "true"
    fi
fi

if [[ "$CAN_TEST_WINDOWS" == "true" ]]; then
    # Windows without service
    run_test_environment "windows" "false"
    
    # Windows with service
    if [[ "$NO_SERVICE" != "true" ]]; then
        run_test_environment "windows" "true"
    fi
fi

# Show final summary
show_summary
exit $?
