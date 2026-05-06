#!/bin/bash
# Start service with logging and run race test

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory
# Start with logging
nohup ./NCDService -log2 > /tmp/ncd_log2.log 2>&1 &
SVC_PID=$!
echo "Service PID: $SVC_PID"
sleep 2
kill -0 $SVC_PID 2>/dev/null && echo "Alive" || echo "DEAD"

cd test
./service_race_tester --duration 5 --threads 4 --agent-only --no-service 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'

sleep 1
echo "=== After race test ==="
kill -0 $SVC_PID 2>/dev/null && echo "Still alive" || echo "DEAD"

echo "=== Service log ==="
cat /tmp/ncd_log2.log | tail -50 || true

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
