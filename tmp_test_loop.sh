#!/usr/bin/env bash
cd /mnt/e/llama/NewChangeDirectory
for i in 1 2 3; do
    echo "=== Run $i ==="
    bash test/test_ncd_wsl_with_service.sh > /dev/null 2>&1
    echo "With service: $?"
    bash test/test_ncd_wsl_standalone.sh > /dev/null 2>&1
    echo "Standalone: $?"
done
