@echo off
:: ==========================================================================
:: test_ncd_win_standalone.bat -- NCD client tests on Windows WITHOUT service
:: ==========================================================================
::
:: PURPOSE:
::   Tests NCD client in standalone/disk-based state access mode.
::   Verifies that NCD works correctly without the resident service running.
::
:: TESTS:
::   - Basic search operations
::   - Help output
::   - Version information
::   - Database operations (rescan, search)
::   - Agent commands (query, check, ls)
::
:: REQUIREMENTS:
::   - NewChangeDirectory.exe built in project root
::   - Run from project root: test\test_ncd_win_standalone.bat
::
:: EXIT CODES:
::   0 - All tests passed
::   1 - One or more tests failed
::
:: ==========================================================================

setlocal enabledelayedexpansion

:: Disable NCD background rescans to prevent scanning user drives
set "NCD_TEST_MODE=1"

:: Configuration
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "NCD=%PROJECT_ROOT%\NewChangeDirectory.exe"

:: Isolation: redirect all NCD data to a temp directory
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_ncd_test_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

:: Test drive/directory setup
set "TESTROOT=%TEMP%\ncd_test_tree_%RANDOM%"
mkdir "%TESTROOT%" 2>nul

:: Test counters
set "TESTS_RUN=0"
set "TESTS_PASSED=0"
set "TESTS_FAILED=0"

:: Service control
set "SERVICE_EXE=%PROJECT_ROOT%\NCDService.exe"

echo ==========================================
echo NCD Client Tests - Windows (Standalone)
echo ==========================================
echo.
echo NCD: %NCD%
echo Test Data: %TEST_DATA%
echo Test Root: %TESTROOT%
echo.

:: ==========================================================================
:: Pre-flight checks
:: ==========================================================================

if not exist "%NCD%" (
    echo ERROR: NCD executable not found: %NCD%
    echo Please build the project first with: build.bat
    goto :cleanup
)

:: ==========================================================================
:: Ensure service is NOT running
:: ==========================================================================

echo Ensuring service is NOT running...
if exist "%SERVICE_EXE%" (
    "%SERVICE_EXE%" stop >nul 2>&1
    taskkill /F /IM NCDService.exe >nul 2>&1
    timeout /t 1 /nobreak >nul
)

:: Verify service is not running
tasklist /FI "IMAGENAME eq NCDService.exe" 2>nul | findstr "NCDService.exe" >nul
if not errorlevel 1 (
    echo WARNING: Service is still running, attempting force kill...
    taskkill /F /IM NCDService.exe >nul 2>&1
    timeout /t 2 /nobreak >nul
)

:: Verify standalone mode
echo Verifying standalone mode...
for /f "delims=" %%a in ('"%NCD%" /agent check --service-status 2^>^&1') do set "STATUS=%%a"
echo   Service status: %STATUS%
echo.

:: ==========================================================================
:: Test Helper Functions
:: ==========================================================================

:pass
set /a TESTS_RUN+=1
set /a TESTS_PASSED+=1
echo   [PASS] %~1
goto :eof

:fail
set /a TESTS_RUN+=1
set /a TESTS_FAILED+=1
echo   [FAIL] %~1
if not "%~2"=="" echo          Reason: %~2
goto :eof

:skip
set /a TESTS_RUN+=1
echo   [SKIP] %~1
if not "%~2"=="" echo          Reason: %~2
goto :eof

:: Run NCD with timeout (prevents hanging on TUI)
:ncd_run_timed
set "TIMEOUT_SEC=%~1"
shift
set "NCD_ARGS="
:ncd_args_loop
if "%~1"=="" goto :ncd_run_exec
set "NCD_ARGS=!NCD_ARGS! %1"
shift
goto :ncd_args_loop
:ncd_run_exec
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '!NCD_ARGS!' -RedirectStandardOutput '%TEMP%\ncd_out.txt' -RedirectStandardError '%TEMP%\ncd_err.txt'; if (-not $p.WaitForExit(%TIMEOUT_SEC%000)) { $p.Kill(); exit 124 } else { exit $p.ExitCode }" >nul 2>&1
set "LAST_EXIT=%ERRORLEVEL%"
if exist "%TEMP%\ncd_out.txt" set /p LAST_OUT= < "%TEMP%\ncd_out.txt" 2>nul
if exist "%TEMP%\ncd_err.txt" set /p LAST_ERR= < "%TEMP%\ncd_err.txt" 2>nul
goto :eof

