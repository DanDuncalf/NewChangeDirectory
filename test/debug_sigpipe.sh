#!/bin/bash
# Test SIGPIPE hypothesis by ignoring SIGPIPE in the service

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory

# Start service with SIGPIPE ignored using a wrapper
bash -c '
  trap "" SIGPIPE
  exec ./NCDService
' > /tmp/ncd_sigpipe.log 2>&1 &
SVC_PID=$!
echo "Service PID: $SVC_PID"
sleep 2
kill -0 $SVC_PID 2>/dev/null && echo "Alive" || echo "DEAD"

cd test
./service_race_tester --duration 10 --threads 8 --agent-only --no-service 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'

sleep 1
echo "=== After race test ==="
kill -0 $SVC_PID 2>/dev/null && echo "Still alive" || echo "DEAD"
ps aux | grep NCDService | grep -v grep || true

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
