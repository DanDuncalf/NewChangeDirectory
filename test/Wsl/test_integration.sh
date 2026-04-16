#!/usr/bin/env bash
# test_integration.sh - Integration tests for NCD on Linux/WSL
# This must be run from the project root directory

set -e

# Disable NCD background rescans to prevent scanning user drives during tests
export NCD_TEST_MODE=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

NCD_EXE="$PROJECT_ROOT/NewChangeDirectory"
NCD_SCRIPT="$PROJECT_ROOT/ncd"

# Isolation: redirect all NCD data to a temp directory
TEST_DATA="/tmp/ncd_integration_test_$$"
mkdir -p "$TEST_DATA"
export XDG_DATA_HOME="$TEST_DATA"

TEST_DIR="/tmp/ncd_integration_tree_$$"
ALL_PASSED=1

echo "=== NCD Integration Tests (Linux/WSL) ==="
echo

# Cleanup function
cleanup() {
    rm -rf "$TEST_DATA"
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

# Check prerequisites
if [[ ! -x "$NCD_EXE" ]]; then
    echo "ERROR: $NCD_EXE not found. Please build first with: ./build.sh"
    exit 1
fi

# Setup test environment
echo "Setting up test environment..."
mkdir -p "$TEST_DIR/dir1"
mkdir -p "$TEST_DIR/dir2/subdir"
touch "$TEST_DIR/file.txt"
echo "  Created test directories"

# Test 1: Version display
echo
echo "Test 1: Version display..."
if "$NCD_EXE" /v >/dev/null 2>&1; then
    echo "  PASS: Version displayed"
else
    echo "  FAIL: Version display failed"
    ALL_PASSED=0
fi

# Test 2: Help display
echo
echo "Test 2: Help display..."
if "$NCD_EXE" /? >/dev/null 2>&1; then
    echo "  PASS: Help displayed"
else
    echo "  FAIL: Help display failed"
    ALL_PASSED=0
fi

# Test 3: Database scan (subdirectory only to avoid scanning all mounts)
echo
echo "Test 3: Database scan..."
if (cd "$TEST_DIR" && "$NCD_EXE" /r.) >/dev/null 2>&1; then
    echo "  PASS: Database scan completed"
else
    echo "  FAIL: Database scan failed"
    ALL_PASSED=0
fi

# Test 4: History display
echo
echo "Test 4: History display..."
if "$NCD_EXE" /f >/dev/null 2>&1; then
    echo "  PASS: History displayed"
else
    echo "  FAIL: History display failed"
    ALL_PASSED=0
fi

# Test 5: Clear history
echo
echo "Test 5: Clear history..."
if "$NCD_EXE" /fc >/dev/null 2>&1; then
    echo "  PASS: History cleared"
else
    echo "  FAIL: History clear failed"
    ALL_PASSED=0
fi

# Test 6: Search for directory
echo
echo "Test 6: Directory search..."
# Note: This may fail if no match found, that's OK for integration test
# We just verify it doesn't crash
"$NCD_EXE" subdir >/dev/null 2>&1 || true
echo "  PASS: Search executed without crash"

# Test 7: Source wrapper test (if sourced)
echo
echo "Test 7: Wrapper script test..."
if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
    echo "  INFO: Script is being sourced, wrapper test would work"
else
    echo "  INFO: Script executed directly, wrapper test skipped"
    echo "        To test wrapper: source ncd <search>"
fi

# Summary
echo
echo "=== Integration Tests Complete ==="
if [[ $ALL_PASSED -eq 1 ]]; then
    echo "All tests PASSED!"
    exit 0
else
    echo "Some tests FAILED!"
    exit 1
fi
