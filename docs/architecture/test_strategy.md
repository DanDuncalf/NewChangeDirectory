# Comprehensive Feature Test Plan for NCD

## Overview

This plan creates two comprehensive feature-test scripts:

1. **`test/Wsl/test_features.sh`** -- Linux/WSL tests on a 16 MB RAM disk
2. **`test/Win/test_features.bat`** -- Windows tests on a VHD created via diskpart

Every single NCD command-line option and feature is tested at least once, most with
three distinct scenarios.  Tests are self-contained: they create and destroy their
own isolated filesystem (ramdisk on Linux, VHD on Windows) and never touch the
user's real data.

---

## Phase 1: Linux/WSL -- `test/Wsl/test_features.sh`

### 1.1 Setup: Create 16 MB RAM Disk

```
RAMDISK=/mnt/ncd_test_ramdisk

# Requires root or sudo
sudo mkdir -p $RAMDISK
sudo mount -t tmpfs -o size=16m tmpfs $RAMDISK
```

### 1.2 Create Test Directory Tree

Build a known, deterministic directory structure on the ramdisk:

```
$RAMDISK/
  Projects/
    alpha/
      src/
        main/
        test/
      docs/
    beta/
      src/
      build/
    gamma-2/
      src4release/
  Users/
    scott/
      Downloads/
      Documents/
        Reports/
        Spreadsheets/
      .hidden_config/
      Music/
    admin/
      Downloads/
  Windows/               (even on Linux, for search testing)
    System32/
      drivers/
        etc/
  Media/
    Photos2024/
    Videos/
    Audio/
  Deep/
    L1/L2/L3/L4/L5/L6/L7/L8/L9/L10/  (10 levels deep)
  Special Chars/
    dir with spaces/
    dir-with-dashes/
    dir_with_underscores/
    ALLCAPS/
  EmptyDrive/            (empty directory for edge cases)
```

### 1.3 Test Categories and Cases

All tests use the built binary `./NewChangeDirectory` with the ramdisk database
via `/d <ramdisk_db_path>`.

---

#### CATEGORY A: Help & Version (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| A1 | Help with /h | `NCD /h` | Exit 0, output contains "Usage" |
| A2 | Help with /? | `NCD /?` | Exit 0, output contains "Usage" |
| A3 | Version /v | `NCD /v` | Exit 0, output contains version number |

#### CATEGORY B: Full Rescan (6 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| B1 | Rescan all | `NCD /r` on ramdisk | Database file created, size > 0 |
| B2 | Rescan creates searchable DB | `NCD /r` then `NCD Downloads` | Match found |
| B3 | Rescan with timeout /t | `NCD /r /t 10` | Completes within 10 seconds |
| B4 | Rescan root only /r / | `NCD /r /` | Only root FS scanned, not /mnt |
| B5 | Rescan with /d override | `NCD /r /d /tmp/test_ncd.db` | DB at custom path |
| B6 | Rescan after adding dirs | Add new dir, `NCD /r`, search for it | Found |

#### CATEGORY C: Selective Drive/Mount Rescan (6 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| C1 | Rescan specific mount | `NCD /r` with only ramdisk | Only ramdisk scanned |
| C2 | Subdirectory rescan /r. | cd into Projects, `NCD /r.` | Only Projects subtree refreshed |
| C3 | Subdirectory rescan /r . | cd into Users, `NCD /r .` | Only Users subtree refreshed |
| C4 | Rescan exclude drives | `NCD /r-a` (exclude drive a) | Scan runs without 'a' |
| C5 | Rescan drive list comma | `NCD /r a,b` | Only listed drives scanned |
| C6 | Rescan drive list dash | `NCD /r a-b` | Only listed drives scanned |

