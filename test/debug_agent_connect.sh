#!/bin/bash
# Check if NewChangeDirectory agent commands connect to the service
set -e

# Clean up and start service
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory
./ncd_service start > /dev/null 2>&1
sleep 1

cd test
export XDG_DATA_HOME=/tmp/ncd_agent_test/ncd
mkdir -p $XDG_DATA_HOME

# Run a single agent query
../NewChangeDirectory --agent:query win --json --limit 5 > /tmp/agent_out.txt 2>&1
echo "Agent query exit code: $?"
cat /tmp/agent_out.txt

# Check service log for connections
echo ""
echo "=== Service log entries ==="
cat ~/.local/share/ncd/ncd_service.log 2>/dev/null | tail -20 || true

./ncd_service stop > /dev/null 2>&1
