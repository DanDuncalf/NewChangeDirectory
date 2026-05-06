#!/bin/bash
# Test if service survives with --daemon

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory
# Start with --daemon (forks)
nohup ./NCDService --daemon > /tmp/ncd_daemon.log 2>&1 &
# The parent exits quickly, so we need to find the child
sleep 2
SVC_PID=$(pgrep -x NCDService | head -1)
echo "Service PID: $SVC_PID"
kill -0 $SVC_PID 2>/dev/null && echo "Alive" || echo "DEAD"

cd test
./service_race_tester --duration 10 --threads 8 --agent-only --no-service 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'

sleep 1
echo "=== After race test ==="
kill -0 $SVC_PID 2>/dev/null && echo "Still alive" || echo "DEAD"
ps aux | grep NCDService | grep -v grep || true

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
