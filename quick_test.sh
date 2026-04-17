#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/q_$$
mkdir -p "$XDG_DATA_HOME/ncd"
echo -n "TkNNRAEAAAAAAAAAAAAA" | base64 -d > "$XDG_DATA_HOME/ncd/ncd.metadata"
TESTROOT=/tmp/qtr_$$
mkdir -p "$TESTROOT/a/b"
cd "$TESTROOT"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r:. >/dev/null 2>&1
echo "Query:"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory --agent:query b 2>&1
echo "Exit: $?"
rm -rf "$XDG_DATA_HOME" "$TESTROOT"
