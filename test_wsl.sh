#!/bin/bash
export NCD_TEST_MODE=1
export XDG_DATA_HOME=/tmp/test_data_12345
mkdir -p "$XDG_DATA_HOME/ncd"
mkdir -p /tmp/test_tree/abc/def
cd /tmp/test_tree
echo "In: $(pwd)"
echo "XDG_DATA_HOME=$XDG_DATA_HOME"
~/llama/NewChangeDirectory/NewChangeDirectory -r:. 2>&1
echo "Exit: $?"
echo "---"
ls -la "$XDG_DATA_HOME/ncd/"
