#!/bin/bash
# Verify the SIGPIPE fix

cd /mnt/e/llama/NewChangeDirectory/test

echo "=== Running 5 agent-only race tests ==="
for i in 1 2 3 4 5; do
  echo "Run $i:"
  ./service_race_tester --duration 10 --threads 8 --agent-only 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context|result'
  sleep 1
done

echo ""
echo "=== Running 5 full race tests ==="
for i in 1 2 3 4 5; do
  echo "Run $i:"
  ./service_race_tester --duration 10 --threads 8 2>&1 | grep -E 'CRASH|PASS|FAIL|Service alive|Context|result'
  sleep 1
done
