#!/bin/bash
cd /mnt/e/llama/NewChangeDirectory
./ncd_service start > /dev/null 2>&1
sleep 1
SVC_PID=$(pgrep -x NCDService | head -1)
echo "PID: $SVC_PID"
cat /proc/$SVC_PID/comm
echo "status: $(kill -0 $SVC_PID 2>/dev/null && echo alive || echo dead)"
./ncd_service stop > /dev/null 2>&1
