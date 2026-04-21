#!/bin/bash
export NCD_TEST_MODE=1
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -agent tree "/nonexistent/path" --json >/dev/null 2>&1
EC=$?
echo "Exit code: $EC"
