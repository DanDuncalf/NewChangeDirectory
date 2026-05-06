#!/bin/bash
cd /mnt/e/llama/NewChangeDirectory/test
for i in 1 2 3 4 5; do
  echo "=== Run $i with 4 threads ==="
  ./service_race_tester --duration 5 --threads 4 --agent-only 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context'
  sleep 1
done
