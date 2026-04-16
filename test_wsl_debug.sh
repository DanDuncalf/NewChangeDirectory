#!/bin/bash
set -x  # Enable trace

export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/ncd_debug_$$
mkdir -p "$XDG_DATA_HOME/ncd"

TESTROOT=/tmp/ncd_debug_tree_$$
mkdir -p "$TESTROOT/Projects/alpha"
mkdir -p "$TESTROOT/Users/scott/Downloads"

echo "=== Environment ==="
echo "XDG_DATA_HOME=$XDG_DATA_HOME"
echo "TESTROOT=$TESTROOT"
echo "CWD=$(pwd)"
echo ""

echo "=== Scan from TESTROOT ==="
cd "$TESTROOT"
echo "Now in: $(pwd)"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /r.
echo "Exit code: $?"
echo ""

echo "=== Check created databases ==="
ls -la "$XDG_DATA_HOME/ncd/"
echo ""

echo "=== Search for Downloads (from TESTROOT) ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory Downloads
echo "Exit code: $?"
echo ""

echo "=== Search with explicit /agent query ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /agent query Downloads
echo "Exit code: $?"
echo ""

# Cleanup
rm -rf "$XDG_DATA_HOME" "$TESTROOT"