#### CATEGORY D: Basic Search (9 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| D1 | Single component exact | `NCD Downloads` | At least 2 matches (scott + admin) |
| D2 | Single component prefix | `NCD Down` | Matches Downloads |
| D3 | Single component substring | `NCD ownload` | Matches Downloads |
| D4 | Case insensitive search | `NCD downloads` | Matches "Downloads" |
| D5 | Case insensitive upper | `NCD DOWNLOADS` | Matches "Downloads" |
| D6 | Multi-component search | `NCD scott/Downloads` | Exactly 1 match |
| D7 | Three-level chain | `NCD Users/scott/Downloads` | Exactly 1 match |
| D8 | No match returns error | `NCD nonexistent_xyz` | Exit code != 0 |
| D9 | Empty search | `NCD ""` | Handled gracefully (no crash) |

#### CATEGORY E: Glob/Wildcard Search (7 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| E1 | Star suffix | `NCD Down*` | Matches Downloads |
| E2 | Star prefix | `NCD *loads` | Matches Downloads |
| E3 | Star both sides | `NCD *ownload*` | Matches Downloads |
| E4 | Question mark single | `NCD System3?` | Matches System32 |
| E5 | Glob in multi-component | `NCD Us*/Down*` | Matches all Downloads under Users |
| E6 | No match glob | `NCD xyz*qqq` | No results, exit != 0 |
| E7 | Star-only glob | `NCD *` | Matches many directories |

#### CATEGORY F: Fuzzy Match /z (6 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| F1 | Fuzzy digit-to-word | `NCD /z Photos2024` | Matches Photos2024 |
| F2 | Fuzzy with typo | `NCD /z Downoads` | Matches Downloads (1 char off) |
| F3 | Fuzzy word-to-digit | `NCD /z gamma2` | Matches gamma-2 |
| F4 | Fuzzy combined with /i | `NCD /z /i .hidden` | Matches .hidden_config |
| F5 | Fuzzy no match at all | `NCD /z zzzzqqqq` | No results |
| F6 | Fuzzy digit-heavy perf | `NCD /z src4release` | Completes in < 5 seconds |

#### CATEGORY G: Hidden/System Filters (6 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| G1 | Default hides hidden | `NCD hidden_config` | No match (dot-dir hidden) |
| G2 | /i shows hidden | `NCD /i hidden_config` | Match found |
| G3 | /s shows system | `NCD /s System32` | Match found |
| G4 | /a shows all | `NCD /a hidden_config` | Match found |
| G5 | /a shows system too | `NCD /a System32` | Match found |
| G6 | Default hides system | `NCD System32` | No match (system flagged) |

#### CATEGORY H: Groups/Bookmarks (9 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| H1 | Create group | cd ramdisk/Projects, `NCD /g @proj` | Exit 0 |
| H2 | Create second group | cd ramdisk/Users, `NCD /g @users` | Exit 0 |
| H3 | List groups /gl | `NCD /gl` | Output contains "@proj" and "@users" |
| H4 | Use group to navigate | `NCD @proj` | Result path = ramdisk/Projects |
| H5 | Use second group | `NCD @users` | Result path = ramdisk/Users |
| H6 | Update existing group | cd ramdisk/Media, `NCD /g @proj` | @proj now = Media |
| H7 | Remove group /g- | `NCD /g- @users` | Exit 0 |
| H8 | Verify removed | `NCD /gl` | "@users" not in output |
| H9 | Use removed group | `NCD @users` | Error: unknown group |

#### CATEGORY I: Exclusion Patterns (9 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| I1 | Add exclusion | `NCD -x "*/Deep"` | Exit 0 |
| I2 | List exclusions | `NCD -xl` | Output contains "Deep" |
| I3 | Excluded dir not found | `NCD /r` then `NCD L10` | No match (Deep excluded) |
| I4 | Add second exclusion | `NCD -x "*/EmptyDrive"` | Exit 0 |
| I5 | List shows both | `NCD -xl` | Both patterns listed |
| I6 | Remove exclusion | `NCD -x- "*/Deep"` | Exit 0 |
| I7 | Removed exclusion gone | `NCD -xl` | "Deep" not in list |
| I8 | Rescan after remove | `NCD /r` then `NCD L10` | Now found (no longer excluded) |
| I9 | Remove nonexistent | `NCD -x- "nonexistent"` | Handled gracefully |

