#!/bin/bash
export NCD_TEST_MODE=1
TESTROOT=/tmp/ncd_agent_test_12345
rm -rf "$TESTROOT"
mkdir -p "$TESTROOT/Projects/alpha/src/main"
TEST_DATA=/tmp/ncd_agent_data_12345
rm -rf "$TEST_DATA"
mkdir -p "$TEST_DATA/ncd"
export XDG_DATA_HOME="$TEST_DATA"
printf '\x4E\x43\x4D\x44\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$TEST_DATA/ncd/ncd.metadata"
cd "$TESTROOT"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r . >/dev/null 2>&1

echo "=== Query root ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "/tmp/ncd_agent_test_12345" --json 2>&1

echo "=== Query Projects ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "/tmp/ncd_agent_test_12345/Projects" --json 2>&1

echo "=== Query alpha ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "/tmp/ncd_agent_test_12345/Projects/alpha" --json 2>&1

echo "=== Query alpha with trailing slash ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "/tmp/ncd_agent_test_12345/Projects/alpha/" --json 2>&1

echo "=== DB dump strings ==="
strings "$TEST_DATA/ncd/"*.database | head -20

rm -rf "$TESTROOT" "$TEST_DATA"
