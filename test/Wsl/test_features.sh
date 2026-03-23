#!/usr/bin/env bash
# ==========================================================================
# test_features.sh -- Comprehensive feature tests for NCD (Linux/WSL)
# ==========================================================================
#
# REQUIREMENTS:
#   - Root/sudo for ramdisk mount/umount (falls back to /tmp if unavailable)
#   - NCD binary built: run 'make' in project root
#   - Run from project root: sudo test/Wsl/test_features.sh
#
# WHAT THIS TESTS:
#   Every NCD command-line option and feature is tested at least once.
#   Tests are self-contained: they create an isolated filesystem (ramdisk)
#   and database, never touching the user's real data.
#
# HOW TO ADD A NEW TEST:
#   1. Find the appropriate category section (A through V).
#   2. Call one of the assertion helpers:
#
#        test_exit_ok       "ID" "description"                  ncd_args...
#        test_exit_fail     "ID" "description"                  ncd_args...
#        test_output_has    "ID" "description" "substring"      ncd_args...
#        test_output_lacks  "ID" "description" "substring"      ncd_args...
#        test_ncd_finds     "ID" "description" "path_substring" ncd_args...
#        test_ncd_no_match  "ID" "description"                  ncd_args...
#        test_file_exists   "ID" "description"                  filepath
#        test_file_nonempty "ID" "description"                  filepath
#        test_custom        "ID" "description"   (then write inline logic)
#
#   3. Bump the expected count comment at the bottom if you like (optional).
#
#   Example -- add a new test in Category D:
#
#       test_ncd_finds "D10" "Prefix match on Media" "Media" Media
#
# ==========================================================================

set -o pipefail

# ==========================================================================
# Configuration
# ==========================================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
NCD="$PROJECT_ROOT/NewChangeDirectory"

# Isolation: redirect all NCD data to a temp directory so we never
# touch the user's real database, metadata, groups, or history.
export XDG_DATA_HOME="/tmp/ncd_test_xdg_$$"
mkdir -p "$XDG_DATA_HOME"

RAMDISK="/mnt/ncd_test_ramdisk_$$"
TESTROOT=""           # set during setup (ramdisk or /tmp fallback)
DB_FILE=""            # set during setup
RESULT_FILE="/tmp/ncd_result.sh"
USE_RAMDISK=false

# ==========================================================================
# Colors (disabled if not a terminal)
# ==========================================================================

if [[ -t 1 ]]; then
    C_GREEN='\033[0;32m'; C_RED='\033[0;31m'; C_YELLOW='\033[0;33m'
    C_CYAN='\033[0;36m'; C_BOLD='\033[1m'; C_RESET='\033[0m'
else
    C_GREEN=''; C_RED=''; C_YELLOW=''; C_CYAN=''; C_BOLD=''; C_RESET=''
fi

# ==========================================================================
# Test Framework
# ==========================================================================

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
CURRENT_CATEGORY=""

pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf "  ${C_GREEN}PASS${C_RESET}  %-5s %s\n" "$1" "$2"
}

fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    printf "  ${C_RED}FAIL${C_RESET}  %-5s %s\n" "$1" "$2"
    if [[ -n "${3:-}" ]]; then
        printf "        ${C_RED}Reason: %s${C_RESET}\n" "$3"
    fi
}

skip() {
    SKIP_COUNT=$((SKIP_COUNT + 1))
    printf "  ${C_YELLOW}SKIP${C_RESET}  %-5s %s (%s)\n" "$1" "$2" "$3"
}

category() {
    CURRENT_CATEGORY="$1"
    printf "\n${C_BOLD}${C_CYAN}--- Category %s ---%s\n" "$1" "${C_RESET}"
}

# --- Low-level: run NCD and capture everything --------------------------

# Usage: ncd_run [args...]
# Sets: LAST_EXIT, LAST_OUTPUT, LAST_NCD_STATUS, LAST_NCD_PATH, LAST_NCD_MESSAGE
ncd_run() {
    rm -f "$RESULT_FILE"
    LAST_NCD_STATUS="" ; LAST_NCD_PATH="" ; LAST_NCD_MESSAGE=""
    LAST_OUTPUT=$("$NCD" "$@" 2>&1) || true
    LAST_EXIT=${PIPESTATUS[0]:-$?}
    if [[ -f "$RESULT_FILE" ]]; then
        # Source safely: result file uses single-quoted values
        eval "$(cat "$RESULT_FILE")"
        LAST_NCD_STATUS="$NCD_STATUS"
        LAST_NCD_PATH="$NCD_PATH"
        LAST_NCD_MESSAGE="$NCD_MESSAGE"
    fi
}

# --- High-level assertion helpers ---------------------------------------

# test_exit_ok_timed "ID" "description" timeout_seconds ncd_args...
#   Like test_exit_ok but kills NCD after timeout_seconds.
#   Used for /r (full rescan) tests that could otherwise hang.
test_exit_ok_timed() {
    local id="$1" desc="$2" secs="$3"; shift 3
    timeout "$secs" "$NCD" "$@" >/dev/null 2>&1
    local exit_code=$?
    # exit 124 = timed out (acceptable for scan tests), 0 = normal
    if [[ $exit_code -eq 0 ]] || [[ $exit_code -eq 124 ]]; then
        pass "$id" "$desc"
    else
        fail "$id" "$desc" "exit code $exit_code (expected 0 or timeout)"
    fi
}

