#!/bin/bash
for f in /mnt/e/llama/NewChangeDirectory/test/test_*; do
    if [ -f "$f" ] && file "$f" | grep -q ELF; then
        stat -c '%n %y' "$f"
    fi
done | sort
