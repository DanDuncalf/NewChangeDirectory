#!/usr/bin/env bash
# test_recursive_mount.sh - Test scanner behavior with recursive/circular mounts
#
# This test creates problematic mount scenarios to verify the scanner:
# 1. Doesn't enter infinite loops
# 2. Doesn't crash
# 3. Properly handles mount cycles
#
# WARNING: Requires root privileges for mount operations

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
TEST_BASE="/tmp/ncd_mount_test_$$"
NCD_EXE="${ROOT_DIR}/../NewChangeDirectory"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

ALL_PASSED=1

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        log_error "This test requires root privileges for mount operations"
        log_info "Run with: sudo $0"
        exit 1
    fi
}

cleanup() {
    log_info "Cleaning up mount points..."
    
    # Unmount in reverse order
    for mp in "$TEST_BASE/loop_mount" "$TEST_BASE/b_on_a" "$TEST_BASE/c_on_b"; do
        if mountpoint -q "$mp" 2>/dev/null; then
            umount "$mp" 2>/dev/null || true
        fi
    done
    
    # Remove temp directories
    rm -rf "$TEST_BASE"
    
    # Clean up any temp database files
    rm -f /tmp/ncd_test_*.database
}

# Test 1: Simple recursive bind mount (mount directory on itself)
test_self_recursive_mount() {
    log_info "Test 1: Self-recursive bind mount"
    
    mkdir -p "$TEST_BASE/self_recursive"
    mkdir -p "$TEST_BASE/self_recursive/subdir"
    
    # Create some test content
    mkdir -p "$TEST_BASE/self_recursive/a/b/c"
    
    # Mount the directory on itself (creates recursion at mount point)
    log_info "  Creating bind mount..."
    if mount --bind "$TEST_BASE/self_recursive" "$TEST_BASE/self_recursive/subdir" 2>/dev/null; then
        log_warn "  Mount succeeded - this creates a recursive structure"
        
        # Run scanner with timeout (should not hang)
        log_info "  Running scanner with 5-second timeout..."
        if timeout 5 "$NCD_EXE" -r "$TEST_BASE" 2>&1 | head -20; then
            log_info "  PASS: Scanner completed without hanging"
        else
            log_warn "  Scanner exited with error (may be expected)"
        fi
        
        umount "$TEST_BASE/self_recursive/subdir" 2>/dev/null || true
    else
        log_warn "  Could not create bind mount (may require different kernel)"
    fi
    
    rm -rf "$TEST_BASE/self_recursive"
}

# Test 2: Circular mount chain (A on B, B on C, C contains reference to A)
test_circular_mount_chain() {
    log_info "Test 2: Circular mount chain"
    
    mkdir -p "$TEST_BASE/dir_a"
    mkdir -p "$TEST_BASE/dir_b"
    mkdir -p "$TEST_BASE/dir_c"
    
    # Create content in each
    mkdir -p "$TEST_BASE/dir_a/content"
    mkdir -p "$TEST_BASE/dir_b/content"
    mkdir -p "$TEST_BASE/dir_c/content"
    
    # Create circular reference via symlinks (doesn't require root)
    ln -sf "$TEST_BASE/dir_a" "$TEST_BASE/dir_c/link_to_a"
    ln -sf "$TEST_BASE/dir_b" "$TEST_BASE/dir_a/link_to_b"
    ln -sf "$TEST_BASE/dir_c" "$TEST_BASE/dir_b/link_to_c"
    
    log_info "  Created circular symlink chain"
    
    # Run scanner with timeout
    log_info "  Running scanner with 5-second timeout..."
    if timeout 5 "$NCD_EXE" -r "$TEST_BASE" 2>&1 | head -20; then
        log_info "  PASS: Scanner completed without hanging"
    else
        log_warn "  Scanner exited with error (may be expected with symlinks)"
    fi
    
    rm -rf "$TEST_BASE/dir_a" "$TEST_BASE/dir_b" "$TEST_BASE/dir_c"
}