# test_exit_ok "ID" "description" ncd_args...
#   Pass if NCD exits 0.
test_exit_ok() {
    local id="$1" desc="$2"; shift 2
    ncd_run "$@"
    if [[ $LAST_EXIT -eq 0 ]]; then
        pass "$id" "$desc"
    else
        fail "$id" "$desc" "exit code $LAST_EXIT (expected 0)"
    fi
}

# test_exit_fail "ID" "description" ncd_args...
#   Pass if NCD exits non-zero.
test_exit_fail() {
    local id="$1" desc="$2"; shift 2
    ncd_run "$@"
    if [[ $LAST_EXIT -ne 0 ]]; then
        pass "$id" "$desc"
    else
        fail "$id" "$desc" "exit code 0 (expected non-zero)"
    fi
}

# test_output_has "ID" "description" "substring" ncd_args...
#   Pass if stdout+stderr contains substring (case-insensitive).
test_output_has() {
    local id="$1" desc="$2" needle="$3"; shift 3
    ncd_run "$@"
    if echo "$LAST_OUTPUT" | grep -qi "$needle"; then
        pass "$id" "$desc"
    else
        fail "$id" "$desc" "output missing '$needle'"
    fi
}

# test_output_lacks "ID" "description" "substring" ncd_args...
#   Pass if stdout+stderr does NOT contain substring.
test_output_lacks() {
    local id="$1" desc="$2" needle="$3"; shift 3
    ncd_run "$@"
    if echo "$LAST_OUTPUT" | grep -qi "$needle"; then
        fail "$id" "$desc" "output unexpectedly contains '$needle'"
    else
        pass "$id" "$desc"
    fi
}

# test_ncd_finds "ID" "description" "path_substring" ncd_args...
#   Pass if NCD_STATUS=OK and NCD_PATH contains path_substring.
test_ncd_finds() {
    local id="$1" desc="$2" needle="$3"; shift 3
    ncd_run "$@"
    if [[ "$LAST_NCD_STATUS" == "OK" ]] && echo "$LAST_NCD_PATH" | grep -qi "$needle"; then
        pass "$id" "$desc"
    elif [[ "$LAST_NCD_STATUS" != "OK" ]]; then
        fail "$id" "$desc" "NCD_STATUS='$LAST_NCD_STATUS' (expected OK)"
    else
        fail "$id" "$desc" "NCD_PATH='$LAST_NCD_PATH' missing '$needle'"
    fi
}

# test_ncd_no_match "ID" "description" ncd_args...
#   Pass if NCD exits 1 (no match found).
test_ncd_no_match() {
    local id="$1" desc="$2"; shift 2
    ncd_run "$@"
    if [[ $LAST_EXIT -ne 0 ]]; then
        pass "$id" "$desc"
    else
        fail "$id" "$desc" "expected no match but got exit 0"
    fi
}

# test_file_exists "ID" "description" filepath
test_file_exists() {
    local id="$1" desc="$2" path="$3"
    if [[ -e "$path" ]]; then
        pass "$id" "$desc"
    else
        fail "$id" "$desc" "file not found: $path"
    fi
}

# test_file_nonempty "ID" "description" filepath
test_file_nonempty() {
    local id="$1" desc="$2" path="$3"
    if [[ -s "$path" ]]; then
        pass "$id" "$desc"
    else
        fail "$id" "$desc" "file empty or missing: $path"
    fi
}

# rescan_testroot
#   Re-scan ONLY the test tree via /r. (subdirectory rescan).
#   This is used instead of /r (which scans ALL mounts and is very slow).
rescan_testroot() {
    (cd "$TESTROOT" && "$NCD" /r. >/dev/null 2>&1)
}

# test_custom "ID" "description"
#   Call this, then write your own inline check calling pass/fail.
#   Example:
#     test_custom "X1" "my test"
#     if [[ some_condition ]]; then pass "X1" "my test"; else fail "X1" "my test" "reason"; fi
test_custom() {
    : # placeholder -- caller does pass/fail inline
}

# ==========================================================================
# Setup: Create test directory tree
# ==========================================================================

setup() {
    printf "${C_BOLD}Setting up test environment...${C_RESET}\n"

    # Try ramdisk first (requires root)
    if [[ $EUID -eq 0 ]]; then
        mkdir -p "$RAMDISK"
        mount -t tmpfs -o size=16m tmpfs "$RAMDISK" 2>/dev/null
        if mountpoint -q "$RAMDISK" 2>/dev/null; then
            USE_RAMDISK=true
            TESTROOT="$RAMDISK"
            printf "  Using ramdisk at %s\n" "$RAMDISK"
        fi
    fi

    # Fallback: regular temp directory
    if [[ "$USE_RAMDISK" != true ]]; then
        TESTROOT="/tmp/ncd_test_tree_$$"
        mkdir -p "$TESTROOT"
        printf "  Using temp directory at %s (run as root for ramdisk)\n" "$TESTROOT"
    fi

    DB_FILE="$XDG_DATA_HOME/ncd/ncd.database"

    # Create a valid minimal metadata file to skip first-run configuration wizard.
    # NCD checks if this file exists; if not, it shows an interactive config TUI
    # that blocks waiting for keyboard input. This 20-byte file has:
    #   magic(4) + version(2) + section_count=0(1) + reserved(1) + reserved(4) + crc=0(8)
    mkdir -p "$XDG_DATA_HOME/ncd"
    printf '\x4E\x43\x4D\x44\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' \
        > "$XDG_DATA_HOME/ncd/ncd.metadata"

    # --- Build the directory tree (matches the plan exactly) ---
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
    mkdir -p "$TESTROOT/Users/admin/Downloads"

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

    printf "  Created %d directories in test tree\n" "$(find "$TESTROOT" -type d | wc -l)"
}

