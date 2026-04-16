@echo off
:: ==========================================================================
:: test_minimal4.bat - Minimal NCD test script #4
:: ==========================================================================
::
:: WARNING: This script has hardcoded paths and is for development only.
:: It should NOT be run directly - use the test harness instead.
::
:: Run through the test harness:
::   Run-Tests-Safe.bat windows
::
:: DO NOT RUN THIS SCRIPT DIRECTLY!
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
    echo This test script has HARDCODED PATHS and is NOT meant to be run directly!
    echo.
    echo This is a DEVELOPMENT-ONLY script that:
    echo   - Uses hardcoded paths ^(E:\llama\NewChangeDirectory^)
    echo   - May not work on your system
    echo   - Can corrupt your NCD configuration
    echo.
    echo CORRECT USAGE - Run from the project root:
    echo   Run-Tests-Safe.bat windows           ^(all Windows tests^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Win\test_minimal4.bat
    echo.
    echo If your environment is already corrupted, repair it with:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo ==========================================
    exit /b 1
)

setlocal enabledelayedexpansion
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test4.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test4_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"

:: Disable NCD background rescans to prevent scanning user drives during tests
set "NCD_TEST_MODE=1"

mkdir "%TEST_DATA%\NCD" 2>nul

:: Create test tree in TEMP (not using E: drive directly)
set "TEST_TREE=%TEMP%\ncd_min_tree4"
mkdir "%TEST_TREE%\testdir456\sub" 2>nul

:: Create isolated metadata and keep all test data local to TEST_DATA.
echo [1] Creating isolated test metadata... > "%LOG%"
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\\NCD\\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))" >> "%LOG%" 2>&1

:: CRITICAL: Disable auto-rescan in copied metadata to prevent full system scan
:: Use keystroke injection to automate the config editor (navigate to item 6, enter -1, save)
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" /c >nul 2>&1
set "NCD_UI_KEYS="

:: Now try /r. to add our test dirs to the existing database
echo [2] Testing /r. with existing DB >> "%LOG%"
pushd "%TEST_TREE%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" /r. >nul 2>&1
echo [2] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

:: Search for a known dir and our test dir
echo [3] Search Downloads >> "%LOG%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" Downloads >nul 2>&1
echo [3] search exit: %ERRORLEVEL% >> "%LOG%"

echo [4] Search testdir456 >> "%LOG%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" testdir456 >nul 2>&1
echo [4] search exit: %ERRORLEVEL% >> "%LOG%"

echo [5] Done >> "%LOG%"
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TEST_TREE%" 2>nul
type "%LOG%"
del "%LOG%" 2>nul
endlocal
