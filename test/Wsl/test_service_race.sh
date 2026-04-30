#!/bin/bash
# Service Race Condition Tester - Integration wrapper for WSL/Linux
set -e

cd "$(dirname "$0")/.."

if [ ! -x ./service_race_tester ]; then
    echo "ERROR: service_race_tester not found. Run make service_race_tester first."
    exit 1
fi

echo "Running Service Race Condition Tester..."
./service_race_tester --duration 30
RC=$?

if [ $RC -eq 0 ]; then
    echo "[PASS] Service race test"
    echo "Total: 1"
    echo "Passed: 1"
    echo "Failed: 0"
    echo "Skipped: 0"
    exit 0
else
    echo "[FAIL] Service race test (exit code $RC)"
    echo "Total: 1"
    echo "Passed: 0"
    echo "Failed: 1"
    echo "Skipped: 0"
    exit 1
fi