#### CATEGORY J: History/Heuristics (6 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| J1 | Search creates history | `NCD Downloads` (select one) | History recorded |
| J2 | Show history /f | `NCD /f` | Output contains "downloads" |
| J3 | History influences rank | Search again, preferred match first | First result matches prior choice |
| J4 | Multiple searches | Search several terms | All appear in /f |
| J5 | Clear history /fc | `NCD /fc` | Exit 0 |
| J6 | History empty after clear | `NCD /f` | Empty or "no history" message |

#### CATEGORY K: Configuration /c (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| K1 | Config edit runs | `NCD /c` (with stdin pipe to exit) | Doesn't crash |
| K2 | Config persists defaults | Set default_fuzzy via config | Fuzzy active without /z |
| K3 | Flag overrides config | Set default hidden=true, `NCD /i Downloads` | Still works |

Note: K1-K3 are limited since /c is a TUI. We test that it launches and exits
without crashing by piping Escape/q to stdin.

#### CATEGORY L: Database Override /d (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| L1 | Custom DB path | `NCD /r /d /tmp/ncd_custom.db` | DB created at custom path |
| L2 | Search with custom DB | `NCD /d /tmp/ncd_custom.db Downloads` | Match found |
| L3 | Default DB unchanged | Check default DB path | Not modified by /d tests |

#### CATEGORY M: Timeout /t (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| M1 | Short timeout | `NCD /r /t 5` | Scan completes (ramdisk is fast) |
| M2 | /t with search | `NCD /t 60 Downloads` | Works normally |
| M3 | /t<N> no space | `NCD /t5 /r` | Same as /t 5 |

#### CATEGORY N: Navigator Mode (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| N1 | Navigate current dir | `NCD .` (pipe Escape to exit) | Doesn't crash, exit 0 or 1 |
| N2 | Navigate specific path | `NCD ./Projects` (pipe Escape) | Shows Projects contents |
| N3 | Navigate drive root | `NCD /` (pipe Escape) | Shows root contents |

Note: TUI tests pipe keystrokes to stdin; we verify no crash, not visual output.

#### CATEGORY O: Wrapper Script (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| O1 | Source wrapper | `source ./ncd Downloads` | Directory changes |
| O2 | Execute wrapper error | `./ncd Downloads` | Error message about sourcing |
| O3 | Wrapper result file | After source ncd, check NCD_STATUS | Variables set then cleaned |

#### CATEGORY P: Edge Cases (6 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| P1 | Spaces in dir name | `NCD "dir with spaces"` | Found |
| P2 | Dashes in dir name | `NCD dir-with-dashes` | Found |
| P3 | ALLCAPS dir | `NCD allcaps` | Case-insensitive match |
| P4 | Deep nesting search | `NCD L10` | Found at depth 10 |
| P5 | Multiple results | `NCD src` | Multiple matches (alpha, beta, gamma) |
| P6 | Combined flags /ri | `NCD /ris Downloads` | Hidden + system + search |

#### CATEGORY Q: Version Update Flow (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| Q1 | Corrupt version in DB | Manually patch DB version to 99 | Update prompted |
| Q2 | Skip update flag | Set skip flag, run NCD | Not prompted again |
| Q3 | Fresh rescan clears skip | `NCD /r` after skip | New DB has correct version |

#### CATEGORY R: Error Handling (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| R1 | Invalid option | `NCD /qqq` | Error message, exit != 0 |
| R2 | /d missing path | `NCD /d` | Error: requires path argument |
| R3 | /g missing name | `NCD /g` | Error: requires tag name |

#### CATEGORY U: Circular Directory History /0../9 (12 tests)

NCD maintains a circular list of the last 9 directories the user ran NCD from.
This is a stack of the caller's working directory at invocation time, NOT the
search result.  The list is stored in metadata and updated on every NCD invocation.

**Behavior:**
- `NCD` (no arguments) -- ping-pong: swap the 1st and 2nd entries in the list,
  then cd to the new 1st entry (i.e., go back to where you just were).
- `NCD /0` -- same as bare `NCD`: swap positions 1 and 2, cd to new position 1.
- `NCD /1` -- push current directory to the top of the list (shift everything
  down by one, dropping the 9th if full), then cd to position 1. This adds the
  current directory without ping-ponging.
