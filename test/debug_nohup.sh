#!/bin/bash
# Test if nohup prevents service death when parent exits

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory

# Start service with nohup in a subshell that exits immediately
(
  nohup ./NCDService > /tmp/ncd_nohup.log 2>&1 &
  echo $! > /tmp/ncd_test_pid.txt
)

sleep 1
PID=$(cat /tmp/ncd_test_pid.txt)
echo "Service PID after subshell exit: $PID"
kill -0 $PID 2>/dev/null && echo "Alive" || echo "DEAD"
ps aux | grep NCDService | grep -v grep || true

# Now run race test
cd test
./service_race_tester --duration 5 --threads 4 --agent-only --no-service 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'

sleep 1
echo "=== After race test ==="
kill -0 $PID 2>/dev/null && echo "Still alive" || echo "DEAD"
ps aux | grep NCDService | grep -v grep || true

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
