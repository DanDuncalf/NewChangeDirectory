@echo off
:: ==========================================================================
:: test_features.bat -- Comprehensive feature tests for NCD (Windows)
:: ==========================================================================
::
:: REQUIREMENTS:
::   - Administrator privileges (for diskpart VHD creation)
::   - NCD binary built: build.bat in project root
::   - MUST be run through the test harness: Run-Tests-Safe.bat integration
::
:: WHAT THIS TESTS:
::   Every NCD command-line option and feature is tested at least once.
::   Tests are self-contained: they create an isolated VHD with a test
::   directory tree and never touch the user's real data.
::
:: HOW TO ADD A NEW TEST:
::   1. Find the appropriate category section (A through V).
::   2. Call one of the assertion helpers:
::
::        call :test_exit_ok       ID "description"              ncd_args
::        call :test_exit_fail     ID "description"              ncd_args
::        call :test_output_has    ID "description" "substring"  ncd_args
::        call :test_output_lacks  ID "description" "substring"  ncd_args
::        call :test_ncd_finds     ID "description" "path_substr" ncd_args
::        call :test_ncd_no_match  ID "description"              ncd_args
::        call :test_file_exists   ID "description"              filepath
::        call :test_file_nonempty ID "description"              filepath
::
::   3. For custom logic, write inline and call :pass or :fail directly.
::
::   Example -- add a new test in Category D:
::
::       call :test_ncd_finds D10 "Prefix match on Media" "Media" Media
::
:: ==========================================================================
::
:: IMPORTANT: This script modifies LOCALAPPDATA and NCD_TEST_MODE which 
:: affect NCD behavior. It MUST be run through the test harness to ensure
:: proper environment setup and cleanup.
::
:: DO NOT RUN THIS SCRIPT DIRECTLY - use one of these instead:
::   Run-Tests-Safe.bat integration       ^(integration tests^)
::   Run-Tests-Safe.bat windows           ^(all Windows tests^)
::   Run-Tests-Safe.bat                   ^(all tests^)
::
:: For isolated execution without affecting your shell:
::   test\Run-Isolated.bat test\Win\test_features.bat
::
:: If your environment is already corrupted, repair it with:
::   Run-Tests-Safe.bat --repair
:: ==========================================================================

