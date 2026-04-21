#!/usr/bin/env bash
set -x
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/ncd_debug_env
mkdir -p "$XDG_DATA_HOME/ncd"
echo -n "TkNNRAEAAAAAAAAAAAAA" | base64 -d > "$XDG_DATA_HOME/ncd/ncd.metadata"
mkdir -p /tmp/ncd_debug_tree/Projects/alpha/src
cd /tmp/ncd_debug_tree
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r . >/dev/null 2>&1
echo "After scan, databases:"
ls -la "$XDG_DATA_HOME/ncd/"
/mnt/e/llama/NewChangeDirectory/NCDService start -log2 >/dev/null 2>&1 &
sleep 3
echo "After start, processes:"
pgrep -x NCDService || echo "No NCDService found"
echo "Log file:"
ls -la "$XDG_DATA_HOME/ncd/ncd_service.log" 2>/dev/null || echo "No log"
cat "$XDG_DATA_HOME/ncd/ncd_service.log" 2>/dev/null || echo "Cannot read log"
/mnt/e/llama/NewChangeDirectory/NCDService stop >/dev/null 2>&1 || true
rm -rf /tmp/ncd_debug_env /tmp/ncd_debug_tree
