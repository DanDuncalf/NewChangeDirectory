#!/bin/bash
set -e

# Clean slate
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

echo '=== Start NCDService directly (no args) ==='
cd /mnt/e/llama/NewChangeDirectory
nohup ./NCDService > /tmp/ncd_direct.log 2>&1 &
PID1=$!
echo "Initial PID: $PID1"
sleep 1
echo '=== Processes after 1 second ==='
ps aux | grep NCDService | grep -v grep || true
sleep 2
echo '=== Processes after 3 seconds ==='
ps aux | grep NCDService | grep -v grep || true
echo '=== Checking initial PID ==='
kill -0 $PID1 2>/dev/null && echo 'PID1 alive' || echo 'PID1 dead'
kill $PID1 2>/dev/null || true
sleep 1
echo '=== Processes after kill ==='
ps aux | grep NCDService | grep -v grep || true
