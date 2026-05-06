#!/bin/bash
set -e

# Clean up any existing service
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

# Start service manually in daemon mode
cd /mnt/e/llama/NewChangeDirectory
nohup ./NCDService --daemon > /tmp/ncd_service_test.log 2>&1 &
SVC_PID=$!
echo "Service PID: $SVC_PID"
sleep 2
ps aux | grep NCDService | grep -v grep || true

# Run race test without service management
cd test
./service_race_tester --duration 5 --no-service --agent-only 2>&1 | tail -20

sleep 1
echo "=== Service still running? ==="
ps aux | grep NCDService | grep -v grep || true
kill -0 $SVC_PID 2>/dev/null && echo "PID $SVC_PID alive" || echo "PID $SVC_PID dead"
cat /tmp/ncd_service_test.log | tail -30