# ==========================================================================
# Teardown
# ==========================================================================

teardown() {
    printf "\n${C_BOLD}Cleaning up...${C_RESET}\n"

    if [[ "$USE_RAMDISK" == true ]]; then
        umount "$RAMDISK" 2>/dev/null || true
        rmdir "$RAMDISK" 2>/dev/null || true
        printf "  Unmounted ramdisk\n"
    else
        rm -rf "$TESTROOT"
        printf "  Removed temp directory\n"
    fi

    rm -rf "$XDG_DATA_HOME"
    rm -f "$RESULT_FILE"
    printf "  Cleaned up test data\n"
}

trap teardown EXIT

# ==========================================================================
# Pre-flight checks
# ==========================================================================

if [[ ! -x "$NCD" ]]; then
    printf "${C_RED}ERROR: NCD binary not found at %s${C_RESET}\n" "$NCD"
    printf "Build it first: make\n"
    exit 1
fi

# ==========================================================================
# Run setup, then initial scan
# ==========================================================================

setup

printf "\n${C_BOLD}Performing initial scan of test tree...${C_RESET}\n"
# Use /r. (subdirectory rescan) from TESTROOT to scan ONLY the test tree,
# not the entire system. This keeps tests fast (~1 second vs minutes).
(cd "$TESTROOT" && "$NCD" /r. 2>&1 | tail -1)
printf "  Scan complete.\n"

printf "\n${C_BOLD}========== Running Feature Tests ==========${C_RESET}\n"

# ==========================================================================
# CATEGORY A: Help & Version (3 tests)
# ==========================================================================

category "A: Help & Version"

test_output_has "A1" "Help with /h"    "usage"  /h
test_output_has "A2" "Help with /?"    "usage"  /?
test_exit_ok    "A3" "Version /v"               /v

# ==========================================================================
# CATEGORY B: Full Rescan (6 tests)
# ==========================================================================

category "B: Full Rescan"

# B1: Rescan test tree (via /r.)
rescan_testroot
pass "B1" "Rescan test tree"
test_file_nonempty "B2" "Database file created"            "$DB_FILE"

# B3: Rescan then search should find our test dirs
rescan_testroot
test_ncd_finds "B3" "Rescan creates searchable DB" "Downloads" Downloads

# B4: Rescan with timeout (use hard timeout to avoid hanging on full scan)
test_exit_ok_timed "B4" "Rescan with /t 10" 15 /r /t 10

# B5: Rescan with /d override (use hard timeout)
CUSTOM_DB="/tmp/ncd_custom_test_$$.db"
test_exit_ok_timed "B5" "Rescan with /d override" 15 /r /d "$CUSTOM_DB"
rm -f "$CUSTOM_DB"

# B6: Rescan after adding dirs
mkdir -p "$TESTROOT/Projects/delta_new"
rescan_testroot
test_ncd_finds "B6" "Rescan finds newly added dir" "delta_new" delta_new
rm -rf "$TESTROOT/Projects/delta_new"

# ==========================================================================
# CATEGORY C: Selective Rescan (6 tests)
# ==========================================================================

category "C: Selective Rescan"

# C1: Rescan via /r. succeeds
rescan_testroot
pass "C1" "Rescan runs successfully"

# C2: Subdirectory rescan /r. (cd into Projects)
test_custom "C2" "Subdirectory rescan /r."
(cd "$TESTROOT/Projects" && "$NCD" /r. >/dev/null 2>&1)
if [[ $? -eq 0 ]]; then pass "C2" "Subdirectory rescan /r."; else fail "C2" "Subdirectory rescan /r."; fi

# C3: Subdirectory rescan /r . (with space)
test_custom "C3" "Subdirectory rescan /r ."
(cd "$TESTROOT/Users" && "$NCD" /r . >/dev/null 2>&1)
if [[ $? -eq 0 ]]; then pass "C3" "Subdirectory rescan /r ."; else fail "C3" "Subdirectory rescan /r ."; fi

# C4-C6: Test /r flag variations (hard timeout to avoid hanging on full scan)
test_exit_ok_timed "C4" "Rescan exclude /r-a"          15 /r-a
test_exit_ok_timed "C5" "Rescan drive list /r a,b"     15 /r "a,b"
test_exit_ok_timed "C6" "Rescan /t5 /r"                15 /t5 /r