:: Run NCD without timeout
:ncd_run
set "NCD_ARGS="
:ncd_args_loop2
if "%~1"=="" goto :ncd_run_exec2
set "NCD_ARGS=!NCD_ARGS! %1"
shift
goto :ncd_args_loop2
:ncd_run_exec2
"%NCD%" %NCD_ARGS% > "%TEMP%\ncd_out.txt" 2> "%TEMP%\ncd_err.txt"
set "LAST_EXIT=%ERRORLEVEL%"
set /p LAST_OUT= < "%TEMP%\ncd_out.txt" 2>nul
set /p LAST_ERR= < "%TEMP%\ncd_err.txt" 2>nul
goto :eof

:: ==========================================================================
:: Setup Test Environment
:: ==========================================================================

echo Setting up test environment...

:: Create minimal metadata file
mkdir "%TEST_DATA%\NCD" 2>nul
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))" >nul 2>&1

:: Set config override
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

:: Create test directory tree
mkdir "%TESTROOT%\Projects\alpha\src" 2>nul
mkdir "%TESTROOT%\Projects\beta\src" 2>nul
mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul
mkdir "%TESTROOT%\Users\scott\Documents" 2>nul
mkdir "%TESTROOT%\Data\Files" 2>nul

echo   Created test directory tree
echo.

:: Initial scan
echo Performing initial scan of test tree...
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r.'; if (-not $p.WaitForExit(30000)) { $p.Kill() }" >nul 2>&1
popd
echo   Scan complete.
echo.

:: ==========================================================================
:: Main Test Suite
:: ==========================================================================

echo --- Test Suite: Standalone Mode Operations ---
echo.

:: Test 1: Help output
echo [TEST 1] Help with /h
call :ncd_run_timed 10 %CONF_OVERRIDE% /h
if %LAST_EXIT%==0 (
    call :pass "Help with /h"
) else (
    call :fail "Help with /h" "exit code %LAST_EXIT%"
)

:: Test 2: Version information
echo [TEST 2] Version with /v
call :ncd_run_timed 10 %CONF_OVERRIDE% /v
if %LAST_EXIT%==0 (
    call :pass "Version with /v"
) else (
    call :fail "Version with /v" "exit code %LAST_EXIT%"
)

:: Test 3: Service status shows NOT_RUNNING
echo [TEST 3] Service status shows NOT_RUNNING
call :ncd_run %CONF_OVERRIDE% /agent check --service-status
echo %LAST_OUT% | findstr /i "NOT_RUNNING\|not_running" >nul
if not errorlevel 1 (
    call :pass "Service status shows NOT_RUNNING"
) else (
    call :fail "Service status shows NOT_RUNNING" "output: %LAST_OUT%"
)

:: Test 4: Basic search finds directory
echo [TEST 4] Basic search finds directory
call :ncd_run_timed 10 %CONF_OVERRIDE% Downloads
echo %LAST_OUT% | findstr /i "Downloads" >nul
if not errorlevel 1 (
    call :pass "Basic search finds directory"
) else (
    call :fail "Basic search finds directory" "output: %LAST_OUT%"
)

:: Test 5: Multi-component search
echo [TEST 5] Multi-component search
call :ncd_run_timed 10 %CONF_OVERRIDE% scott\Downloads
echo %LAST_OUT% | findstr /i "Downloads" >nul
if not errorlevel 1 (
    call :pass "Multi-component search"
) else (
    call :fail "Multi-component search" "output: %LAST_OUT%"
)

