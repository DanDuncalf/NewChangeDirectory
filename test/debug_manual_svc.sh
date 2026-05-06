#!/bin/bash
set -e
cd /mnt/e/llama/NewChangeDirectory/test

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

# Start service manually and capture PID
cd /mnt/e/llama/NewChangeDirectory
./NCDService > /tmp/ncd_direct.log 2>&1 &
SVC_PID=$!
echo "Service PID: $SVC_PID"
sleep 1

# Run race test with --no-service
cd test
./service_race_tester --duration 10 --threads 4 --agent-only --no-service 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'

sleep 1
echo "=== Checking service PID $SVC_PID ==="
kill -0 $SVC_PID 2>/dev/null && echo 'Still alive' || echo 'Dead'
ps aux | grep NCDService | grep -v grep || true

echo "=== Service direct log ==="
cat /tmp/ncd_direct.log | tail -30 || true

echo "=== Internal service log ==="
cat ~/.local/share/ncd/ncd_service.log 2>/dev/null | tail -30 || true