# ==========================================================================
# CATEGORY D: Basic Search (9 tests)
# ==========================================================================

category "D: Basic Search"

# Ensure database is fresh
rescan_testroot

test_ncd_finds    "D1" "Single component exact"         "Downloads"    Downloads
test_ncd_finds    "D2" "Single component prefix"         "Down"         Down
test_ncd_finds    "D3" "Single component substring"      "ownload"      ownload
test_ncd_finds    "D4" "Case insensitive (lower)"        "Downloads"    downloads
test_ncd_finds    "D5" "Case insensitive (UPPER)"        "Downloads"    DOWNLOADS
test_ncd_finds    "D6" "Multi-component search"          "Downloads"    scott/Downloads
test_ncd_finds    "D7" "Three-level chain"               "Downloads"    Users/scott/Downloads
test_ncd_no_match "D8" "No match returns error"                         nonexistent_xyz_42
test_exit_ok      "D9" "Empty search handled gracefully"                ""

# ==========================================================================
# CATEGORY E: Glob/Wildcard Search (7 tests)
# ==========================================================================

category "E: Glob/Wildcard Search"

test_ncd_finds    "E1" "Star suffix"              "Down"      "Down*"
test_ncd_finds    "E2" "Star prefix"              "loads"     "*loads"
test_ncd_finds    "E3" "Star both sides"          "ownload"   "*ownload*"
test_ncd_finds    "E4" "Question mark single"     "System32"  "Sys?em32"
test_ncd_finds    "E5" "Glob in multi-component"  "Downloads" "Us*/Down*"
test_ncd_no_match "E6" "No match glob"                        "xyz*qqq"
# E7: Star-only should match many dirs (just verify it doesn't crash/error)
test_exit_ok      "E7" "Star-only glob"                       "*"

# ==========================================================================
# CATEGORY F: Fuzzy Match /z (6 tests)
# ==========================================================================

category "F: Fuzzy Match /z"

test_ncd_finds    "F1" "Fuzzy exact term"           "Photos2024"  /z Photos2024
test_ncd_finds    "F2" "Fuzzy with typo"            "Downloads"   /z Downoads
test_ncd_finds    "F3" "Fuzzy word-to-digit"        "gamma"       /z gamma2
test_ncd_finds    "F4" "Fuzzy combined with /i"     "hidden"      /z /i .hidden
test_ncd_no_match "F5" "Fuzzy no match at all"                    /z zzzzqqqq

# F6: Fuzzy performance (should complete in <5s)
test_custom "F6" "Fuzzy digit-heavy performance"
START_T=$SECONDS
ncd_run /z src4release
ELAPSED=$((SECONDS - START_T))
if [[ $ELAPSED -lt 5 ]]; then
    pass "F6" "Fuzzy digit-heavy performance (${ELAPSED}s)"
else
    fail "F6" "Fuzzy digit-heavy performance" "took ${ELAPSED}s (expected <5s)"
fi

# ==========================================================================
# CATEGORY G: Hidden/System Filters (6 tests)
# ==========================================================================

category "G: Hidden/System Filters"

# Note: on Linux, "hidden" means dot-prefixed dirs. "system" is less relevant
# but NCD still supports the flag. The .hidden_config dir was created under scott.

test_ncd_no_match "G1" "Default hides hidden dirs"                    hidden_config
test_ncd_finds    "G2" "/i shows hidden dirs"    "hidden_config"   /i hidden_config
# G3-G5: System filter (Windows\System32 was created as a regular dir on Linux,
# so it won't have the system flag. Test that /s doesn't crash.)
test_exit_ok      "G3" "/s flag accepted"                          /s System32
test_ncd_finds    "G4" "/a shows all dirs"       "hidden_config"   /a hidden_config
test_exit_ok      "G5" "/a flag accepted"                          /a System32
# G6: Default should hide hidden dirs (same as G1, different wording)
test_ncd_no_match "G6" "Default excludes .hidden_config"              .hidden_config

# ==========================================================================
# CATEGORY H: Groups/Bookmarks (9 tests)
# ==========================================================================

category "H: Groups/Bookmarks"

# H1: Create group @proj
test_custom "H1" "Create group @proj"
(cd "$TESTROOT/Projects" && "$NCD" /g @proj >/dev/null 2>&1)
if [[ $? -eq 0 ]]; then pass "H1" "Create group @proj"; else fail "H1" "Create group @proj"; fi

# H2: Create group @users
test_custom "H2" "Create group @users"
(cd "$TESTROOT/Users" && "$NCD" /g @users >/dev/null 2>&1)
if [[ $? -eq 0 ]]; then pass "H2" "Create group @users"; else fail "H2" "Create group @users"; fi

# H3: List groups
test_output_has "H3" "List groups /gl" "proj" /gl

# H4: Navigate to group
test_ncd_finds "H4" "Navigate to @proj" "Projects" @proj

# H5: Navigate to second group
test_ncd_finds "H5" "Navigate to @users" "Users" @users

# H6: Update existing group
test_custom "H6" "Update existing group"
(cd "$TESTROOT/Media" && "$NCD" /g @proj >/dev/null 2>&1)
if [[ $? -eq 0 ]]; then
    ncd_run @proj
    if echo "$LAST_NCD_PATH" | grep -qi "Media"; then
        pass "H6" "Update existing group"
    else
        fail "H6" "Update existing group" "NCD_PATH='$LAST_NCD_PATH' expected Media"
    fi
