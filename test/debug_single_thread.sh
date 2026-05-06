#!/bin/bash
cd /mnt/e/llama/NewChangeDirectory/test
for i in 1 2 3 4 5; do
  echo "=== Run $i: single tree/ls thread ==="
  ./service_race_tester --duration 5 --threads 1 --agent-only 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context|T0:'
  sleep 1
done