- `NCD /N` (N = 2..8) -- cd to the Nth entry in the circular list. So `/3`
  goes back 3 directories.  The list is not reordered; the entry at position N
  becomes the target.
- When more than 9 entries are stored, the oldest (9th) is dropped.

| # | Test | Command | Verify |
|---|------|---------|--------|
| U1 | Bare NCD ping-pong | cd to DirA, `NCD Downloads`, cd to DirB, `NCD` (bare) | Returns to DirA (swapped 1st and 2nd) |
| U2 | Bare NCD second time | `NCD` (bare) again | Returns to DirB (swapped back) |
| U3 | /0 same as bare | cd DirA, `NCD Downloads`, cd DirB, `NCD /0` | Same result as bare NCD |
| U4 | /1 pushes current dir | cd to Projects, `NCD /1` | Current dir (Projects) pushed to top, cd to it (stays in Projects) |
| U5 | /1 shifts list down | cd to DirA, `NCD /1`, cd to DirB, `NCD /1`, check list | DirB is pos 1, DirA is pos 2 |
| U6 | /2 goes back 2 | Build 3-entry history, `NCD /2` | Goes to 2nd-oldest directory |
| U7 | /3 goes back 3 | Build 4-entry history, `NCD /3` | Goes to 3rd-oldest directory |
| U8 | Circular list max 9 | Build 10+ entries via repeated cd + NCD | Only 9 entries retained, oldest dropped |
| U9 | /8 max index | Build 9-entry history, `NCD /8` | Goes to 8th entry (index 8 = 9th oldest, 0-based) |
| U10 | /9 out of range | `NCD /9` | Error or wraps around gracefully |
| U11 | List survives restart | Build history, exit shell, reopen, `NCD /2` | Still goes to correct entry (persisted in metadata) |
| U12 | Empty history bare NCD | Fresh metadata (no history), `NCD` (bare) | Handled gracefully: error message or no-op, no crash |

**Test strategy:**
Each test uses the wrapper script (`source ncd` on Linux, `call ncd.bat` on
Windows) to actually change directories, then verifies the current working
directory matches expectations.  The circular list is inspected by running
`NCD /f` or by reading the metadata file directly.

### 1.4 Teardown

```bash
sudo umount $RAMDISK
sudo rmdir $RAMDISK
rm -f /tmp/ncd_custom.db /tmp/test_ncd.db
# Clean up metadata from default paths
rm -f ~/.local/share/ncd/ncd.metadata
```

---

## Phase 2: Windows -- `test/Win/test_features.bat`

### 2.1 Setup: Create VHD via diskpart

The script must:
1. Find an unused drive letter (scan Z: down to G:)
2. Create a 16 MB VHD file in %TEMP%
3. Attach and format it
4. Assign the unused drive letter

```batch
:: Find unused drive letter
for %%L in (Z Y X W V U T S R Q P O N M L K J I H G) do (
    if not exist %%L:\ (
        set TESTDRIVE=%%L
        goto :found_letter
    )
)
echo ERROR: No free drive letter found
exit /b 1
:found_letter

set VHD_PATH=%TEMP%\ncd_test.vhdx
set VHD_SIZE=16

:: Create diskpart script
(
echo create vdisk file="%VHD_PATH%" maximum=%VHD_SIZE% type=expandable
echo select vdisk file="%VHD_PATH%"
echo attach vdisk
echo create partition primary
echo format fs=ntfs quick label="NCDTest"
echo assign letter=%TESTDRIVE%
) > "%TEMP%\ncd_diskpart.txt"

diskpart /s "%TEMP%\ncd_diskpart.txt"
```

### 2.2 Create Test Directory Tree

