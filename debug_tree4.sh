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

DBFILE=$(ls "$TEST_DATA/ncd/"*.database | head -1)
echo "Database: $DBFILE"
python3 /mnt/e/llama/NewChangeDirectory/debug_parse_db.py "$DBFILE"

rm -rf "$TESTROOT" "$TEST_DATA"