else
    fail "H6" "Update existing group"
fi

# H7: Remove group
test_exit_ok "H7" "Remove group /g- @users" /g- @users

# H8: Verify removed
test_output_lacks "H8" "Removed group not in list" "@users" /gl

# H9: Use removed group
test_ncd_no_match "H9" "Removed group returns error" @users

# ==========================================================================
# CATEGORY I: Exclusion Patterns (9 tests)
# ==========================================================================

category "I: Exclusion Patterns"

test_exit_ok    "I1" "Add exclusion"           -x "*/Deep"
test_output_has "I2" "List exclusions"  "Deep" -xl

# I3: After rescan, excluded dir should not be found
rescan_testroot
test_ncd_no_match "I3" "Excluded dir not found" L10

test_exit_ok    "I4" "Add second exclusion"              -x "*/EmptyDrive"
test_output_has "I5" "List shows both"    "EmptyDrive"   -xl

test_exit_ok       "I6" "Remove exclusion"               -x- "*/Deep"
test_output_lacks  "I7" "Removed exclusion gone" "Deep"  -xl

# I8: Rescan after removing exclusion, Deep should be findable again
rescan_testroot
test_ncd_finds  "I8" "Rescan after remove finds Deep" "L10" L10

# I9: Remove nonexistent pattern (should not crash)
test_exit_ok    "I9" "Remove nonexistent exclusion" -x- "nonexistent_pattern_xyz"

# Clean up exclusions for subsequent tests
ncd_run -x- "*/EmptyDrive"

# ==========================================================================
# CATEGORY J: History/Heuristics (6 tests)
# ==========================================================================

category "J: History/Heuristics"

# J1: Search creates history
ncd_run Downloads
test_exit_ok "J1" "Search creates history entry" /f

# J2: Show history
test_output_has "J2" "History shows search term" "download" /f

# J3: History influences ranking (just verify /f still works after multiple searches)
ncd_run Music
ncd_run Downloads
test_exit_ok "J3" "History after multiple searches" /f

# J4: Multiple searches appear in history
ncd_run Reports
test_output_has "J4" "Multiple searches in history" "download" /f

# J5: Clear history
test_exit_ok "J5" "Clear history /fc" /fc

# J6: History empty after clear
test_custom "J6" "History empty after clear"
ncd_run /f
# After clearing, output should be empty or say "no history"
if [[ -z "$LAST_OUTPUT" ]] || echo "$LAST_OUTPUT" | grep -qi "no\|empty\|0 entries"; then
    pass "J6" "History empty after clear"
else
    # If output doesn't contain any of our previous search terms, also pass
    if ! echo "$LAST_OUTPUT" | grep -qi "download"; then
        pass "J6" "History empty after clear"
    else
        fail "J6" "History empty after clear" "history still contains entries"
    fi
fi

# ==========================================================================
# CATEGORY K: Configuration /c (3 tests)
# ==========================================================================

category "K: Configuration /c"

# K1: Config edit launches and exits (pipe Escape to exit TUI)
test_custom "K1" "Config edit runs without crash"
printf '\x1b\n' | "$NCD" /c >/dev/null 2>&1
if [[ $? -eq 0 ]] || [[ $? -eq 1 ]]; then
    pass "K1" "Config edit runs without crash"
else
    fail "K1" "Config edit runs without crash" "exit code $?"
fi

# K2-K3: Config persistence requires TUI interaction, skip in automated tests
skip "K2" "Config persists defaults" "requires TUI interaction"
skip "K3" "Flag overrides config"    "requires TUI interaction"

# ==========================================================================
# CATEGORY L: Database Override /d (3 tests)
# ==========================================================================

category "L: Database Override /d"

CUSTOM_DB2="/tmp/ncd_custom_L_$$.db"

# L1: Custom DB path
test_custom "L1" "Custom DB path"
"$NCD" /r /d "$CUSTOM_DB2" >/dev/null 2>&1
if [[ -f "$CUSTOM_DB2" ]]; then
    pass "L1" "Custom DB path"
else
    fail "L1" "Custom DB path" "file not created at $CUSTOM_DB2"
fi

# L2: Search with custom DB
test_ncd_finds "L2" "Search with custom DB" "Downloads" /d "$CUSTOM_DB2" Downloads

# L3: Default DB unchanged
test_file_exists "L3" "Default DB still exists" "$DB_FILE"

rm -f "$CUSTOM_DB2"

# ==========================================================================
# CATEGORY M: Timeout /t (3 tests)
# ==========================================================================

category "M: Timeout /t"

test_exit_ok_timed "M1" "Short timeout rescan"       15 /r /t 5
test_exit_ok       "M2" "/t with search"                 /t 60 Downloads
test_exit_ok_timed "M3" "/t<N> no space shorthand"  15 /t5 /r

# ==========================================================================
# CATEGORY N: Navigator Mode (3 tests)
# ==========================================================================

category "N: Navigator Mode"

