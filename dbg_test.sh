#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/ncd_dbg_env
mkdir -p /tmp/ncd_dbg_env/ncd
echo -n TkNNRAEAAAAAAAAAAAAA | base64 -d > /tmp/ncd_dbg_env/ncd/ncd.metadata
mkdir -p /tmp/ncd_dbg_tree/Projects/alpha/src
cd /tmp/ncd_dbg_tree
/mnt/e/llama/NewChangeDirectory/NewChangeDirectory -r . >/dev/null 2>&1
echo Databases after scan:
ls -la /tmp/ncd_dbg_env/ncd/
