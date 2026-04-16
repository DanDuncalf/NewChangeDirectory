#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/trace_test_$$
mkdir -p "$XDG_DATA_HOME/ncd"

TESTROOT=/tmp/trace_tree_$$
mkdir -p "$TESTROOT/Users/scott/Downloads"

echo "=== Trace Test ==="
echo "Running from: $(pwd)"
echo "TESTROOT: $TESTROOT"
echo "XDG_DATA_HOME: $XDG_DATA_HOME"
echo ""

# Initial scan
echo "=== Scan ==="
cd "$TESTROOT"
echo "Now in: $(pwd)"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /r.
echo "Scan done"
echo ""

# Check database
echo "=== Database files ==="
ls -la "$XDG_DATA_HOME/ncd/"
echo ""

# Try search from TESTROOT
echo "=== Search from TESTROOT ==="
echo "CWD: $(pwd)"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory Downloads 2>&1
echo "Exit: $?"
echo ""

# Try from different dir
echo "=== Search from /tmp ==="
cd /tmp
echo "CWD: $(pwd)"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory Downloads 2>&1
echo "Exit: $?"

rm -rf "$XDG_DATA_HOME" "$TESTROOT"
