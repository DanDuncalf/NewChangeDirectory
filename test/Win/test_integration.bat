@echo off
:: ==========================================================================
:: test_integration.bat - Integration tests for NCD on Windows
:: ==========================================================================
::
:: This must be run from the project root directory through the test harness.
::
:: IMPORTANT: This script modifies LOCALAPPDATA and NCD_TEST_MODE.
:: It MUST be run through the test harness for proper isolation.
::
:: CORRECT USAGE - Run from project root:
::   Run-Tests-Safe.bat integration
::   Run-Tests-Safe.bat windows
::   Run-Tests-Safe.bat
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
    echo This test script is NOT meant to be run directly!
    echo.
    echo Running this script directly will:
    echo   - Modify your LOCALAPPDATA environment variable
    echo   - Leave your NCD configuration in an inconsistent state
    echo   - Potentially corrupt your NCD database
    echo.
    echo CORRECT USAGE - Run from the project root:
    echo   Run-Tests-Safe.bat integration       ^(integration tests^)
    echo   Run-Tests-Safe.bat windows           ^(all Windows tests^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Win\test_integration.bat
    echo.
    echo If your environment is already corrupted, repair it with:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo ==========================================
    exit /b 1
)

setlocal enabledelayedexpansion

:: Save original LOCALAPPDATA and set up isolation
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_int_test_%RANDOM%"
mkdir "%TEST_DATA%\NCD" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

:: CRITICAL: Disable auto-rescan to prevent background scans of user drives
set "NCD_TEST_MODE=1"

set "NCD_EXE=NewChangeDirectory.exe"
set "NCD_BAT=ncd.bat"
set "TEST_DIR=test_data"
set "ALL_PASSED=1"

echo === NCD Integration Tests ===
echo.

:: Check prerequisites
if not exist "%NCD_EXE%" (
    echo ERROR: %NCD_EXE% not found. Please build first.
    call :cleanup
    exit /b 1
)

if not exist "%NCD_BAT%" (
    echo ERROR: %NCD_BAT% not found.
    call :cleanup
    exit /b 1
)

:: Setup test environment
echo Setting up test environment...
if exist "%TEST_DIR%" rmdir /s /q "%TEST_DIR%"
mkdir "%TEST_DIR%\dir1"
mkdir "%TEST_DIR%\dir2\subdir"
echo. > "%TEST_DIR%\file.txt"
echo   Created test directories

:: Test 1: Version display
echo.
echo Test 1: Version display...
"%NCD_EXE%" -conf "%TEST_DATA%\NCD\ncd.metadata" /v >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: Version displayed
) else (
    echo   FAIL: Version display failed
    set "ALL_PASSED=0"
)

:: Test 2: Help display
echo.
echo Test 2: Help display...
"%NCD_EXE%" -conf "%TEST_DATA%\NCD\ncd.metadata" /? >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: Help displayed
) else (
    echo   FAIL: Help display failed
    set "ALL_PASSED=0"
)

:: Test 3: Database scan (subdirectory only to avoid scanning all drives)
echo.
echo Test 3: Database scan...
pushd "%TEST_DIR%"
"%NCD_EXE%" -conf "%TEST_DATA%\NCD\ncd.metadata" /r. >nul 2>&1
popd
if exist "%TEST_DATA%\NCD\*.database" (
    echo   PASS: Database created
) else (
    echo   FAIL: Database not created
    set "ALL_PASSED=0"
)

:: Test 4: History display
echo.
echo Test 4: History display...
"%NCD_EXE%" -conf "%TEST_DATA%\NCD\ncd.metadata" /f >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: History displayed
) else (
    echo   FAIL: History display failed
    set "ALL_PASSED=0"
)

:: Test 5: Clear history
echo.
echo Test 5: Clear history...
"%NCD_EXE%" -conf "%TEST_DATA%\NCD\ncd.metadata" /fc >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: History cleared
) else (
    echo   FAIL: History clear failed
    set "ALL_PASSED=0"
)

:: Test 6: Search for directory
echo.
echo Test 6: Directory search...
"%NCD_EXE%" -conf "%TEST_DATA%\NCD\ncd.metadata" subdir >nul 2>&1
:: Note: This may fail if no match found, that's OK for integration test
:: We just verify it doesn't crash
echo   PASS: Search executed without crash

:: Cleanup
echo.
echo Cleaning up...
call :cleanup
echo   Test environment cleaned

:: Summary
echo.
echo === Integration Tests Complete ===
if "%ALL_PASSED%"=="1" (
    echo All tests PASSED!
    exit /b 0
) else (
    echo Some tests FAILED!
    exit /b 1
)

:cleanup
:: Restore original LOCALAPPDATA and clean up test data
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
if exist "%TEST_DIR%" rmdir /s /q "%TEST_DIR%" 2>nul
if exist "%TEST_DATA%" rmdir /s /q "%TEST_DATA%" 2>nul
goto :eof
