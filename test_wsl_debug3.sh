#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/ncd_debug_$$
mkdir -p "$XDG_DATA_HOME/ncd"

TESTROOT=/tmp/ncd_debug_tree_$$
mkdir -p "$TESTROOT/Projects/alpha"
mkdir -p "$TESTROOT/Users/scott/Downloads"

echo "=== Environment ==="
echo "XDG_DATA_HOME=$XDG_DATA_HOME"
echo "TESTROOT=$TESTROOT"

echo "=== Scan from TESTROOT ==="
cd "$TESTROOT"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /r.
echo "Scan exit code: $?"

echo "=== Check created databases ==="
ls -la "$XDG_DATA_HOME/ncd/"

echo "=== Agent query Downloads (from TESTROOT) ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /agent query Downloads
echo "Agent query exit code: $?"

echo "=== Agent tree (from TESTROOT) ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /agent tree "$TESTROOT" --depth 2

echo "=== Agent check --stats ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /agent check --stats

# Cleanup
rm -rf "$XDG_DATA_HOME" "$TESTROOT"
