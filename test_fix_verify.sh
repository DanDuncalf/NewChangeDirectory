#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/v_$$
mkdir -p "$XDG_DATA_HOME/ncd"

# Create metadata file to skip first-run config
echo -n "TkNNRAEAAAAAAAAAAAAA" | base64 -d > "$XDG_DATA_HOME/ncd/ncd.metadata"

TESTROOT=/tmp/vtr_$$
mkdir -p "$TESTROOT/Users/scott/Downloads"

cd "$TESTROOT"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /r. >/dev/null 2>&1

echo "=== Query result ==="
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /agent query Downloads 2>&1

echo ""
echo "=== Exit code: $? ==="

rm -rf "$XDG_DATA_HOME" "$TESTROOT"
