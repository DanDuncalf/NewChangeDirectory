#!/bin/bash
cd /mnt/e/llama/NewChangeDirectory/test
for t in 2 4 6 8; do
  echo "=== Testing with $t threads ==="
  ./service_race_tester --duration 5 --threads $t --agent-only 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive'
  sleep 1
done
