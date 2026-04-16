#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/tt_$$
mkdir -p "$XDG_DATA_HOME/ncd"
TESTROOT=/tmp/tr_$$
mkdir -p "$TESTROOT/Users/scott/Downloads"
echo "CWD: $(pwd)"
cd "$TESTROOT"
echo "In: $(pwd)"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory /r.
echo "Scan: $?"
ls -la "$XDG_DATA_HOME/ncd/"
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory Downloads 2>&1
echo "Search: $?"
rm -rf "$XDG_DATA_HOME" "$TESTROOT"
