#!/bin/bash
set -x
cd /mnt/e/llama/NewChangeDirectory/test
export XDG_DATA_HOME=/tmp/ncd_agent_test/ncd
mkdir -p $XDG_DATA_HOME
../NewChangeDirectory --agent:query win --json --limit 5 > /tmp/agent_out.txt 2>&1
RC=$?
echo "Exit: $RC"
cat /tmp/agent_out.txt
echo "=== Service log ==="
cat ~/.local/share/ncd/ncd_service.log 2>/dev/null | tail -10 || true
cd /mnt/e/llama/NewChangeDirectory
./ncd_service stop
