#!/bin/bash
export NCD_TEST_MODE=1
TESTROOT=/tmp/ncd_agent_test_12345
rm -rf "$TESTROOT"
mkdir -p "$TESTROOT/Projects/alpha/src/main"
mkdir -p "$TESTROOT/Projects/alpha/src/test"
mkdir -p "$TESTROOT/Projects/alpha/docs"
mkdir -p "$TESTROOT/Projects/beta/src"
mkdir -p "$TESTROOT/Users/scott/Downloads"
mkdir -p "$TESTROOT/Users/scott/Documents"
mkdir -p "$TESTROOT/Media/Photos2024"
mkdir -p "$TESTROOT/Media/Videos"
TEST_DATA=/tmp/ncd_agent_data_12345
rm -rf "$TEST_DATA"
mkdir -p "$TEST_DATA/ncd"
export XDG_DATA_HOME="$TEST_DATA"
printf '\x4E\x43\x4D\x44\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$TEST_DATA/ncd/ncd.metadata"
cd "$TESTROOT"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r . >/dev/null 2>&1

echo "=== Original W10 test ==="
OUTPUT=$(/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "$TESTROOT/Projects" --json --depth 2 2>&1)
echo "$OUTPUT"
if echo "$OUTPUT" | grep -q '"v":1' && echo "$OUTPUT" | grep -q '"tree":'; then
    echo "PASS W10"
else
    echo "FAIL W10"
fi

echo "=== Original W11 test ==="
OUTPUT=$(/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "$TESTROOT/Projects" --flat --depth 2 2>&1)
echo "$OUTPUT"
if echo "$OUTPUT" | grep -q '/'; then
    echo "PASS W11"
else
    echo "FAIL W11"
fi

echo "=== Original W12 test ==="
DEPTH1=$(/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "$TESTROOT" --depth 1 2>&1 | wc -l)
DEPTH3=$(/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "$TESTROOT" --depth 3 2>&1 | wc -l)
echo "DEPTH1=$DEPTH1 DEPTH3=$DEPTH3"
if [ "$DEPTH3" -gt "$DEPTH1" ]; then
    echo "PASS W12"
else
    echo "FAIL W12"
fi

rm -rf "$TESTROOT" "$TEST_DATA"
