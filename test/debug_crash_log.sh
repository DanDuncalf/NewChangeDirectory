#!/bin/bash
set -e
cd /mnt/e/llama/NewChangeDirectory/test

# Clean up and run a test that might crash
echo "=== Running race test ==="
./service_race_tester --duration 10 --threads 4 --agent-only 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'

echo ""
echo "=== Service log ==="
LOGFILE="${XDG_RUNTIME_DIR:-/tmp}/ncd_service.log"
if [ -f "$LOGFILE" ]; then
    echo "Log file: $LOGFILE"
    ls -la "$LOGFILE"
    tail -50 "$LOGFILE"
else
    echo "No log file at $LOGFILE"
fi

echo ""
echo "=== Any core dumps? ==="
ls -la /tmp/core* 2>/dev/null || true
ls -la /var/lib/systemd/coredump/* 2>/dev/null || true

echo ""
echo "=== dmesg for segfaults ==="
dmesg | grep -i ncdservice | tail -5 || true