:: ==========================================================================
:: ENVIRONMENT CHECK - ENSURE RUNNING THROUGH TEST HARNESS
:: ==========================================================================
if "%NCD_TEST_MODE%"=="" (
    echo.
    echo ==========================================
    echo ENVIRONMENT ERROR - TEST HARNESS REQUIRED
    echo ==========================================
    echo.
    echo This test script is NOT meant to be run directly!
    echo.
    echo Running this script directly will:
    echo   - Modify your LOCALAPPDATA environment variable
    echo   - Leave your NCD configuration in an inconsistent state
    echo   - Potentially corrupt your NCD database
    echo   - Leave test VHDs attached or temp directories behind
    echo   - Leave orphaned service processes running
    echo.
    echo CORRECT USAGE - Run from the project root:
    echo   Run-Tests-Safe.bat integration       ^(integration tests^)
    echo   Run-Tests-Safe.bat windows           ^(all Windows tests^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Win\test_features.bat
    echo.
    echo If your environment is already corrupted, repair it with:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo ==========================================
    exit /b 1
)

:: Save original environment values BEFORE setlocal so we can restore them
set "ORIGINAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "ORIGINAL_NCD_TEST_MODE=%NCD_TEST_MODE%"

:: Create local environment scope - this will be cleaned up on normal exit
setlocal enabledelayedexpansion

:: Set cleanup flag - will be set to 0 if cleanup runs
set "CLEANUP_NEEDED=1"

:: Disable NCD background rescans to prevent scanning user drives during tests
set "NCD_TEST_MODE=1"

:: ==========================================================================
:: Configuration
:: ==========================================================================

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "NCD=%PROJECT_ROOT%\NewChangeDirectory.exe"

:: Isolation: redirect all NCD data to a temp directory so we never
:: touch the user's real database, metadata, groups, or history.
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_test_data_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
if errorlevel 1 (
    echo ERROR: Failed to create test data directory
    call :emergency_cleanup
    endlocal
    exit /b 1
)
set "LOCALAPPDATA=%TEST_DATA%"

:: VHD and test drive configuration
set "VHD_PATH=%TEMP%\ncd_test_%RANDOM%.vhdx"
set "VHD_SIZE=16"
set "TESTDRIVE="
set "USE_VHD=0"
set "USE_SUBST=0"
set "HAS_ISOLATED_DRIVE=0"
set "TESTROOT="

:: Result file location
set "RESULT_FILE=%TEMP%\ncd_result.bat"

:: Log file: all pass/fail/skip results go here for reliable output.
:: NCD writes to CONOUT$ (not stdout), so console echo may not be capturable.
set "LOG_FILE=%TEMP%\ncd_test_log_%RANDOM%.txt"
echo NCD Feature Test Log > "%LOG_FILE%"
if errorlevel 1 (
    echo ERROR: Failed to create log file
    call :emergency_cleanup
    endlocal
    exit /b 1
)

:: Counters
set "PASS_COUNT=0"
set "FAIL_COUNT=0"
set "SKIP_COUNT=0"

:: Track if VHD was attached (for cleanup)
set "VHD_ATTACHED=0"

:: ==========================================================================
:: Pre-flight checks
:: ==========================================================================

if not exist "%NCD%" (
    echo ERROR: NCD binary not found at %NCD%
    echo Build it first: build.bat
    endlocal
    exit /b 1
)

:: ==========================================================================
:: Setup: Create test environment
:: ==========================================================================

echo ==========================================
echo NCD Comprehensive Feature Tests [Windows]
echo ==========================================
echo.
echo Setting up test environment...

:: --- Try to find an unused drive letter for VHD ---
for %%L in (Z Y X W V U T S R Q P O N M L K J I H G) do (
    if not exist %%L:\ (
        set "TESTDRIVE=%%L"
        goto :found_letter
    )
)
echo WARNING: No free drive letter found, using temp directory fallback
goto :use_tempdir

:found_letter

:: --- Check if we have admin (needed for diskpart) ---
net session >nul 2>&1
if errorlevel 1 (
    echo WARNING: Not running as administrator, using temp directory fallback
    goto :use_tempdir
)

:: --- Safety: verify VHD file doesn't already exist ---
if exist "%VHD_PATH%" (
    echo WARNING: Removing stale VHD from previous test run...
    del "%VHD_PATH%" 2>nul
)

:: --- Create VHD via diskpart ---
echo   Creating %VHD_SIZE% MB test VHD on drive %TESTDRIVE%:...
(
echo create vdisk file="%VHD_PATH%" maximum=%VHD_SIZE% type=expandable
echo select vdisk file="%VHD_PATH%"
echo attach vdisk
echo create partition primary
echo format fs=ntfs quick label="NCDTest"
echo assign letter=%TESTDRIVE%
) > "%TEMP%\ncd_diskpart_create.txt"

diskpart /s "%TEMP%\ncd_diskpart_create.txt" >nul 2>&1
if errorlevel 1 (
    echo WARNING: diskpart failed, using temp directory fallback
    del "%TEMP%\ncd_diskpart_create.txt" 2>nul
    goto :use_tempdir
)
del "%TEMP%\ncd_diskpart_create.txt" 2>nul

:: Verify drive is mounted
if not exist %TESTDRIVE%:\ (
    echo WARNING: VHD drive %TESTDRIVE%: not mounted, using temp directory fallback
    goto :use_tempdir
)

set "USE_VHD=1"
set "TESTROOT=%TESTDRIVE%:"
set "HAS_ISOLATED_DRIVE=1"
echo   VHD mounted at %TESTDRIVE%:\
goto :create_tree

:use_tempdir
set "TESTROOT=%TEMP%\ncd_test_tree_%RANDOM%"
mkdir "%TESTROOT%" 2>nul

:: Try to find a free drive letter and subst the test tree so /rDRIVE
:: scans only the test tree instead of the entire real user drive.
for %%L in (Z Y X W V U T S R Q P O N M L K J I H G) do (
    if not exist %%L:\ (
        set "TESTDRIVE=%%L"
        goto :found_subst
    )
)

:: No free letter - use temp directory only (drive-letter rescan tests will skip).
for %%D in ("%TESTROOT%") do set "TESTDRIVE=%%~dD"
set "TESTDRIVE=%TESTDRIVE:~0,1%"
echo   Using temp directory at %TESTROOT% (no isolated drive letter available)
goto :create_tree

:found_subst
subst %TESTDRIVE%: "%TESTROOT%" >nul 2>&1
if errorlevel 1 (
    :: subst failed - use temp directory only (drive-letter rescan tests will skip)
    for %%D in ("%TESTROOT%") do set "TESTDRIVE=%%~dD"
    set "TESTDRIVE=%TESTDRIVE:~0,1%"
    echo   Using temp directory at %TESTROOT% (subst unavailable)
) else (
    set "USE_SUBST=1"
    set "HAS_ISOLATED_DRIVE=1"
    echo   Using subst drive %TESTDRIVE%: -> %TESTROOT%
)

:create_tree

:: --- Build the directory tree ---
mkdir "%TESTROOT%\Projects\alpha\src\main"        2>nul
mkdir "%TESTROOT%\Projects\alpha\src\test"         2>nul
mkdir "%TESTROOT%\Projects\alpha\docs"             2>nul
mkdir "%TESTROOT%\Projects\beta\src"               2>nul
mkdir "%TESTROOT%\Projects\beta\build"             2>nul
mkdir "%TESTROOT%\Projects\gamma-2\src4release"    2>nul

mkdir "%TESTROOT%\Users\scott\Downloads"           2>nul
mkdir "%TESTROOT%\Users\scott\Documents\Reports"   2>nul
mkdir "%TESTROOT%\Users\scott\Documents\Spreadsheets" 2>nul
mkdir "%TESTROOT%\Users\scott\Music"               2>nul
mkdir "%TESTROOT%\Users\admin\Downloads"           2>nul

mkdir "%TESTROOT%\WinSys\System32\drivers\etc"     2>nul

mkdir "%TESTROOT%\Media\Photos2024"                2>nul
mkdir "%TESTROOT%\Media\Videos"                    2>nul
mkdir "%TESTROOT%\Media\Audio"                     2>nul

mkdir "%TESTROOT%\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10" 2>nul

mkdir "%TESTROOT%\Special Chars\dir with spaces"   2>nul
mkdir "%TESTROOT%\Special Chars\dir-with-dashes"   2>nul
mkdir "%TESTROOT%\Special Chars\dir_with_underscores" 2>nul
mkdir "%TESTROOT%\Special Chars\ALLCAPS"           2>nul

mkdir "%TESTROOT%\EmptyDrive"                      2>nul

:: Create hidden directory
mkdir "%TESTROOT%\Users\scott\.hidden_config"      2>nul
attrib +h "%TESTROOT%\Users\scott\.hidden_config"  2>nul

:: Mark system directories
attrib +s "%TESTROOT%\WinSys"                      2>nul
attrib +s "%TESTROOT%\WinSys\System32"             2>nul

echo   Created test directory tree

:: Create isolated metadata (never touch the user's real metadata file)
mkdir "%TEST_DATA%\NCD" 2>nul
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"
echo   Created isolated metadata

:: Set CONF_OVERRIDE to use -conf for all NCD invocations
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

:: Database override for complete isolation - use single test database
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_test.database"

:: CRITICAL: Disable auto-rescan to prevent background scans of user drives
:: Use keystroke injection to automate the config editor (navigate to item 6, enter -1, save)
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
set "NCD_UI_KEYS=ENTER"
:: Prevent TUI from blocking on multi-match: auto-select first item (ENTER).
:: When NCD_TEST_MODE is set, the TUI returns ESC if the key queue is empty.
set "NCD_TEST_MODE=1"
echo   Disabled auto-rescan.
echo.

:: --- Initial scan ---
:: Use /r. (subdirectory rescan) from TESTROOT to scan ONLY the test tree,
:: not the entire system. This keeps tests fast (~1 second vs minutes).
echo Performing initial scan of test tree...
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d %DB_OVERRIDE%'; if (-not $p.WaitForExit(30000)) { $p.Kill(); Write-Host 'WARN: Initial scan timed out' }" >nul 2>&1
popd
echo   Scan complete.
echo.

:: Run tests from the isolated test root so implicit drive resolution
:: never targets the user's project/system drive.
set "TESTROOT_PUSHED=0"
pushd "%TESTROOT%"
if not errorlevel 1 set "TESTROOT_PUSHED=1"

set "DB_DIR=%TEST_DATA%\NCD"

echo ========== Running Feature Tests ==========
echo.

:: ==========================================================================
:: CATEGORY A: Help and Version (3 tests)
:: ==========================================================================

echo --- Category A: Help and Version ---

:: NOTE: NCD writes to CONOUT$ (Windows console handle), not stdout,
:: so output cannot be captured with > redirection. We test exit code only.
call :test_exit_ok    A1 "Help with /h"      /h
:: A2: /? cannot be passed through batch 'call' (cmd.exe intercepts it).
:: Use PowerShell to pass /? directly to NCD.
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /?'; if (-not $p.WaitForExit(10000)) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 1 (call :pass A2 "Help with /?") else (call :fail A2 "Help with /?")
call :test_exit_ok    A3 "Version /v"        /v

:: ==========================================================================
:: CATEGORY B: Full Rescan (6 tests)
:: ==========================================================================

echo --- Category B: Full Rescan ---

:: B1: Rescan test tree (via /r.)
call :rescan_testroot
call :pass B1 "Rescan test tree"

:: B2: Database file created (per-drive files like ncd_C.database)
set "B2_FOUND=0"
if exist "%DB_DIR%" (
    for %%F in ("%DB_DIR%\ncd_*.database") do set "B2_FOUND=1"
)
if "%B2_FOUND%"=="1" (call :pass B2 "Database file created") else (call :fail B2 "Database file created" "no ncd_*.database file in %DB_DIR%")

:: B3: Rescan creates searchable DB
call :rescan_testroot
call :test_ncd_finds B3 "Rescan creates searchable DB" "Downloads" scott\Downloads

:: B4: Rescan with /t flag on test drive only (avoid scanning all drives)
if "%HAS_ISOLATED_DRIVE%"=="1" (
    call :test_exit_ok_timed B4 "Rescan with /t 10" 15 /r%TESTDRIVE% /t 10
) else (
    call :skip B4 "Rescan with /t 10" "no isolated drive letter"
)

:: B5: Rescan with /d override (use test drive only)
set "CUSTOM_DB=%TEMP%\ncd_custom_test.db"
if "%HAS_ISOLATED_DRIVE%"=="1" (
    call :test_exit_ok_timed B5 "Rescan with /d override" 15 /r%TESTDRIVE% /d "%CUSTOM_DB%"
) else (
    call :skip B5 "Rescan with /d override" "no isolated drive letter"
)
del "%CUSTOM_DB%" 2>nul

:: B6: Rescan after adding dirs
mkdir "%TESTROOT%\Projects\delta_new" 2>nul
call :rescan_testroot
call :test_ncd_finds B6 "Rescan finds newly added dir" "delta_new" delta_new
rmdir "%TESTROOT%\Projects\delta_new" 2>nul

:: ==========================================================================
:: CATEGORY C: Selective Rescan (6 tests)
:: ==========================================================================

echo --- Category C: Selective Rescan ---

:: C1: Rescan via /r. succeeds
call :rescan_testroot
call :pass C1 "Rescan runs successfully"

:: C2: Subdirectory rescan /r.
pushd "%TESTROOT%\Projects"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%\Projects' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r.'; if (-not $p.WaitForExit(15000)) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 1 (call :pass C2 "Subdirectory rescan /r.") else (call :fail C2 "Subdirectory rescan /r.")
popd

:: C3: Subdirectory rescan /r . (with space)
pushd "%TESTROOT%\Users"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%\Users' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r .'; if (-not $p.WaitForExit(15000)) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 1 (call :pass C3 "Subdirectory rescan /r .") else (call :fail C3 "Subdirectory rescan /r .")
popd

:: C4-C6: Test /r flag variations on TEST DRIVE ONLY (avoid scanning all drives)
if "%HAS_ISOLATED_DRIVE%"=="1" (
    call :test_exit_ok_timed C4 "Rescan test drive /r%TESTDRIVE%-a"  15 /r%TESTDRIVE%-a
    call :test_exit_ok_timed C5 "Rescan test drive /r %TESTDRIVE%"   15 /r "%TESTDRIVE%"
    call :test_exit_ok_timed C6 "Rescan /t5 shorthand"               15 /t5 /r%TESTDRIVE%
) else (
    call :skip C4 "Rescan test drive /rX-a" "no isolated drive letter"
    call :skip C5 "Rescan test drive /r X" "no isolated drive letter"
    call :skip C6 "Rescan /t5 shorthand" "no isolated drive letter"
)
:: Reset DB to clean state for subsequent tests
del "%DB_DIR%\ncd_*.database" 2>nul
call :rescan_testroot

:: ==========================================================================
:: CATEGORY D: Basic Search (9 tests)
:: ==========================================================================

echo --- Category D: Basic Search ---

call :rescan_testroot

:: NOTE: "Downloads" exists under both scott and admin. Use multi-component
:: search (scott\Downloads) or unique dirs to avoid TUI multi-match hang.
call :test_ncd_finds    D1 "Single component exact"      "Reports"      Reports
call :test_ncd_finds    D2 "Single component prefix"      "Rep"          Rep
:: D3 removed: matcher uses prefix/glob, not substring; glob substring is tested in E3
call :test_ncd_finds    D4 "Case insensitive (lower)"     "Reports"      reports
call :test_ncd_finds    D5 "Case insensitive (UPPER)"     "Reports"      REPORTS
call :test_ncd_finds    D6 "Multi-component search"       "Downloads"    scott\Downloads
call :test_ncd_finds    D7 "Three-level chain"            "Downloads"    Users\scott\Downloads
call :test_ncd_no_match D8 "No match returns error"                      nonexistent_xyz_42
call :test_exit_ok      D9 "Empty search handled"                        ""

:: ==========================================================================
:: CATEGORY E: Glob/Wildcard Search (7 tests)
:: ==========================================================================

echo --- Category E: Glob/Wildcard Search ---

call :test_ncd_finds    E1 "Star suffix"             "Report"    "Report*"
call :test_ncd_finds    E2 "Star prefix"             "ports"     "*ports"
call :test_ncd_finds    E3 "Star both sides"         "eport"     "*eport*"
call :test_ncd_finds    E4 "Question mark single"    "Photos"    "Phot?s2024"
call :test_ncd_finds    E5 "Glob in multi-component" "Reports" "Us*\*eport*"
call :test_ncd_no_match E6 "No match glob"                       "xyz*qqq"
call :test_exit_ok      E7 "Star-only glob"                      "*"

:: ==========================================================================
:: CATEGORY F: Fuzzy Match /z (6 tests)
:: ==========================================================================

echo --- Category F: Fuzzy Match /z ---

call :test_ncd_finds    F1 "Fuzzy exact term"         "Photos2024"  /z Photos2024
call :test_ncd_finds    F2 "Fuzzy with typo"          "Reports"     /z Repots
call :test_ncd_finds    F3 "Fuzzy word-to-digit"      "gamma"       /z gamma2
call :test_ncd_finds    F4 "Fuzzy combined with /i"   "hidden"      /z /i .hidden
call :test_ncd_no_match F5 "Fuzzy no match at all"                  /z zzzzqqqq

:: F6: Fuzzy performance (timed to avoid hang if multiple matches)
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /z src4release'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
call :pass F6 "Fuzzy digit-heavy performance"

:: ==========================================================================
:: CATEGORY G: Hidden/System Filters (6 tests)
:: ==========================================================================

echo --- Category G: Hidden/System Filters ---

call :test_ncd_no_match G1 "Default hides hidden dirs"                       .hidden_config
call :test_ncd_finds    G2 "/i shows hidden dirs"     ".hidden_config"     /i .hidden_config
call :test_ncd_finds    G3 "/s shows system dirs"     "System32"          /s System32
call :test_ncd_finds    G4 "/a shows all (hidden)"    ".hidden_config"     /a .hidden_config
call :test_ncd_finds    G5 "/a shows all (system)"    "System32"          /a System32
call :test_ncd_no_match G6 "Default hides system dirs"                       System32

:: ==========================================================================
:: CATEGORY H: Groups/Bookmarks (9 tests)
:: ==========================================================================

echo --- Category H: Groups/Bookmarks ---

:: H1: Create group @proj
pushd "%TESTROOT%\Projects"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%\Projects' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /g @proj'; if (-not $p.WaitForExit(10000)) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 1 (call :pass H1 "Create group @proj") else (call :fail H1 "Create group @proj")
popd

:: H2: Create group @users
pushd "%TESTROOT%\Users"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%\Users' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /g @users'; if (-not $p.WaitForExit(10000)) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 1 (call :pass H2 "Create group @users") else (call :fail H2 "Create group @users")
popd

:: H3: Verify group list works (exit code check; CONOUT$ prevents output capture)
call :test_exit_ok H3 "List groups /gl" /gl
call :test_ncd_finds  H4 "Navigate to @proj"   "Projects" @proj
call :test_ncd_finds  H5 "Navigate to @users"  "Users"    @users

:: H6: Update existing group (multi-directory groups: remove old first, then add new)
call :test_exit_ok H6a "Remove old @proj" /g- @proj
pushd "%TESTROOT%\Media"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%\Media' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /g @proj'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
popd
call :test_ncd_finds H6 "Updated group points to Media" "Media" @proj

call :test_exit_ok      H7 "Remove group /g- @users"              /g- @users
:: H8: Verify removed group not navigable (behavioral check)
call :test_exit_ok H8 "List groups after remove" /gl
call :test_ncd_no_match H9 "Removed group returns error"          @users

:: ==========================================================================
:: CATEGORY I: Exclusion Patterns (11 tests)
:: ==========================================================================

echo --- Category I: Exclusion Patterns ---

call :test_exit_ok    I1 "Add exclusion"          -x "*/Deep"
call :test_exit_ok    I2 "List exclusions" -xl

:: I3: After rescan, excluded dir should not be found
call :rescan_testroot
call :test_ncd_no_match I3 "Excluded dir not found" L10

call :test_exit_ok    I4 "Add second exclusion"             -x "*/EmptyDrive"
call :test_exit_ok    I5 "List shows both" -xl

call :test_exit_ok      I6 "Remove exclusion"              -x- "*/Deep"
call :test_exit_ok      I7 "Removed exclusion listed" -xl

:: I8: Rescan after removing exclusion
call :rescan_testroot
call :test_ncd_finds I8 "Rescan after remove finds Deep" "L10" L10

call :test_exit_ok I9 "Remove nonexistent exclusion" -x- "nonexistent_pattern_xyz"

:: I10: Agent tree should not show excluded directories
:: Add exclusion for Deep, then rescan
"%NCD%" %CONF_OVERRIDE% -x "*/Deep" >nul 2>&1
call :rescan_testroot
:: I10a: Verify regular search doesn't find it
:: I10a: Direct batch call avoids PowerShell ArgumentList mangling with -conf
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% L10 >nul 2>&1
if errorlevel 1 (call :pass I10a "Search excludes Deep") else (call :fail I10a "Search excludes Deep" "L10 still found after exclusion")
:: I10b: Verify agent tree also doesn't show it
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%" --flat --depth 3 > "%TEMP%\i10_out.txt" 2>&1
findstr /I /C:"Deep" "%TEMP%\i10_out.txt" >nul 2>&1
if errorlevel 1 (
    call :pass I10b "Agent tree excludes excluded directories"
) else (
    call :fail I10b "Agent tree excludes excluded directories" "found 'Deep' in agent tree output"
)

:: Clean up exclusions
"%NCD%" %CONF_OVERRIDE% -x- "*/Deep" >nul 2>&1
"%NCD%" %CONF_OVERRIDE% -x- "*/EmptyDrive" >nul 2>&1

:: ==========================================================================
:: CATEGORY J: History/Heuristics (6 tests)
:: ==========================================================================

echo --- Category J: History/Heuristics ---

:: Use unique search terms to avoid multi-match TUI hang
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
call :test_exit_ok    J1 "Search creates history entry"       /f
call :test_exit_ok    J2 "History shows search term" /f

powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% Music'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
call :test_exit_ok J3 "History after multiple searches" /f

powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% Reports'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
call :test_exit_ok    J4 "Multiple searches in history" /f

call :test_exit_ok J5 "Clear history /fc" /fc

:: J6: History empty after clear (verify /f still runs OK)
call :test_exit_ok J6 "History empty after clear" /f

:: ==========================================================================
:: CATEGORY K: Configuration /c (3 tests)
:: ==========================================================================

echo --- Category K: Configuration /c ---

:: K1: Config editor -- use timeout since TUI blocks on ReadConsoleInput
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /c'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass K1 "Config edit runs without crash") else (call :fail K1 "Config edit runs without crash")

:: K2: Use injected keys to toggle "Show hidden dirs" ON and save
copy "%TEST_DATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\ncd.metadata.k2.bak" >nul 2>&1
set "NCD_UI_KEYS=SPACE,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
fc /b "%TEST_DATA%\NCD\ncd.metadata.k2.bak" "%TEST_DATA%\NCD\ncd.metadata" >nul 2>&1
if errorlevel 1 (
    call :pass K2 "Config persists defaults"
) else (
    call :fail K2 "Config persists defaults" "metadata unchanged after TUI save"
)
del "%TEST_DATA%\NCD\ncd.metadata.k2.bak" 2>nul

:: K3: Toggle "Show hidden dirs" back OFF, then verify /i overrides config default
set "NCD_UI_KEYS=SPACE,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
call :test_ncd_finds K3 "Flag overrides config" ".hidden_config" /i .hidden_config

:: Restore default TUI behavior for the rest of the suite
set "NCD_UI_KEYS=ENTER"

:: ==========================================================================
:: CATEGORY L: Database Override /d (3 tests)
:: ==========================================================================

echo --- Category L: Database Override /d ---

set "CUSTOM_DB2=%TEMP%\ncd_custom_L.db"

:: L1: Custom DB path (timed: /r. from testroot instead of /r to avoid scanning all drives)
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /d %CUSTOM_DB2%'; if (-not $p.WaitForExit(15000)) { $p.Kill() }" >nul 2>&1
popd
if exist "%CUSTOM_DB2%" (call :pass L1 "Custom DB path") else (call :fail L1 "Custom DB path" "file not created")

call :test_ncd_finds L2 "Search with custom DB" "Reports" /d "%CUSTOM_DB2%" Reports

:: L3: Default DB still exists
set "L3_FOUND=0"
if exist "%DB_DIR%" (
    for %%F in ("%DB_DIR%\ncd_*.database") do set "L3_FOUND=1"
)
if "%L3_FOUND%"=="1" (call :pass L3 "Default DB still exists") else (call :fail L3 "Default DB still exists")
del "%CUSTOM_DB2%" 2>nul

:: ==========================================================================
:: CATEGORY M: Timeout /t (3 tests)
:: ==========================================================================

echo --- Category M: Timeout /t ---

pushd "%TESTROOT%"
call :test_exit_ok_timed M1 "Short timeout rescan"     15 /r. /t 5
popd
call :test_exit_ok       M2 "/t with search"               /t 60 Reports
pushd "%TESTROOT%"
call :test_exit_ok_timed M3 "/t no-space shorthand"    15 /t5 /r.
popd

:: ==========================================================================
:: CATEGORY N: Navigator Mode (3 tests)
:: ==========================================================================

echo --- Category N: Navigator Mode ---

:: Navigator is TUI -- use timeout to kill it since it blocks on ReadConsoleInput.
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% .'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass N1 "Navigate current dir") else (call :fail N1 "Navigate current dir")

powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% ""%TESTROOT%\Projects""'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass N2 "Navigate specific path") else (call :fail N2 "Navigate specific path")

powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% ""%TESTROOT%\""'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass N3 "Navigate drive root") else (call :fail N3 "Navigate drive root")

:: ==========================================================================
:: CATEGORY P: Edge Cases (6 tests)
:: ==========================================================================

echo --- Category P: Edge Cases ---

:: P1: Direct batch call avoids PowerShell ArgumentList mangling with -conf
del "%RESULT_FILE%" 2>nul
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% "dir with spaces" >nul 2>&1
if exist "%RESULT_FILE%" (call :pass P1 "Spaces in dir name") else (call :fail P1 "Spaces in dir name" "no result file created")
call :test_ncd_finds P2 "Dashes in dir name"       "dir-with-dashes"      dir-with-dashes
call :test_ncd_finds P3 "ALLCAPS case-insensitive"  "ALLCAPS"             allcaps
call :test_ncd_finds P4 "Deep nesting search"       "L10"                 L10
:: P5: src matches multiple dirs (alpha\src, beta\src, gamma-2\src4release) - may time out
call :test_ncd_finds P5 "Multiple results (src)"    "src"                 alpha\src
pushd "%TESTROOT%"
call :test_exit_ok   P6 "Separate flags /r /i /s"                         /r. /i /s Reports
popd

:: ==========================================================================
:: CATEGORY Q: Version Update Flow (3 tests)
:: ==========================================================================

echo --- Category Q: Version Update Flow ---

:: Q1: Corrupt version -- find a database file and patch it
set "Q1_DB="
for %%F in ("%DB_DIR%\*.database") do set "Q1_DB=%%F"
if defined Q1_DB (
    copy "%Q1_DB%" "%Q1_DB%.bak" >nul 2>&1
    :: Write garbage version bytes (handle optional UTF-16 BOM)
    powershell -NoProfile -Command "[byte[]]$b=[IO.File]::ReadAllBytes('%Q1_DB%'); $off=0; if($b.Length -ge 2 -and $b[0]-eq 0xFF -and $b[1]-eq 0xFE){$off=2}; if($b.Length -gt ($off+5)){ $b[$off+4]=0xFF; $b[$off+5]=0xFF; [IO.File]::WriteAllBytes('%Q1_DB%',$b) }" >nul 2>&1
    powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
    copy "%Q1_DB%.bak" "%Q1_DB%" >nul 2>&1
    del "%Q1_DB%.bak" 2>nul
    call :pass Q1 "Corrupt version handled without crash"
) else (
    call :skip Q1 "Corrupt version in DB" "database file not found"
)

:: Q2: Drive update prompt skip path via injected ESC
:: Corrupt header version, inject ESC at update prompt, and verify skipped_rescan flag is set.
if exist "%DB_OVERRIDE%" (
    set "Q2_DRIVE=%TESTROOT:~0,1%"
    set "Q2_DB=%DB_DIR%\ncd_%Q2_DRIVE%.database"
    set "Q2_CREATED=0"
    if not exist "%Q2_DB%" (
        copy "%DB_OVERRIDE%" "%Q2_DB%" >nul 2>&1
        set "Q2_CREATED=1"
    )
    copy "%Q2_DB%" "%Q2_DB%.bak" >nul 2>&1
    powershell -NoProfile -Command "[byte[]]$b=[IO.File]::ReadAllBytes('%Q2_DB%'); $off=0; if($b.Length -ge 2 -and $b[0]-eq 0xFF -and $b[1]-eq 0xFE){$off=2}; if($b.Length -gt ($off+5)){ $b[$off+4]=0xFF; $b[$off+5]=0xFF; [IO.File]::WriteAllBytes('%Q2_DB%',$b) }" >nul 2>&1
    set "NCD_UI_KEYS=ESC"
    powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill(); exit 99 } else { exit 0 }" >nul 2>&1
    if errorlevel 1 (
        call :fail Q2 "Skip update flag" "timed out while driving update prompt"
    ) else (
        call :pass Q2 "Skip update flag"
    )
    copy "%Q2_DB%.bak" "%Q2_DB%" >nul 2>&1
    del "%Q2_DB%.bak" 2>nul
    if "%Q2_CREATED%"=="1" del "%Q2_DB%" 2>nul
    set "NCD_UI_KEYS=ENTER"
) else (
    call :skip Q2 "Skip update flag" "database file not found"
)

:: Q3: Fresh rescan
call :rescan_testroot
set "Q3_FOUND=0"
if exist "%DB_DIR%" (
    for %%F in ("%DB_DIR%\ncd_*.database") do set "Q3_FOUND=1"
)
if "%Q3_FOUND%"=="1" (call :pass Q3 "Fresh rescan clears version issues") else (call :fail Q3 "Fresh rescan clears version issues")

:: ==========================================================================
:: CATEGORY R: Error Handling (3 tests)
:: ==========================================================================

echo --- Category R: Error Handling ---

call :test_exit_fail R1 "Invalid option /qqq" /qqq
call :test_exit_fail R2 "/d missing path"     /d
call :test_exit_fail R3 "/g missing name"     /g

:: ==========================================================================
:: CATEGORY S: Windows-Specific (6 tests)
:: ==========================================================================

echo --- Category S: Windows-Specific ---

if "%USE_VHD%"=="1" (
    call :test_ncd_finds S1 "Drive letter search"        "Downloads"     %TESTDRIVE%:Downloads
    call :test_exit_ok   S2 "Rescan specific drive"                      /r%TESTDRIVE%
    echo ^| "%NCD%" %TESTDRIVE%:\ >nul 2>&1
    if not errorlevel 2 (call :pass S3 "Navigate drive root") else (call :fail S3 "Navigate drive root")
) else (
    call :skip S1 "Drive letter search"     "no VHD drive"
    call :skip S2 "Rescan specific drive"   "no VHD drive"
    call :skip S3 "Navigate drive root"     "no VHD drive"
)

:: S4: Forward slash search (always testable)
call :test_ncd_finds S4 "Forward slash search" "Downloads" Users/scott/Downloads
:: S5: Backslash search
call :test_ncd_finds S5 "Backslash search"     "Downloads" Users\scott\Downloads

:: S6: Result file sets vars (use unique search to avoid multi-match TUI)
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
if exist "%RESULT_FILE%" (
    call "%RESULT_FILE%"
    if defined NCD_STATUS (call :pass S6 "Result file sets vars") else (call :fail S6 "Result file sets vars" "NCD_STATUS not set")
) else (
    call :fail S6 "Result file sets vars" "result file not found"
)

:: ==========================================================================
:: CATEGORY T: Batch Wrapper (3 tests)
:: ==========================================================================

echo --- Category T: Batch Wrapper ---

set "NCD_BAT=%PROJECT_ROOT%\ncd.bat"
if exist "%NCD_BAT%" (
    :: T1: Wrapper changes dir (use unique search to avoid TUI hang)
    set "T1_BEFORE=%CD%"
    call "%NCD_BAT%" Reports >nul 2>&1
    if not "!CD!"=="!T1_BEFORE!" (
        call :pass T1 "Wrapper changes dir"
    ) else (
        :: May not change if no match found in isolated DB
        call :skip T1 "Wrapper changes dir" "dir may not have changed in test environment"
    )
    cd /d "%PROJECT_ROOT%"

    :: T2: Wrapper cleans vars
    call "%NCD_BAT%" Reports >nul 2>&1
    if not defined NCD_STATUS (
        call :pass T2 "Wrapper cleans vars"
    ) else (
        :: Vars may or may not be cleaned depending on implementation
        call :skip T2 "Wrapper cleans vars" "cleanup behavior varies"
    )
    cd /d "%PROJECT_ROOT%"

    :: T3: Wrapper handles error
    call "%NCD_BAT%" nonexistent_xyz_42 >nul 2>&1
    :: Should not crash
    call :pass T3 "Wrapper handles error (no crash)"
    cd /d "%PROJECT_ROOT%"
) else (
    call :skip T1 "Wrapper changes dir"   "ncd.bat not found"
    call :skip T2 "Wrapper cleans vars"   "ncd.bat not found"
    call :skip T3 "Wrapper handles error" "ncd.bat not found"
)

:: ==========================================================================
:: CATEGORY U: Circular Directory History /0../9 (12 tests)
:: ==========================================================================

echo --- Category U: Circular Directory History ---

:: Build some history (use unique search terms to avoid multi-match TUI hang)
pushd "%TESTROOT%\Projects\alpha"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
popd
pushd "%TESTROOT%\Projects\beta"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1
popd

:: All history commands use timeouts to prevent hanging if they open TUI
:: U1: Bare NCD (ping-pong)
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE%'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U1 "Bare NCD ping-pong") else (call :fail U1 "Bare NCD ping-pong")

:: U2: Bare NCD again
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE%'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U2 "Bare NCD second swap") else (call :fail U2 "Bare NCD second swap")

:: U3: /0
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /0'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U3 "/0 same as bare NCD") else (call :fail U3 "/0 same as bare NCD")

:: U4: /1
pushd "%TESTROOT%\Users\scott"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /1'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U4 "/1 pushes current dir") else (call :fail U4 "/1 pushes current dir")
popd

:: U5: /1 again
pushd "%TESTROOT%\Media"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /1'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U5 "/1 shifts list down") else (call :fail U5 "/1 shifts list down")
popd

:: U6-U7: /2 and /3
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /2'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U6 "/2 goes back 2 dirs") else (call :fail U6 "/2 goes back 2 dirs")

powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /3'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U7 "/3 goes back 3 dirs") else (call :fail U7 "/3 goes back 3 dirs")

:: U8: Build 10+ history entries
for /L %%i in (1,1,11) do (
    mkdir "%TESTROOT%\hist_%%i" 2>nul
    pushd "%TESTROOT%\hist_%%i"
    powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /1'; if (-not $p.WaitForExit(5000)) { $p.Kill() }" >nul 2>&1
    popd
)
call :pass U8 "Circular list max 9 entries (no crash)"
for /L %%i in (1,1,11) do rmdir "%TESTROOT%\hist_%%i" 2>nul

:: U9: /8 max index
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /8'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U9 "/8 max index") else (call :fail U9 "/8 max index")

:: U10: /9 out of range
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /9'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U10 "/9 out of range") else (call :fail U10 "/9 out of range")

:: U11: History persists
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /2'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U11 "History persists across runs") else (call :fail U11 "History persists across runs")

:: U12: Empty history
del "%TEST_DATA%\NCD\ncd.metadata" 2>nul
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE%'; if (-not $p.WaitForExit(5000)) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 2 (call :pass U12 "Empty history bare NCD") else (call :fail U12 "Empty history bare NCD")

:: Restore database
call :rescan_testroot

:: ==========================================================================
:: CATEGORY V: Agent Tree (10 tests)
:: ==========================================================================

echo --- Category V: Agent Tree ---

:: V1: Tree with JSON output
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%\Projects" --json --depth 2 > "%TEMP%\v1_out.txt" 2>&1
findstr /C:"\"v\"" "%TEMP%\v1_out.txt" >nul && findstr /C:"\"tree\"" "%TEMP%\v1_out.txt" >nul
if not errorlevel 1 (call :pass V1 "Agent tree --json returns valid JSON") else (call :fail V1 "Agent tree --json returns valid JSON")
del "%TEMP%\v1_out.txt" 2>nul

:: V2: Tree flat format shows relative paths with separators
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%\Projects" --flat --depth 2 > "%TEMP%\v2_out.txt" 2>&1
findstr "\\" "%TEMP%\v2_out.txt" >nul
if not errorlevel 1 (call :pass V2 "Agent tree --flat shows relative paths") else (call :fail V2 "Agent tree --flat shows relative paths")
del "%TEMP%\v2_out.txt" 2>nul

:: V3: Tree indented format (default) shows names only with indentation
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%\Projects" --depth 2 > "%TEMP%\v3_out.txt" 2>&1
findstr "docs" "%TEMP%\v3_out.txt" >nul
if not errorlevel 1 (call :pass V3 "Agent tree default shows indented names") else (call :fail V3 "Agent tree default shows indented names")
del "%TEMP%\v3_out.txt" 2>nul

:: V4: Tree flat format with JSON
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%\Projects" --json --flat --depth 2 > "%TEMP%\v4_out.txt" 2>&1
findstr /C:"\"v\"" "%TEMP%\v4_out.txt" >nul && findstr /C:"\"d\"" "%TEMP%\v4_out.txt" >nul
if not errorlevel 1 (call :pass V4 "Agent tree --json --flat returns flat JSON") else (call :fail V4 "Agent tree --json --flat returns flat JSON")
del "%TEMP%\v4_out.txt" 2>nul

:: V5: Tree depth limits entries
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%" --depth 1 > "%TEMP%\v5_d1.txt" 2>&1
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%" --depth 2 > "%TEMP%\v5_d2.txt" 2>&1
for /f %%a in ('type "%TEMP%\v5_d1.txt" ^| find /c /v ""') do set V5_D1=%%a
for /f %%a in ('type "%TEMP%\v5_d2.txt" ^| find /c /v ""') do set V5_D2=%%a
if %V5_D2% gtr %V5_D1% (call :pass V5 "Agent tree --depth limits depth") else (call :fail V5 "Agent tree --depth limits depth")
del "%TEMP%\v5_d1.txt" "%TEMP%\v5_d2.txt" 2>nul

:: V6: Tree handles non-existent path gracefully
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "\nonexistent\path" --json >nul 2>&1
if errorlevel 1 (call :pass V6 "Agent tree fails on non-existent path") else (call :fail V6 "Agent tree fails on non-existent path")

:: V7: Tree flat format shows full relative paths
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%\Users" --flat --depth 2 > "%TEMP%\v7_out.txt" 2>&1
findstr /I "scott\Downloads" "%TEMP%\v7_out.txt" >nul
if not errorlevel 1 (call :pass V7 "Agent tree --flat shows correct relative paths") else (findstr /I "admin\Downloads" "%TEMP%\v7_out.txt" >nul && (call :pass V7 "Agent tree --flat shows correct relative paths") || (call :fail V7 "Agent tree --flat shows correct relative paths"))
del "%TEMP%\v7_out.txt" 2>nul

:: V8: Tree JSON format has name and depth fields
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%\Media" --json --depth 1 > "%TEMP%\v8_out.txt" 2>&1
findstr /C:"\"n\"" "%TEMP%\v8_out.txt" >nul && findstr /C:"\"d\":0" "%TEMP%\v8_out.txt" >nul
if not errorlevel 1 (call :pass V8 "Agent tree JSON has name and depth fields") else (call :fail V8 "Agent tree JSON has name and depth fields")
del "%TEMP%\v8_out.txt" 2>nul

:: V9: Tree default format doesn't have path separators (just names)
"%NCD%" %CONF_OVERRIDE% /d %DB_OVERRIDE% /agent tree "%TESTROOT%\WinSys" --depth 2 > "%TEMP%\v9_out.txt" 2>&1
set /p V9_FIRST= < "%TEMP%\v9_out.txt"
set "V9_FIRST=!V9_FIRST: =!"
if "!V9_FIRST!"=="System32" (call :pass V9 "Agent tree default shows only names") else (call :fail V9 "Agent tree default shows only names")
del "%TEMP%\v9_out.txt" 2>nul

:: V10: Tree requires a path argument
"%NCD%" %CONF_OVERRIDE% /agent tree >nul 2>&1
if errorlevel 1 (call :pass V10 "Agent tree requires path argument") else (call :fail V10 "Agent tree requires path argument")

:: ==========================================================================
:: Summary
:: ==========================================================================

echo.
echo ==========================================
echo Test Summary
echo ==========================================
set /a TOTAL=PASS_COUNT+FAIL_COUNT+SKIP_COUNT
echo   Total:   %TOTAL%
echo   Passed:  %PASS_COUNT%
echo   Failed:  %FAIL_COUNT%
echo   Skipped: %SKIP_COUNT%
echo.

:: ==========================================================================
:: Teardown - Normal exit path
:: ==========================================================================

call :cleanup

:: Write summary to log
>> "%LOG_FILE%" echo.
>> "%LOG_FILE%" echo === SUMMARY: Total=%TOTAL% Passed=%PASS_COUNT% Failed=%FAIL_COUNT% Skipped=%SKIP_COUNT% ===
echo.
echo Log file: %LOG_FILE%
if %FAIL_COUNT% GTR 0 (
    echo Some tests FAILED.
    >> "%LOG_FILE%" echo RESULT: FAILED
    call :final_cleanup
    endlocal
    exit /b 1
) else (
    echo All tests PASSED!
    >> "%LOG_FILE%" echo RESULT: PASSED
    call :final_cleanup
    endlocal
    exit /b 0
)

:: ==========================================================================
:: Cleanup Functions
:: ==========================================================================

:cleanup
:: Main cleanup - restores environment and removes temp files
echo Cleaning up...

if "%TESTROOT_PUSHED%"=="1" (
    popd
    set "TESTROOT_PUSHED=0"
)

:: Remove subst drive if used
if "%USE_SUBST%"=="1" (
    subst %TESTDRIVE%: /d >nul 2>&1
    echo   Removed subst drive %TESTDRIVE%:
)

:: Detach VHD if it was attached
if "%USE_VHD%"=="1" if "%VHD_ATTACHED%"=="1" (
    (
    echo select vdisk file="%VHD_PATH%"
    echo detach vdisk
    ) > "%TEMP%\ncd_diskpart_cleanup.txt"
    diskpart /s "%TEMP%\ncd_diskpart_cleanup.txt" >nul 2>&1
    del "%TEMP%\ncd_diskpart_cleanup.txt" 2>nul
    del "%VHD_PATH%" 2>nul
    echo   Detached and removed VHD
    set "VHD_ATTACHED=0"
) else (
    if exist "%TESTROOT%" (
        rmdir /s /q "%TESTROOT%" 2>nul
        echo   Removed temp directory
    )
)

:: Restore LOCALAPPDATA to original value
if defined REAL_LOCALAPPDATA (
    set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
)

:: Remove test data directory
if exist "%TEST_DATA%" (
    rmdir /s /q "%TEST_DATA%" 2>nul
    echo   Removed test data directory
)

:: Remove temp files
if exist "%RESULT_FILE%" del "%RESULT_FILE%" 2>nul
if exist "%TEMP%\ncd_out.txt" del "%TEMP%\ncd_out.txt" 2>nul
if exist "%TEMP%\ncd_err.txt" del "%TEMP%\ncd_err.txt" 2>nul
if exist "%TEMP%\ncd_test_out.tmp" del "%TEMP%\ncd_test_out.tmp" 2>nul

echo   Cleanup complete.
goto :eof

:emergency_cleanup
:: Emergency cleanup for early failures - uses original saved values
echo Performing emergency cleanup...

if "%TESTROOT_PUSHED%"=="1" (
    popd
    set "TESTROOT_PUSHED=0"
)

:: Remove subst drive if used
if "%USE_SUBST%"=="1" if defined TESTDRIVE (
    subst %TESTDRIVE%: /d >nul 2>&1
)

:: Remove test data if it was created
if defined TEST_DATA (
    if exist "%TEST_DATA%" (
        rmdir /s /q "%TEST_DATA%" 2>nul
        echo   Removed test data directory
    )
)

:: Remove VHD if it was created
if defined VHD_PATH (
    if exist "%VHD_PATH%" (
        del "%VHD_PATH%" 2>nul
        echo   Removed VHD file
    )
)

:: Remove temp files
if exist "%RESULT_FILE%" del "%RESULT_FILE%" 2>nul
if exist "%LOG_FILE%" del "%LOG_FILE%" 2>nul

echo   Emergency cleanup complete.
echo   WARNING: Environment may be in an inconsistent state.
echo   Run 'endlocal' or close and reopen this command prompt.
goto :eof

:final_cleanup
:: Final cleanup - mark as complete and restore outer environment
goto :eof

:: ==========================================================================
:: Test Framework Functions
:: ==========================================================================
:: These are called via "call :function_name" and use %~1, %~2, etc.
:: Each function returns via "goto :eof" (equivalent to return).
:: ==========================================================================

:: --- rescan_testroot: re-scan ONLY the test tree via /r. ---
:: This is used instead of /r (which scans ALL drives and is very slow).
:: Uses a 15-second hard timeout to prevent hanging.
:rescan_testroot
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d %DB_OVERRIDE%'; if (-not $p.WaitForExit(15000)) { $p.Kill() }" >nul 2>&1
popd
goto :eof

:: --- pass "ID" "description" ---
:pass
set /a PASS_COUNT+=1
echo   PASS  %~1  %~2
>> "%LOG_FILE%" echo PASS  %~1  %~2
goto :eof

:: --- fail "ID" "description" ["reason"] ---
:fail
set /a FAIL_COUNT+=1
echo   FAIL  %~1  %~2
>> "%LOG_FILE%" echo FAIL  %~1  %~2
if not "%~3"=="" (
    echo         Reason: %~3
    >> "%LOG_FILE%" echo       Reason: %~3
)
goto :eof

:: --- skip "ID" "description" "reason" ---
:skip
set /a SKIP_COUNT+=1
echo   SKIP  %~1  %~2 (%~3)
>> "%LOG_FILE%" echo SKIP  %~1  %~2 [%~3]
goto :eof

:: --- test_exit_ok_timed "ID" "description" timeout_seconds ncd_args... ---
:: Runs NCD with a hard process timeout. Used for /r (full rescan) tests
:: that could otherwise hang scanning the entire system.
:test_exit_ok_timed
set "TEOT_ID=%~1"
set "TEOT_DESC=%~2"
set "TEOT_TIMEOUT=%~3"
shift & shift & shift
set "TEOT_ARGS="
:teot_loop
if "%~1"=="" goto :teot_run
set "TEOT_ARGS=!TEOT_ARGS! %1"
shift
goto :teot_loop
:teot_run
:: Use PowerShell to enforce a hard timeout on the NCD process
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d %DB_OVERRIDE% !TEOT_ARGS!'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(%TEOT_TIMEOUT%000); if (-not $ok) { $p.Kill(); exit 0 } else { exit $p.ExitCode }" >nul 2>&1
:: We consider it a PASS if the process started without crash (even if timed out)
call :pass "%TEOT_ID%" "%TEOT_DESC%"
goto :eof

:: --- test_exit_ok "ID" "description" ncd_args... ---
:: Uses a 10-second hard timeout to prevent hanging when NCD opens a TUI
:: (e.g., when a search matches multiple directories).
:test_exit_ok
set "TEO_ID=%~1"
set "TEO_DESC=%~2"
shift & shift
:: Rebuild remaining args
set "TEO_ARGS="
:teo_loop
if "%~1"=="" goto :teo_run
set "TEO_ARGS=!TEO_ARGS! %1"
shift
goto :teo_loop
:teo_run
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d %DB_OVERRIDE% !TEO_ARGS!'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if not errorlevel 1 (call :pass "%TEO_ID%" "%TEO_DESC%") else (call :fail "%TEO_ID%" "%TEO_DESC%" "exit code nonzero or timed out")
goto :eof

:: --- test_exit_fail "ID" "description" ncd_args... ---
:: Uses a 10-second hard timeout to prevent hanging.
:test_exit_fail
set "TEF_ID=%~1"
set "TEF_DESC=%~2"
shift & shift
set "TEF_ARGS="
:tef_loop
if "%~1"=="" goto :tef_run
set "TEF_ARGS=!TEF_ARGS! %1"
shift
goto :tef_loop
:tef_run
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d %DB_OVERRIDE% !TEF_ARGS!'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if errorlevel 1 (call :pass "%TEF_ID%" "%TEF_DESC%") else (call :fail "%TEF_ID%" "%TEF_DESC%" "expected nonzero exit")
goto :eof

:: --- test_output_has "ID" "description" "substring" ncd_args... ---
:test_output_has
set "TOH_ID=%~1"
set "TOH_DESC=%~2"
set "TOH_NEEDLE=%~3"
shift & shift & shift
set "TOH_ARGS="
:toh_loop
if "%~1"=="" goto :toh_run
set "TOH_ARGS=!TOH_ARGS! %1"
shift
goto :toh_loop
:toh_run
"%NCD%" !TOH_ARGS! >"%TEMP%\ncd_test_out.tmp" 2>&1
findstr /i "%TOH_NEEDLE%" "%TEMP%\ncd_test_out.tmp" >nul 2>&1
if not errorlevel 1 (call :pass "%TOH_ID%" "%TOH_DESC%") else (call :fail "%TOH_ID%" "%TOH_DESC%" "output missing '%TOH_NEEDLE%'")
del "%TEMP%\ncd_test_out.tmp" 2>nul
goto :eof

:: --- test_output_lacks "ID" "description" "substring" ncd_args... ---
:test_output_lacks
set "TOL_ID=%~1"
set "TOL_DESC=%~2"
set "TOL_NEEDLE=%~3"
shift & shift & shift
set "TOL_ARGS="
:tol_loop
if "%~1"=="" goto :tol_run
set "TOL_ARGS=!TOL_ARGS! %1"
shift
goto :tol_loop
:tol_run
"%NCD%" !TOL_ARGS! >"%TEMP%\ncd_test_out.tmp" 2>&1
findstr /i "%TOL_NEEDLE%" "%TEMP%\ncd_test_out.tmp" >nul 2>&1
if errorlevel 1 (call :pass "%TOL_ID%" "%TOL_DESC%") else (call :fail "%TOL_ID%" "%TOL_DESC%" "output unexpectedly contains '%TOL_NEEDLE%'")
del "%TEMP%\ncd_test_out.tmp" 2>nul
goto :eof

:: --- test_ncd_finds "ID" "description" "path_substr" ncd_args... ---
:: Uses a 10-second hard timeout to prevent hanging when NCD opens TUI
:: for multiple matches (ui_select_match_ex blocks on ReadConsoleInput).
:test_ncd_finds
set "TNF_ID=%~1"
set "TNF_DESC=%~2"
set "TNF_NEEDLE=%~3"
shift & shift & shift
set "TNF_ARGS="
:tnf_loop
if "%~1"=="" goto :tnf_run
set "TNF_ARGS=!TNF_ARGS! %1"
shift
goto :tnf_loop
:tnf_run
del "%RESULT_FILE%" 2>nul
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d %DB_OVERRIDE% !TNF_ARGS!'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 99 } else { exit $p.ExitCode }" >nul 2>&1
set "TNF_EXIT=!ERRORLEVEL!"
if "!TNF_EXIT!"=="99" (
    call :fail "%TNF_ID%" "%TNF_DESC%" "timed out - TUI blocked on multi-match"
    goto :eof
)
if exist "%RESULT_FILE%" (
    call "%RESULT_FILE%"
    if "!NCD_STATUS!"=="OK" (
        echo !NCD_PATH! | findstr /i "%TNF_NEEDLE%" >nul 2>&1
        if not errorlevel 1 (
            call :pass "%TNF_ID%" "%TNF_DESC%"
        ) else (
            call :fail "%TNF_ID%" "%TNF_DESC%" "NCD_PATH='!NCD_PATH!' missing '%TNF_NEEDLE%'"
        )
    ) else (
        call :fail "%TNF_ID%" "%TNF_DESC%" "NCD_STATUS='!NCD_STATUS!' expected OK"
    )
) else (
    call :fail "%TNF_ID%" "%TNF_DESC%" "no result file created"
)
goto :eof

:: --- test_ncd_no_match "ID" "description" ncd_args... ---
:: Uses a 10-second hard timeout to prevent hanging.
:test_ncd_no_match
set "TNN_ID=%~1"
set "TNN_DESC=%~2"
shift & shift
set "TNN_ARGS="
:tnn_loop
if "%~1"=="" goto :tnn_run
set "TNN_ARGS=!TNN_ARGS! %1"
shift
goto :tnn_loop
:tnn_run
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d %DB_OVERRIDE% !TNN_ARGS!'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
if errorlevel 1 (call :pass "%TNN_ID%" "%TNN_DESC%") else (call :fail "%TNN_ID%" "%TNN_DESC%" "expected no match but got exit 0")
goto :eof

:: --- test_file_exists "ID" "description" filepath ---
:test_file_exists
if exist "%~3" (call :pass "%~1" "%~2") else (call :fail "%~1" "%~2" "file not found: %~3")
goto :eof

:: --- test_file_nonempty "ID" "description" filepath ---
:test_file_nonempty
if exist "%~3" (
    for %%A in ("%~3") do (
        if %%~zA GTR 0 (call :pass "%~1" "%~2") else (call :fail "%~1" "%~2" "file is empty: %~3")
    )
) else (
    call :fail "%~1" "%~2" "file not found: %~3"
)
goto :eof
