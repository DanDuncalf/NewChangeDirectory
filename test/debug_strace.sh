#!/bin/bash
# Run service under strace to see why it dies

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory
# Start service with strace, logging signals and exits
nohup strace -e trace=signal,exit_group -o /tmp/ncd_strace.log ./NCDService > /tmp/ncd_direct.log 2>&1 &
SVC_PID=$!
echo "Service PID: $SVC_PID"
sleep 2
kill -0 $SVC_PID 2>/dev/null && echo "Alive" || echo "DEAD"

cd test
./service_race_tester --duration 5 --threads 4 --agent-only --no-service 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'

sleep 1
echo "=== After race test ==="
kill -0 $SVC_PID 2>/dev/null && echo "Still alive" || echo "DEAD"

echo "=== strace log ==="
cat /tmp/ncd_strace.log | tail -20 || true

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
pkill -9 -x strace 2>/dev/null || true
