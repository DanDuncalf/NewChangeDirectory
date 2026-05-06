#!/bin/bash
set -e

# Clean slate
pkill -9 -x NCDService 2>/dev/null || true
rm -f /tmp/ncd_service.pid
sleep 1

cd /mnt/e/llama/NewChangeDirectory/test

# Run with only 1 worker thread (watchdog only essentially, but we need at least 1 worker)
./service_race_tester --duration 5 --threads 1 --agent-only --verbose 2>&1 | tail -30

sleep 1
echo "=== Service processes after test ==="
ps aux | grep NCDService | grep -v grep || true
