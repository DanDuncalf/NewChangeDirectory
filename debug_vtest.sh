#!/bin/bash
set -e
export NCD_TEST_MODE=1
export NCD_UI_KEYS=ENTER
export NCD_UI_KEYS_STRICT=1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR" && pwd)"
NCD="$PROJECT_ROOT/NewChangeDirectory"

XDG_DATA_HOME="/tmp/ncd_test_xdg_$$"
rm -rf "$XDG_DATA_HOME"
mkdir -p "$XDG_DATA_HOME"
export XDG_DATA_HOME

TESTROOT="/tmp/ncd_test_tree_$$"
rm -rf "$TESTROOT"
mkdir -p "$TESTROOT/Projects/alpha/src/main"
mkdir -p "$TESTROOT/Projects/alpha/src/test"
mkdir -p "$TESTROOT/Projects/alpha/docs"
mkdir -p "$TESTROOT/Projects/beta/src"
mkdir -p "$TESTROOT/Projects/beta/build"
mkdir -p "$TESTROOT/Projects/gamma-2/src4release"
mkdir -p "$TESTROOT/Users/scott/Downloads"
mkdir -p "$TESTROOT/Users/scott/Documents/Reports"
mkdir -p "$TESTROOT/Users/scott/Documents/Spreadsheets"
mkdir -p "$TESTROOT/Users/scott/.hidden_config"
mkdir -p "$TESTROOT/Users/scott/Music"
mkdir -p "$TESTROOT/Users/admin/Library"
mkdir -p "$TESTROOT/Windows/System32/drivers/etc"
mkdir -p "$TESTROOT/Media/Photos2024"
mkdir -p "$TESTROOT/Media/Videos"
mkdir -p "$TESTROOT/Media/Audio"
mkdir -p "$TESTROOT/Deep/L1/L2/L3/L4/L5/L6/L7/L8/L9/L10"
mkdir -p "$TESTROOT/Special Chars/dir with spaces"
mkdir -p "$TESTROOT/Special Chars/dir-with-dashes"
mkdir -p "$TESTROOT/Special Chars/dir_with_underscores"
mkdir -p "$TESTROOT/Special Chars/ALLCAPS"
mkdir -p "$TESTROOT/EmptyDrive"

mkdir -p "$XDG_DATA_HOME/ncd"
printf '\x4E\x43\x4D\x44\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$XDG_DATA_HOME/ncd/ncd.metadata"

cd "$TESTROOT"
"$NCD" -r . 2>&1 | tail -1
echo "=== V1 test ==="
"$NCD" --agent tree "$TESTROOT/Projects" --json --depth 2 2>&1

echo "=== DB files ==="
ls -la "$XDG_DATA_HOME/ncd/"

rm -rf "$TESTROOT" "$XDG_DATA_HOME"
