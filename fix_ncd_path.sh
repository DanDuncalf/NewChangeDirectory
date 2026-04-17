#!/bin/bash
# Fix NCD PATH to use new version

echo "=== Fixing NCD PATH ==="

# Check if .bashrc exists
if [[ -f ~/.bashrc ]]; then
    # Check if PATH fix already exists
    if grep -q 'local/bin.*PATH' ~/.bashrc; then
        echo "PATH fix already in .bashrc"
    else
        echo "" >> ~/.bashrc
        echo "# NCD - Put local bin before system bin" >> ~/.bashrc
        echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
        echo "Added PATH fix to .bashrc"
    fi
else
    echo "Creating .bashrc..."
    echo 'export PATH="$HOME/.local/bin:$PATH"' > ~/.bashrc
fi

# Show which version will be used
export PATH="$HOME/.local/bin:$PATH"
echo ""
echo "=== Version that will be used ==="
which NewChangeDirectory
NewChangeDirectory -v 2>&1

echo ""
echo "=== IMPORTANT ==="
echo "Run this command to update your current shell:"
echo "  source ~/.bashrc"
echo ""
echo "Or open a new terminal window."