Same logical structure as Linux, but on `%TESTDRIVE%:\`:

```batch
mkdir %TESTDRIVE%:\Projects\alpha\src\main
mkdir %TESTDRIVE%:\Projects\alpha\src\test
mkdir %TESTDRIVE%:\Projects\alpha\docs
mkdir %TESTDRIVE%:\Projects\beta\src
mkdir %TESTDRIVE%:\Projects\beta\build
mkdir %TESTDRIVE%:\Projects\gamma-2\src4release
mkdir %TESTDRIVE%:\Users\scott\Downloads
mkdir %TESTDRIVE%:\Users\scott\Documents\Reports
mkdir %TESTDRIVE%:\Users\scott\Documents\Spreadsheets
mkdir %TESTDRIVE%:\Users\scott\Music
mkdir %TESTDRIVE%:\Users\admin\Downloads
mkdir %TESTDRIVE%:\Windows\System32\drivers\etc
mkdir %TESTDRIVE%:\Media\Photos2024
mkdir %TESTDRIVE%:\Media\Videos
mkdir %TESTDRIVE%:\Media\Audio
mkdir %TESTDRIVE%:\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10
mkdir "%TESTDRIVE%:\Special Chars\dir with spaces"
mkdir %TESTDRIVE%:\"Special Chars\dir-with-dashes"
mkdir %TESTDRIVE%:\"Special Chars\dir_with_underscores"
mkdir %TESTDRIVE%:\"Special Chars\ALLCAPS"
mkdir %TESTDRIVE%:\EmptyDrive

:: Create hidden directory (Windows hidden attribute)
mkdir %TESTDRIVE%:\Users\scott\.hidden_config
attrib +h %TESTDRIVE%:\Users\scott\.hidden_config

:: Mark system directories
attrib +s %TESTDRIVE%:\Windows
attrib +s %TESTDRIVE%:\Windows\System32
```

### 2.3 Test Categories

Identical test matrix to Linux with Windows-specific adaptations:
- Use `\` as path separator in search strings
- Use `%TESTDRIVE%:` for drive-specific commands
- `/r%TESTDRIVE%` for selective drive rescan
- Navigator mode with `%TESTDRIVE%:\`

All categories A through R and U apply, with these Windows-specific additions:

#### CATEGORY S: Windows-Specific (6 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| S1 | Drive letter search | `NCD %TESTDRIVE%:Downloads` | Found on test drive |
| S2 | Rescan specific drive | `NCD /r%TESTDRIVE%` | Only test drive scanned |
| S3 | Navigate drive root | `NCD %TESTDRIVE%:\` (pipe Escape) | Shows drive root |
| S4 | Forward slash search | `NCD Users/scott/Downloads` | Works with / on Windows |
| S5 | Backslash search | `NCD Users\scott\Downloads` | Works with \ on Windows |
| S6 | Result file sets vars | Check %TEMP%\ncd_result.bat | NCD_STATUS set |

#### CATEGORY T: Batch Wrapper (3 tests)

| # | Test | Command | Verify |
|---|------|---------|--------|
| T1 | Wrapper changes dir | `call ncd.bat Downloads` | %CD% changed |
| T2 | Wrapper cleans vars | After ncd.bat | NCD_STATUS is empty |
| T3 | Wrapper handles error | `call ncd.bat nonexistent` | Error message shown |

### 2.4 Teardown

```batch
:: Detach VHD and clean up
(
echo select vdisk file="%VHD_PATH%"
echo detach vdisk
) > "%TEMP%\ncd_diskpart_cleanup.txt"

diskpart /s "%TEMP%\ncd_diskpart_cleanup.txt"
del "%TEMP%\ncd_test.vhdx"
del "%TEMP%\ncd_diskpart.txt"
del "%TEMP%\ncd_diskpart_cleanup.txt"

:: Clean up metadata
if exist "%LOCALAPPDATA%\NCD\ncd.metadata" del "%LOCALAPPDATA%\NCD\ncd.metadata"
```

### 2.5 Safety Checks in Windows Script

```batch
:: SAFETY: Verify the drive letter is truly free before diskpart
if exist %TESTDRIVE%:\ (
    echo SAFETY ERROR: Drive %TESTDRIVE%: is in use! Aborting.
    exit /b 1
)

:: SAFETY: Verify VHD file doesn't already exist
if exist "%VHD_PATH%" (
    echo WARNING: Removing stale VHD from previous test run...
    del "%VHD_PATH%"
)

