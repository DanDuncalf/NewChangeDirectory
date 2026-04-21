#!/bin/bash
export NCD_TEST_MODE=1
TESTROOT=/tmp/ncd_test_tree_123456
rm -rf "$TESTROOT"
mkdir -p "$TESTROOT/Projects/alpha"
TEST_DATA=/tmp/ncd_test_data_123456
rm -rf "$TEST_DATA"
mkdir -p "$TEST_DATA/ncd"
export XDG_DATA_HOME="$TEST_DATA"
printf '\x4E\x43\x4D\x44\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$TEST_DATA/ncd/ncd.metadata"
cd "$TESTROOT"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r . >/dev/null 2>&1
cd /mnt/e/llama/NewChangeDirectory
echo "=== Tree from project root ==="
./NewChangeDirectory --agent tree "/tmp/ncd_test_tree_123456/Projects" --json --depth 2 2>&1
rm -rf "$TESTROOT" "$TEST_DATA"
