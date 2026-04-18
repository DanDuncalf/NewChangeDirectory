#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/test_data_manual
mkdir -p "$XDG_DATA_HOME/ncd"
mkdir -p /tmp/test_manual/abc/def
cd /tmp/test_manual
echo "=== Running ncd -r:. ==="
~/llama/NewChangeDirectory/NewChangeDirectory -r:. 2>&1
echo "Exit code: $?"
echo "=== Checking for database ==="
ls -la "$XDG_DATA_HOME/ncd/"
