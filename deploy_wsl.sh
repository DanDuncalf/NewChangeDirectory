#!/bin/bash
cd /mnt/e/llama/NewChangeDirectory
mkdir -p ~/.local/bin
cp NewChangeDirectory NCDService ncd ~/.local/bin/
chmod 755 ~/.local/bin/NewChangeDirectory ~/.local/bin/NCDService ~/.local/bin/ncd
export PATH=$HOME/.local/bin:$PATH
echo "=== Deployed NCD to ~/.local/bin ==="
which ncd
ncd -? 2>&1 | head -10