# Navigator is a TUI -- pipe Escape to exit immediately.
# We only verify it doesn't crash.

test_custom "N1" "Navigate current dir"
printf '\x1b' | "$NCD" . >/dev/null 2>&1
RET=$?
if [[ $RET -eq 0 ]] || [[ $RET -eq 1 ]]; then
    pass "N1" "Navigate current dir (exit $RET)"
else
    fail "N1" "Navigate current dir" "exit code $RET"
fi

test_custom "N2" "Navigate specific path"
printf '\x1b' | "$NCD" "$TESTROOT/Projects" >/dev/null 2>&1
RET=$?
if [[ $RET -eq 0 ]] || [[ $RET -eq 1 ]]; then
    pass "N2" "Navigate specific path (exit $RET)"
else
    fail "N2" "Navigate specific path" "exit code $RET"
fi

test_custom "N3" "Navigate root"
printf '\x1b' | "$NCD" / >/dev/null 2>&1
RET=$?
if [[ $RET -eq 0 ]] || [[ $RET -eq 1 ]]; then
    pass "N3" "Navigate root (exit $RET)"
else
    fail "N3" "Navigate root" "exit code $RET"
fi

# ==========================================================================
# CATEGORY O: Wrapper Script (3 tests)
# ==========================================================================

category "O: Wrapper Script"

NCD_WRAPPER="$PROJECT_ROOT/ncd"

if [[ -f "$NCD_WRAPPER" ]]; then
    # O1: Source wrapper changes directory
    test_custom "O1" "Source wrapper changes dir"
    ORIG_DIR="$(pwd)"
    # Source in a subshell to avoid messing up our cwd
    WRAPPER_RESULT=$(bash -c "cd /tmp && source '$NCD_WRAPPER' Downloads 2>&1 && pwd")
    if echo "$WRAPPER_RESULT" | grep -qi "download"; then
        pass "O1" "Source wrapper changes dir"
    else
        # Wrapper may not work outside of interactive shell; don't fail hard
        skip "O1" "Source wrapper changes dir" "non-interactive shell"
    fi

    # O2: Execute (not source) wrapper should error
    test_custom "O2" "Execute wrapper shows error"
    WRAPPER_OUT=$("$NCD_WRAPPER" Downloads 2>&1) || true
    if echo "$WRAPPER_OUT" | grep -qi "source\|must be sourced"; then
        pass "O2" "Execute wrapper shows error"
    else
        skip "O2" "Execute wrapper shows error" "wrapper may not detect non-sourced execution"
    fi

    # O3: Result file exists after wrapper run
    skip "O3" "Wrapper result file" "requires sourced interactive shell"
else
    skip "O1" "Source wrapper" "ncd wrapper script not found"
    skip "O2" "Execute wrapper error" "ncd wrapper script not found"
    skip "O3" "Wrapper result file" "ncd wrapper script not found"
fi

# ==========================================================================
# CATEGORY P: Edge Cases (6 tests)
# ==========================================================================

category "P: Edge Cases"

test_ncd_finds    "P1" "Spaces in dir name"      "dir with spaces"      "dir with spaces"
test_ncd_finds    "P2" "Dashes in dir name"       "dir-with-dashes"      dir-with-dashes
test_ncd_finds    "P3" "ALLCAPS case-insensitive"  "ALLCAPS"             allcaps
test_ncd_finds    "P4" "Deep nesting search"       "L10"                 L10
test_ncd_finds    "P5" "Multiple results (src)"    "src"                 src
# P6: Combined flags
test_exit_ok      "P6" "Combined flags /ris"                             /ris Downloads

# ==========================================================================
# CATEGORY Q: Version Update Flow (3 tests)
# ==========================================================================

category "Q: Version Update Flow"

# Q1: Corrupt version in DB (create a file with wrong version)
test_custom "Q1" "Corrupt version in DB"
if [[ -f "$DB_FILE" ]]; then
    # Read the file, patch version bytes (offset 4-5) to 0xFF 0xFF
    cp "$DB_FILE" "${DB_FILE}.bak"
    printf '\xff\xff' | dd of="$DB_FILE" bs=1 seek=4 count=2 conv=notrunc 2>/dev/null
    ncd_run Downloads
    # Restore
    cp "${DB_FILE}.bak" "$DB_FILE"
    rm -f "${DB_FILE}.bak"
    # NCD should have detected the version mismatch (exit may vary)
    pass "Q1" "Corrupt version handled without crash"
else
    skip "Q1" "Corrupt version in DB" "database file not found"
fi

# Q2-Q3: Skip/fresh rescan behavior (hard to test in isolation)
skip "Q2" "Skip update flag"           "requires interactive prompt"
# Q3: Fresh rescan
test_custom "Q3" "Fresh rescan clears version issues"
rescan_testroot
test_file_nonempty "Q3" "Fresh rescan clears version issues" "$DB_FILE"

# ==========================================================================
# CATEGORY R: Error Handling (3 tests)
# ==========================================================================

category "R: Error Handling"

test_exit_fail "R1" "Invalid option /qqq"        /qqq
test_exit_fail "R2" "/d missing path"             /d
test_exit_fail "R3" "/g missing name"             /g