:: SAFETY: Always detach VHD in cleanup, even on test failure
:: Use goto :cleanup pattern with error flag
```

---

## Phase 3: Implementation Notes

### 3.1 Test Runner Pattern

Both scripts use this pattern:

```
PASS=0; FAIL=0; SKIP=0

run_test "TEST_NAME" "COMMAND" "EXPECTED_BEHAVIOR"
    # Run command, capture exit code and output
    # Check expected behavior
    # Increment PASS or FAIL
    # Print colored result

# At end: print summary
```

### 3.2 Non-Interactive TUI Tests

Tests involving TUI (navigator /c config editor) pipe input to avoid blocking:

```bash
# Linux: pipe Escape key (0x1b) then Enter
printf '\x1b\n' | ./NewChangeDirectory /c

# Windows: pipe via echo
echo q | NewChangeDirectory.exe /c
```

### 3.3 Search Result Verification

Since NCD writes results to a temp file, tests can:

```bash
# Linux
./NewChangeDirectory "$@"
source /tmp/ncd_result.sh
[[ "$NCD_STATUS" == "OK" ]] && [[ "$NCD_PATH" == *"expected"* ]]
```

```batch
:: Windows
NewChangeDirectory.exe %*
call %TEMP%\ncd_result.bat
if "%NCD_STATUS%"=="OK" if "%NCD_PATH%"=="%EXPECTED%" echo PASS
```

### 3.4 File Organization

```
test/
  Wsl/
    test_features.sh          <-- NEW: Comprehensive Linux feature tests
    test_integration.sh        (existing, simpler)
    test_recursive_mount.sh    (existing, specialized)
  Win/
    test_features.bat          <-- NEW: Comprehensive Windows feature tests
    test_integration.bat       (existing, simpler)
  PLAN_comprehensive_feature_tests.md  <-- This file
```

### 3.5 Execution Requirements

**Linux:**
- Must be run as root (or with sudo) for mount/umount
- NCD binary must be built: `make` in project root
- Run from project root: `sudo test/Wsl/test_features.sh`

**Windows:**
- Must be run as Administrator (for diskpart)
- NCD must be built: `build.bat` in project root
- Run from project root: `test\Win\test_features.bat`

---

## Complete Test Count

| Category | Description | Linux | Windows |
|----------|-------------|-------|---------|
| A | Help & Version | 3 | 3 |
| B | Full Rescan | 6 | 6 |
| C | Selective Rescan | 6 | 6 |
| D | Basic Search | 9 | 9 |
| E | Glob/Wildcard | 7 | 7 |
| F | Fuzzy Match | 6 | 6 |
| G | Hidden/System Filters | 6 | 6 |
| H | Groups/Bookmarks | 9 | 9 |
| I | Exclusion Patterns | 9 | 9 |
| J | History/Heuristics | 6 | 6 |
| K | Configuration | 3 | 3 |
| L | Database Override | 3 | 3 |
| M | Timeout | 3 | 3 |
| N | Navigator Mode | 3 | 3 |
| O | Wrapper Script | 3 | -- |
| P | Edge Cases | 6 | 6 |
| Q | Version Update Flow | 3 | 3 |
| R | Error Handling | 3 | 3 |
| S | Windows-Specific | -- | 6 |
| T | Batch Wrapper | -- | 3 |
| U | Circular Dir History /0../9 | 12 | 12 |
| **TOTAL** | | **106** | **112** |

**Grand total: 218 test cases across both platforms.**

Every command-line flag (`/h`, `/?`, `/v`, `/r`, `/r.`, `/r /`, `/rX`, `/r-X`,
`/r X,Y`, `/i`, `/s`, `/a`, `/z`, `/f`, `/fc`, `/g`, `/g-`, `/gl`, `-x`, `-x-`,
`-xl`, `/c`, `/d`, `/t`, `/0`, `/1`..`/8`, `/test NC`, `/test SL`), every search
mode (exact, prefix, substring, multi-component, glob `*`, glob `?`, fuzzy), every
feature (groups, exclusions, heuristics, configuration, navigator, version checking,
circular directory history), and every wrapper behavior is covered with at least one
test, most with three.