:: Test 6: Agent query command
echo [TEST 6] Agent query command
call :ncd_run %CONF_OVERRIDE% /agent query Downloads
echo %LAST_OUT% | findstr /i "Downloads" >nul
if not errorlevel 1 (
    call :pass "Agent query command"
) else (
    call :fail "Agent query command" "output: %LAST_OUT%"
)

:: Test 7: Agent check database age
echo [TEST 7] Agent check database age
call :ncd_run %CONF_OVERRIDE% /agent check --db-age
if %LAST_EXIT%==0 (
    call :pass "Agent check database age"
) else (
    call :fail "Agent check database age" "exit code %LAST_EXIT%"
)

:: Test 8: Agent check stats
echo [TEST 8] Agent check stats
call :ncd_run %CONF_OVERRIDE% /agent check --stats
if %LAST_EXIT%==0 (
    call :pass "Agent check stats"
) else (
    call :fail "Agent check stats" "exit code %LAST_EXIT%"
)

:: Test 9: Rescan creates database
echo [TEST 9] Rescan creates database
del "%TEST_DATA%\NCD\ncd_*.database" 2>nul
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r.'; if (-not $p.WaitForExit(30000)) { $p.Kill() }" >nul 2>&1
popd
set "DB_FOUND=0"
for %%F in ("%TEST_DATA%\NCD\ncd_*.database") do set "DB_FOUND=1"
if "%DB_FOUND%"=="1" (
    call :pass "Rescan creates database"
) else (
    call :fail "Rescan creates database" "no database file found"
)

:: Test 10: Search after rescan
echo [TEST 10] Search after rescan
call :ncd_run_timed 10 %CONF_OVERRIDE% Projects
echo %LAST_OUT% | findstr /i "Projects" >nul
if not errorlevel 1 (
    call :pass "Search after rescan"
) else (
    call :fail "Search after rescan" "output: %LAST_OUT%"
)

:: Test 11: No match returns error
echo [TEST 11] No match returns error
call :ncd_run_timed 10 %CONF_OVERRIDE% NONEXISTENT_DIR_12345
if not %LAST_EXIT%==0 (
    call :pass "No match returns error"
) else (
    call :fail "No match returns error" "exit code %LAST_EXIT% (expected non-zero)"
)

:: Test 12: Database override /d
echo [TEST 12] Database override /d
set "CUSTOM_DB=%TEMP%\ncd_test_%RANDOM%.db"
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /d %CUSTOM_DB%'; if (-not $p.WaitForExit(30000)) { $p.Kill() }" >nul 2>&1
popd
if exist "%CUSTOM_DB%" (
    call :pass "Database override /d"
) else (
    call :fail "Database override /d" "custom database not created"
)
del "%CUSTOM_DB%" 2>nul

:: ==========================================================================
:: Summary
:: ==========================================================================

:cleanup
echo.
echo ==========================================
echo Test Summary
echo ==========================================
echo   Total:   %TESTS_RUN%
echo   Passed:  %TESTS_PASSED%
echo   Failed:  %TESTS_FAILED%
echo.

:: Cleanup: ensure service is stopped
if exist "%SERVICE_EXE%" (
    "%SERVICE_EXE%" stop >nul 2>&1
    taskkill /F /IM NCDService.exe >nul 2>&1
)

:: Cleanup: remove test data
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
if exist "%TEST_DATA%" rmdir /s /q "%TEST_DATA%" 2>nul
if exist "%TESTROOT%" rmdir /s /q "%TESTROOT%" 2>nul
del "%TEMP%\ncd_out.txt" 2>nul
del "%TEMP%\ncd_err.txt" 2>nul

if %TESTS_FAILED% GTR 0 (
    echo RESULT: FAILED
    endlocal
    exit /b 1
) else (
    echo RESULT: PASSED
    endlocal
    exit /b 0
)