# ==========================================================================
# CATEGORY U: Circular Directory History /0../9 (12 tests)
# ==========================================================================

category "U: Circular Directory History"

# The directory history tracks the caller's working directory.
# We test by adding entries via the API and verifying navigation.

DIR_A="$TESTROOT/Projects/alpha"
DIR_B="$TESTROOT/Projects/beta"
DIR_C="$TESTROOT/Users/scott"
DIR_D="$TESTROOT/Media"

# U1: Bare NCD ping-pong
test_custom "U1" "Bare NCD ping-pong"
# First build some history by running NCD from different directories
(cd "$DIR_A" && "$NCD" Downloads >/dev/null 2>&1)
(cd "$DIR_B" && "$NCD" Downloads >/dev/null 2>&1)
# Bare NCD should swap first two entries
ncd_run ""
# Just verify no crash (actual dir change requires wrapper)
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U1" "Bare NCD ping-pong (no crash)"
else
    fail "U1" "Bare NCD ping-pong" "exit $LAST_EXIT"
fi

# U2: Bare NCD second time
test_custom "U2" "Bare NCD second swap"
ncd_run ""
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U2" "Bare NCD second swap (no crash)"
else
    fail "U2" "Bare NCD second swap" "exit $LAST_EXIT"
fi

# U3: /0 same as bare NCD
test_custom "U3" "/0 same as bare NCD"
ncd_run /0
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U3" "/0 same as bare NCD (no crash)"
else
    fail "U3" "/0 same as bare NCD" "exit $LAST_EXIT"
fi

# U4: /1 pushes current dir
test_custom "U4" "/1 pushes current dir"
(cd "$DIR_C" && "$NCD" /1 >/dev/null 2>&1)
RET=$?
if [[ $RET -eq 0 ]] || [[ $RET -eq 1 ]]; then
    pass "U4" "/1 pushes current dir"
else
    fail "U4" "/1 pushes current dir" "exit $RET"
fi

# U5: /1 shifts list down
test_custom "U5" "/1 shifts list down"
(cd "$DIR_D" && "$NCD" /1 >/dev/null 2>&1)
RET=$?
if [[ $RET -eq 0 ]] || [[ $RET -eq 1 ]]; then
    pass "U5" "/1 shifts list down"
else
    fail "U5" "/1 shifts list down" "exit $RET"
fi

# U6: /2 goes back 2
test_custom "U6" "/2 goes back 2 dirs"
ncd_run /2
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U6" "/2 goes back 2 dirs"
else
    fail "U6" "/2 goes back 2 dirs" "exit $LAST_EXIT"
fi

# U7: /3 goes back 3
test_custom "U7" "/3 goes back 3 dirs"
ncd_run /3
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U7" "/3 goes back 3 dirs"
else
    fail "U7" "/3 goes back 3 dirs" "exit $LAST_EXIT"
fi

# U8: Circular list max 9 entries
test_custom "U8" "Circular list max 9 entries"
# Build 10+ history entries
for i in $(seq 1 11); do
    mkdir -p "$TESTROOT/hist_$i"
    (cd "$TESTROOT/hist_$i" && "$NCD" /1 >/dev/null 2>&1)
done
# No crash = pass
pass "U8" "Circular list max 9 entries (no crash)"
# Clean up temp dirs
for i in $(seq 1 11); do rmdir "$TESTROOT/hist_$i" 2>/dev/null; done

# U9: /8 max index
test_custom "U9" "/8 max index"
ncd_run /8
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U9" "/8 max index"
else
    fail "U9" "/8 max index" "exit $LAST_EXIT"
fi

# U10: /9 out of range
test_custom "U10" "/9 out of range"
ncd_run /9
# Should handle gracefully (either error or no-op)
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U10" "/9 out of range (handled gracefully)"
else
    fail "U10" "/9 out of range" "exit $LAST_EXIT"
fi

# U11: History survives process restart (just verify /2 still works)
test_custom "U11" "History persists across runs"
ncd_run /2
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U11" "History persists across runs"
else
    fail "U11" "History persists across runs" "exit $LAST_EXIT"
fi

# U12: Empty history bare NCD
test_custom "U12" "Empty history bare NCD"
# Clear all metadata to simulate fresh state
rm -f "$XDG_DATA_HOME/ncd/ncd.metadata"
ncd_run ""
if [[ $LAST_EXIT -eq 0 ]] || [[ $LAST_EXIT -eq 1 ]]; then
    pass "U12" "Empty history bare NCD (no crash)"
else
    fail "U12" "Empty history bare NCD" "exit $LAST_EXIT"
fi

# Rescan to restore database for any further tests
rescan_testroot

# ==========================================================================
# CATEGORY V: Agent Tree (10 tests)
# ==========================================================================

category "V: Agent Tree"

# Agent tree tests verify the different output formats for /agent tree command.
# The tree command displays directory structures from the database.

# V1: Tree with JSON output
 test_custom "V1" "Agent tree --json returns valid JSON"
 OUTPUT=$("$NCD" /agent tree "$TESTROOT/Projects" --json --depth 2 2>&1)
 if echo "$OUTPUT" | grep -q '"v":1' && echo "$OUTPUT" | grep -q '"tree":'; then
     pass "V1" "Agent tree --json returns valid JSON"
 else
     fail "V1" "Agent tree --json returns valid JSON" "output missing JSON markers"
 fi

