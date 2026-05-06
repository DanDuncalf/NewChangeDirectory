#!/bin/bash
# Test if running many parallel agent commands kills the service
set -e

# Clean up and start service
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory
./ncd_service start > /dev/null 2>&1
sleep 1
SVC_PID=$(pgrep -x NCDService | head -1)
echo "Service PID: $SVC_PID"

cd test
export XDG_DATA_HOME=/tmp/ncd_race_test/ncd
mkdir -p $XDG_DATA_HOME

# Run 50 agent queries in parallel
echo "Starting 50 parallel agent queries..."
for i in $(seq 1 50); do
  ../NewChangeDirectory --agent:query win --json --limit 5 > /dev/null 2>&1 &
done

wait
sleep 1

echo "=== After 50 parallel queries ==="
kill -0 $SVC_PID 2>/dev/null && echo "Service still alive" || echo "Service DEAD"
ps aux | grep NCDService | grep -v grep || true

cd /mnt/e/llama/NewChangeDirectory
./ncd_service stop > /dev/null 2>&1
