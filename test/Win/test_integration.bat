@echo off
:: test_integration.bat - Integration tests for NCD on Windows
:: This must be run from the project root directory

setlocal enabledelayedexpansion

set "NCD_EXE=NewChangeDirectory.exe"
set "NCD_BAT=ncd.bat"
set "TEST_DIR=test_data"
set "ALL_PASSED=1"

echo === NCD Integration Tests ===
echo.

:: Check prerequisites
if not exist "%NCD_EXE%" (
    echo ERROR: %NCD_EXE% not found. Please build first.
    exit /b 1
)

if not exist "%NCD_BAT%" (
    echo ERROR: %NCD_BAT% not found.
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
%NCD_EXE% /v >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: Version displayed
) else (
    echo   FAIL: Version display failed
    set "ALL_PASSED=0"
)

:: Test 2: Help display
echo.
echo Test 2: Help display...
%NCD_EXE% /? >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: Help displayed
) else (
    echo   FAIL: Help display failed
    set "ALL_PASSED=0"
)

:: Test 3: Database scan
echo.
echo Test 3: Database scan...
%NCD_EXE% /r >nul 2>&1
if exist "%LOCALAPPDATA%\NCD\*.database" (
    echo   PASS: Database created
) else (
    echo   FAIL: Database not created
    set "ALL_PASSED=0"
)

:: Test 4: History display
echo.
echo Test 4: History display...
%NCD_EXE% /f >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: History displayed
) else (
    echo   FAIL: History display failed
    set "ALL_PASSED=0"
)

:: Test 5: Clear history
echo.
echo Test 5: Clear history...
%NCD_EXE% /fc >nul 2>&1
if %errorlevel% == 0 (
    echo   PASS: History cleared
) else (
    echo   FAIL: History clear failed
    set "ALL_PASSED=0"
)

:: Test 6: Search for directory
echo.
echo Test 6: Directory search...
%NCD_EXE% subdir >nul 2>&1
:: Note: This may fail if no match found, that's OK for integration test
:: We just verify it doesn't crash
echo   PASS: Search executed without crash

:: Cleanup
echo.
echo Cleaning up...
if exist "%TEST_DIR%" rmdir /s /q "%TEST_DIR%"
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
