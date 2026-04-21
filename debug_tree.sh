#!/bin/bash
export NCD_TEST_MODE=1
TESTROOT=/tmp/ncd_debug_tree_12345
rm -rf "$TESTROOT"
mkdir -p "$TESTROOT/Projects/alpha"
TEST_DATA=/tmp/ncd_debug_data_12345
rm -rf "$TEST_DATA"
mkdir -p "$TEST_DATA/ncd"
export XDG_DATA_HOME="$TEST_DATA"
printf '\x4E\x43\x4D\x44\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$TEST_DATA/ncd/ncd.metadata"
cd "$TESTROOT"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r . >/dev/null 2>&1
echo "Tree output:"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "/tmp/ncd_debug_tree_12345/Projects" --json 2>&1
EC=$?
echo "Exit code: $EC"
ls -la "$TEST_DATA/ncd/"
rm -rf "$TESTROOT" "$TEST_DATA"
