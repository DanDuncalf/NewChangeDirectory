#!/bin/bash
export NCD_TEST_MODE=1
export NCD_UI_KEYS=ENTER
export NCD_UI_KEYS_STRICT=1
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

echo "=== W10: tree --json ==="
OUTPUT=$(/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "$TESTROOT/Projects/alpha" --json 2>&1)
echo "$OUTPUT"
if echo "$OUTPUT" | grep -q '"v":1' && echo "$OUTPUT" | grep -q 'alpha'; then
    echo "PASS W10"
else
    echo "FAIL W10"
fi

echo "=== W11: tree --flat ==="
OUTPUT=$(/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "$TESTROOT/Projects/alpha" --flat 2>&1)
echo "$OUTPUT"
if echo "$OUTPUT" | grep -q 'src/main'; then
    echo "PASS W11"
else
    echo "FAIL W11"
fi

echo "=== W12: tree --depth ==="
OUTPUT=$(/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent tree "$TESTROOT/Projects/alpha" --depth 1 --json 2>&1)
echo "$OUTPUT"
if echo "$OUTPUT" | grep -q '"v":1' && echo "$OUTPUT" | grep -q 'src'; then
    echo "PASS W12"
else
    echo "FAIL W12"
fi

rm -rf "$TESTROOT" "$TEST_DATA"
