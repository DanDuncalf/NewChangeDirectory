#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/ncd_dbg_env2
mkdir -p /tmp/ncd_dbg_env2/ncd
echo -n TkNNRAEAAAAAAAAAAAAA | base64 -d > /tmp/ncd_dbg_env2/ncd/ncd.metadata
mkdir -p /tmp/ncd_dbg_tree2/Projects/alpha/src
cd /tmp/ncd_dbg_tree2
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r . >/dev/null 2>&1
echo Databases after scan:
ls -la /tmp/ncd_dbg_env2/ncd/
/mnt/e/llama/NewChangeDirectory/NCDService start -log2 >/dev/null 2>&1 &
sleep 3
echo Service processes:
pgrep -x NCDService || echo No service
echo Log file:
cat /tmp/ncd_dbg_env2/ncd/ncd_service.log 2>/dev/null || echo No log
/mnt/e/llama/NewChangeDirectory/NCDService stop >/dev/null 2>&1 || true
rm -rf /tmp/ncd_dbg_env2 /tmp/ncd_dbg_tree2
