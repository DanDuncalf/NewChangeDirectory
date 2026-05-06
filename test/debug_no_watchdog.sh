#!/bin/bash
# Test if service dies without watchdog

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory
nohup ./NCDService > /tmp/ncd_nohup2.log 2>&1 &
SVC_PID=$!
echo "Service PID: $SVC_PID"
sleep 1
kill -0 $SVC_PID 2>/dev/null && echo "Alive" || echo "DEAD"

cd test

# Run a modified race test: agent-only with a custom script that doesn't ping the service
# We'll just run 100 agent commands in parallel and check service status
export XDG_DATA_HOME=/tmp/ncd_race_test/ncd
mkdir -p $XDG_DATA_HOME

echo "Running agent commands..."
for i in $(seq 1 100); do
  ../NewChangeDirectory --agent:query win --json --limit 5 > /dev/null 2>&1 &
done
wait

echo "=== After 100 queries ==="
kill -0 $SVC_PID 2>/dev/null && echo "Still alive" || echo "DEAD"

# Now run tree/ls commands
for i in $(seq 1 50); do
  ../NewChangeDirectory --agent:tree /tmp/ncd_race_test --json --depth 2 > /dev/null 2>&1 &
done
wait

echo "=== After 50 tree commands ==="
kill -0 $SVC_PID 2>/dev/null && echo "Still alive" || echo "DEAD"

# Now run mkdir/rmdir commands
for i in $(seq 1 50); do
  ../NewChangeDirectory --agent:mkdir /tmp/ncd_race_test/testdir_$i --json > /dev/null 2>&1 &
done
wait

echo "=== After 50 mkdir commands ==="
kill -0 $SVC_PID 2>/dev/null && echo "Still alive" || echo "DEAD"

# Clean up
pkill -9 -x NCDService 2>/dev/null || true
rm -rf /tmp/ncd_race_test