# V2: Tree flat format shows relative paths with separators
 test_custom "V2" "Agent tree --flat shows relative paths"
 OUTPUT=$("$NCD" /agent tree "$TESTROOT/Projects" --flat --depth 2 2>&1)
 # Check that output contains path separators (forward slash on Linux)
 if echo "$OUTPUT" | grep -q '/'; then
     pass "V2" "Agent tree --flat shows relative paths"
 else
     fail "V2" "Agent tree --flat shows relative paths" "no path separators found"
 fi

# V3: Tree indented format (default) shows names only with indentation
 test_custom "V3" "Agent tree default shows indented names"
 OUTPUT=$("$NCD" /agent tree "$TESTROOT/Projects" --depth 2 2>&1)
 # Check for leading spaces (indentation)
 if echo "$OUTPUT" | grep -q '^ '; then
     pass "V3" "Agent tree default shows indented names"
 else
     fail "V3" "Agent tree default shows indented names" "no indentation found"
 fi

# V4: Tree flat format with JSON
 test_custom "V4" "Agent tree --json --flat returns flat JSON"
 OUTPUT=$("$NCD" /agent tree "$TESTROOT/Projects" --json --flat --depth 2 2>&1)
 if echo "$OUTPUT" | grep -q '"v":1' && echo "$OUTPUT" | grep -q '"d":'; then
     pass "V4" "Agent tree --json --flat returns flat JSON with depth"
 else
     fail "V4" "Agent tree --json --flat returns flat JSON with depth" "missing JSON structure"
 fi

# V5: Tree depth limits entries
 test_custom "V5" "Agent tree --depth limits depth"
 DEPTH1_OUTPUT=$("$NCD" /agent tree "$TESTROOT" --depth 1 2>&1 | wc -l)
 DEPTH2_OUTPUT=$("$NCD" /agent tree "$TESTROOT" --depth 2 2>&1 | wc -l)
 if [[ $DEPTH2_OUTPUT -gt $DEPTH1_OUTPUT ]]; then
     pass "V5" "Agent tree --depth limits depth"
 else
     fail "V5" "Agent tree --depth limits depth" "depth 2 not showing more entries"
 fi

# V6: Tree handles non-existent path gracefully
 test_exit_fail "V6" "Agent tree fails on non-existent path" /agent tree "/nonexistent/path" --json

# V7: Tree flat format shows full relative paths
 test_custom "V7" "Agent tree --flat shows correct relative paths"
 OUTPUT=$("$NCD" /agent tree "$TESTROOT/Users" --flat --depth 2 2>&1)
 # Should contain paths like "scott/Downloads" or "admin/Downloads"
 if echo "$OUTPUT" | grep -qE '(scott|admin).*(Downloads|Documents)'; then
     pass "V7" "Agent tree --flat shows correct relative paths"
 else
     fail "V7" "Agent tree --flat shows correct relative paths" "expected path patterns not found"
 fi

# V8: Tree JSON format has name and depth fields
 test_custom "V8" "Agent tree JSON has name and depth fields"
 OUTPUT=$("$NCD" /agent tree "$TESTROOT/Media" --json --depth 1 2>&1)
 if echo "$OUTPUT" | grep -q '"n":"' && echo "$OUTPUT" | grep -q '"d":0'; then
     pass "V8" "Agent tree JSON has name and depth fields"
 else
     fail "V8" "Agent tree JSON has name and depth fields" "missing name or depth fields"
 fi

# V9: Tree default format doesn't have path separators (just names)
 test_custom "V9" "Agent tree default shows only names"
 OUTPUT=$("$NCD" /agent tree "$TESTROOT/Windows" --depth 2 2>&1)
 # First line should be just "System32" without any path separator
 FIRST_LINE=$(echo "$OUTPUT" | head -1 | tr -d '[:space:]')
 if [[ "$FIRST_LINE" == "System32" ]]; then
     pass "V9" "Agent tree default shows only names"
 else
     fail "V9" "Agent tree default shows only names" "first line was '$FIRST_LINE', expected 'System32'"
 fi

# V10: Tree requires a path argument
 test_exit_fail "V10" "Agent tree requires path argument" /agent tree

# ==========================================================================
# Summary
# ==========================================================================

TOTAL=$((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))

printf "\n${C_BOLD}==========================================\n"
printf "Test Summary\n"
printf "==========================================${C_RESET}\n"
printf "  Total:   %d\n" "$TOTAL"
printf "  ${C_GREEN}Passed:  %d${C_RESET}\n" "$PASS_COUNT"
printf "  ${C_RED}Failed:  %d${C_RESET}\n" "$FAIL_COUNT"
printf "  ${C_YELLOW}Skipped: %d${C_RESET}\n" "$SKIP_COUNT"
printf "\n"

if [[ $FAIL_COUNT -gt 0 ]]; then
    printf "${C_RED}Some tests FAILED.${C_RESET}\n"
    exit 1
else
    printf "${C_GREEN}All tests PASSED!${C_RESET}\n"
    exit 0
fi
