#!/bin/bash
# Test if service dies at a specific time

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory
nohup ./NCDService > /tmp/ncd_dur.log 2>&1 &
SVC_PID=$!
echo "Service PID: $SVC_PID"
sleep 2
kill -0 $SVC_PID 2>/dev/null && echo "Alive" || echo "DEAD"

cd test
for dur in 5 10 15 20; do
  echo "=== Duration $dur ==="
  ./service_race_tester --duration $dur --threads 8 --agent-only --no-service 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'
  sleep 1
  kill -0 $SVC_PID 2>/dev/null && echo "Service still alive" || echo "Service DEAD"
  if ! kill -0 $SVC_PID 2>/dev/null; then
    # Restart service
    cd /mnt/e/llama/NewChangeDirectory
    nohup ./NCDService > /tmp/ncd_dur.log 2>&1 &
    SVC_PID=$!
    sleep 2
    cd test
  fi
done

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