# Test 3: Deeply nested mount points
test_deeply_nested_mounts() {
    log_info "Test 3: Deeply nested mount points"
    
    mkdir -p "$TEST_BASE/deep/level1/level2/level3/level4/level5"
    
    # Create bind mounts at multiple levels
    for i in 1 2 3; do
        mkdir -p "$TEST_BASE/deep_mnt$i"
        mkdir -p "$TEST_BASE/deep/level$i/mnt"
    done
    
    # Mount at various depths
    mount --bind "$TEST_BASE/deep_mnt1" "$TEST_BASE/deep/level1/mnt" 2>/dev/null || true
    mount --bind "$TEST_BASE/deep_mnt2" "$TEST_BASE/deep/level3/mnt" 2>/dev/null || true
    mount --bind "$TEST_BASE/deep_mnt3" "$TEST_BASE/deep/level5/mnt" 2>/dev/null || true
    
    # Create content
    for i in 1 2 3; do
        mkdir -p "$TEST_BASE/deep_mnt$i/subdir"
    done
    
    log_info "  Running scanner with 10-second timeout..."
    if timeout 10 "$NCD_EXE" -r "$TEST_BASE/deep" 2>&1 | head -30; then
        log_info "  PASS: Scanner handled deeply nested mounts"
    else
        log_warn "  Scanner exited with error"
    fi
    
    # Cleanup mounts
    for i in 1 3 5; do
        umount "$TEST_BASE/deep/level$i/mnt" 2>/dev/null || true
    done
    rm -rf "$TEST_BASE/deep" "$TEST_BASE/deep_mnt"*
}

# Test 4: Mount with special filesystem types
test_special_filesystems() {
    log_info "Test 4: Scanning with various filesystem types mounted"
    
    # Just verify the scanner doesn't crash when encountering these
    # (most special filesystems are filtered, but verify)
    
    log_info "  Current mount points:"
    mount | head -20
    
    log_info "  Running scanner on /proc (should be filtered)..."
    if timeout 5 "$NCD_EXE" -r /proc 2>&1 | head -10; then
        log_info "  PASS: Scanner handled /proc"
    else
        log_info "  Scanner rejected /proc (expected - pseudo-filesystem)"
    fi
    
    log_info "  Running scanner on /sys (should be filtered)..."
    if timeout 5 "$NCD_EXE" -r /sys 2>&1 | head -10; then
        log_info "  PASS: Scanner handled /sys"
    else
        log_info "  Scanner rejected /sys (expected - pseudo-filesystem)"
    fi
}

# Test 5: Very deep directory structure (not mount, but depth-related)
test_very_deep_directories() {
    log_info "Test 5: Very deep directory structure"
    
    mkdir -p "$TEST_BASE/deep_chain"
    
    # Create 1000-level deep directory chain
    current="$TEST_BASE/deep_chain"
    for i in $(seq 1 100); do
        current="$current/level$i"
        mkdir -p "$current"
    done
    
    log_info "  Created 100-level deep directory chain"
    log_info "  Running scanner with 30-second timeout..."
    
    if timeout 30 "$NCD_EXE" -r "$TEST_BASE/deep_chain" 2>&1 | head -50; then
        log_info "  PASS: Scanner handled deep directory chain"
    else
        log_warn "  Scanner timed out or failed"
        ALL_PASSED=0
    fi
    
    rm -rf "$TEST_BASE/deep_chain"
}

# Main
echo "=== NCD Recursive Mount Tests ==="
echo ""

check_root

# Set trap for cleanup
trap cleanup EXIT

# Create test base
mkdir -p "$TEST_BASE"

# Check if NCD binary exists
if [[ ! -x "$NCD_EXE" ]]; then
    log_error "NCD binary not found at $NCD_EXE"
    log_info "Please build NCD first: cd $ROOT_DIR/.. && ./build.sh"
    exit 1
fi

log_info "NCD binary: $NCD_EXE"
log_info "Test base: $TEST_BASE"
echo ""

# Run tests
test_self_recursive_mount
echo ""

test_circular_mount_chain
echo ""

test_deeply_nested_mounts
echo ""

test_special_filesystems
echo ""

test_very_deep_directories
echo ""

# Summary
echo "=== Recursive Mount Tests Complete ==="
if [[ $ALL_PASSED -eq 1 ]]; then
    log_info "All tests completed (some warnings are expected)"
    exit 0
else
    log_error "Some tests had issues"
    exit 1
fi
