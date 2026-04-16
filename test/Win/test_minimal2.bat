@echo off
:: ==========================================================================
:: test_minimal2.bat - Minimal NCD test script #2
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
    echo   test\Run-Isolated.bat test\Win\test_minimal2.bat
    echo.
    echo If your environment is already corrupted, repair it with:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo ==========================================
    exit /b 1
)

setlocal enabledelayedexpansion
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "LOG=%TEMP%\ncd_minimal_test2.log"
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_min_test2_%RANDOM%"
set "LOCALAPPDATA=%TEST_DATA%"

:: Disable NCD background rescans to prevent scanning user drives during tests
set "NCD_TEST_MODE=1"

mkdir "%TEST_DATA%\NCD" 2>nul
echo. > "%TEST_DATA%\NCD\ncd.metadata"

:: Create test tree in TEMP (not on E: drive directly)
set "TEST_TREE=%TEMP%\ncd_min_tree2"
mkdir "%TEST_TREE%\a\b\c" 2>nul

echo [1] LOCALAPPDATA=%LOCALAPPDATA% > "%LOG%"
echo [2] Test dir: %TEST_TREE% >> "%LOG%"

pushd "%TEST_TREE%"
echo [3] CWD: %CD% >> "%LOG%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" /r. >nul 2>&1
echo [3] /r. exit: %ERRORLEVEL% >> "%LOG%"
popd

echo [4] Now searching... >> "%LOG%"
"%NCD%" -conf "%TEST_DATA%\NCD\ncd.metadata" ncd_min_tree2 >nul 2>&1
echo [4] search exit: %ERRORLEVEL% >> "%LOG%"

echo [5] Done >> "%LOG%"
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TEST_TREE%" 2>nul
type "%LOG%"
del "%LOG%" 2>nul
endlocal
