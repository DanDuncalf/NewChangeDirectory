#!/bin/bash
cd /mnt/e/llama/NewChangeDirectory
./NCDService stop 2>/dev/null
sleep 1
./NCDService start &
sleep 2
for pid in $(ps -eo pid,comm | grep NCDService | grep -v grep | awk '{print $1}'); do
    echo "PID: $pid, COMM: $(cat /proc/$pid/comm)"
done
./NCDService stop
