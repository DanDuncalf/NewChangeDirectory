#!/bin/bash
# TUI Key Injection Demo
# 
# This demonstrates how to use the NCD_UI_KEYS and NCD_UI_KEYS_FILE
# environment variables to automate TUI interactions.
#
# Usage: ./tui_key_injection_demo.sh

set -e

NCD="../NewChangeDirectory"
TEST_DIR=".tui_demo_tmp"
CONFIG_FILE="$TEST_DIR/ncd.metadata"

echo "========================================"
echo "NCD TUI Key Injection Demo"
echo "========================================"
echo ""

# Create test directory
mkdir -p "$TEST_DIR"

# Cleanup function
cleanup() {
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

echo "Test 1: Config editor - cancel immediately"
echo "  Keys: ESC"
echo "  Expected: Config editor opens, then closes immediately"
echo ""
NCD_UI_KEYS="ESC" "$NCD" -conf "$CONFIG_FILE" -c || true
echo "  (Press Enter to continue)"
read

echo ""
echo "Test 2: Config editor - toggle hidden and save"
echo "  Keys: SPACE (toggle), ENTER (save)"
echo "  Expected: Hidden dirs option toggled on, config saved"
echo ""
NCD_UI_KEYS="SPACE,ENTER" "$NCD" -conf "$CONFIG_FILE" -c || true
echo ""

# Verify config was created
if [ -f "$CONFIG_FILE" ]; then
    echo "  âœ“ Config file created: $CONFIG_FILE"
    ls -la "$CONFIG_FILE"
else
    echo "  âœ— Config file not found"
fi
echo ""
echo "  (Press Enter to continue)"
read

echo ""
echo "Test 3: Config editor - navigate and toggle multiple options"
echo "  Keys: SPACE, DOWN, SPACE, DOWN, SPACE, ENTER"
echo "  Expected: Toggle hidden, system, and fuzzy match, then save"
echo ""
NCD_UI_KEYS="SPACE,DOWN,SPACE,DOWN,SPACE,ENTER" "$NCD" -conf "$CONFIG_FILE" -c || true
echo "  (Press Enter to continue)"
read

echo ""
echo "Test 4: Using key file instead of environment variable"
echo "  Writing keys to file..."

KEYS_FILE="$TEST_DIR/keys.txt"
echo "DOWN,DOWN,SPACE,ENTER" > "$KEYS_FILE"

echo "  Keys in file: $(cat "$KEYS_FILE")"
echo "  Expected: Navigate to fuzzy match, toggle, save"
echo ""
NCD_UI_KEYS_FILE="$KEYS_FILE" "$NCD" -conf "$CONFIG_FILE" -c || true
echo ""

echo "========================================"
echo "Demo complete!"
echo "========================================"
echo ""
echo "Summary of key injection methods:"
echo ""
echo "1. NCD_UI_KEYS - comma-separated key sequence"
echo "   Example: NCD_UI_KEYS=\"SPACE,DOWN,ENTER\" ncd -c"
echo ""
echo "2. NCD_UI_KEYS_FILE - file containing key sequence"
echo "   Example: NCD_UI_KEYS_FILE=./keys.txt ncd -c"
echo ""
echo "3. NCD_UI_KEYS_STRICT=1 - fail if keys run out (for testing)"
echo "   Example: NCD_UI_KEYS=\"ESC\" NCD_UI_KEYS_STRICT=1 ncd -c"
echo ""
echo "Available keys:"
echo "  - Navigation: UP, DOWN, LEFT, RIGHT, HOME, END, PGUP, PGDN"
echo "  - Actions: ENTER, ESC, SPACE, TAB, BACKSPACE, DELETE"
echo "  - Text input: TEXT:hello (types 'hello')"
echo "  - Symbols: PLUS (+), MINUS (-)"
echo ""
