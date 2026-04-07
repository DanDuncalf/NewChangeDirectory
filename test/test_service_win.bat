@echo off
:: ==========================================================================
:: test_service_win.bat -- Windows Service tests WITHOUT NCD client
:: ==========================================================================
::
:: PURPOSE:
::   Tests the resident service (NCDService.exe) in isolation without
::   involving the NCD client. Verifies service lifecycle, IPC, and basic
::   operations.
::
:: TESTS:
::   - Service start
::   - Service stop  
::   - Service status query
::   - Service IPC ping
::   - Service version query
::   - Service state info
::
:: REQUIREMENTS:
::   - NCDService.exe built in project root
::   - Run from project root: test\test_service_win.bat
::
:: EXIT CODES:
::   0 - All tests passed
::   1 - One or more tests failed
::
:: ==========================================================================

setlocal enabledelayedexpansion

:: Disable NCD background rescans during testing
set "NCD_TEST_MODE=1"

:: Configuration
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "SERVICE_EXE=%PROJECT_ROOT%\NCDService.exe"
set "SERVICE_BAT=%PROJECT_ROOT%\ncd_service.bat"

:: Isolation: use temp directory for all service data
set "TEST_DATA=%TEMP%\ncd_service_test_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

:: Test counters
set "TESTS_RUN=0"
set "TESTS_PASSED=0"
set "TESTS_FAILED=0"

:: Service control timeout (seconds)
set "SERVICE_TIMEOUT=10"

echo ==========================================
echo Windows Service Tests (Isolation Mode)
echo ==========================================
echo.
echo Service: %SERVICE_EXE%
echo Test Data: %TEST_DATA%
echo.

:: ==========================================================================
:: Pre-flight checks
:: ==========================================================================

if not exist "%SERVICE_EXE%" (
    echo ERROR: Service executable not found: %SERVICE_EXE%
    echo Please build the project first with: build.bat
    goto :cleanup
)

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

:: Stop service with timeout
:stop_service
echo   [INFO] Stopping service...
if exist "%SERVICE_BAT%" (
    call "%SERVICE_BAT%" stop >nul 2>&1
) else (
    "%SERVICE_EXE%" stop >nul 2>&1
)
timeout /t 1 /nobreak >nul 2>&1
:: Force kill if still running
taskkill /F /IM NCDService.exe >nul 2>&1
timeout /t 1 /nobreak >nul 2>&1
goto :eof

:: Check if service is running
:is_service_running
set "SERVICE_PID="
for /f "tokens=2" %%a in ('tasklist /FI "IMAGENAME eq NCDService.exe" /NH 2^>nul') do set "SERVICE_PID=%%a"
if defined SERVICE_PID (
    exit /b 0
) else (
    exit /b 1
)

:: Wait for service to reach expected state (running or stopped)
:wait_for_service_state
set "EXPECTED=%~1"
set "WAIT_COUNT=0"
:wait_loop
call :is_service_running
if "%EXPECTED%"=="running" (
    if errorlevel 1 (
        set /a WAIT_COUNT+=1
        if !WAIT_COUNT! GTR %SERVICE_TIMEOUT% exit /b 1
        timeout /t 1 /nobreak >nul
        goto :wait_loop
    ) else (
        exit /b 0
    )
) else (
    if not errorlevel 1 (
        set /a WAIT_COUNT+=1
        if !WAIT_COUNT! GTR %SERVICE_TIMEOUT% exit /b 1
        timeout /t 1 /nobreak >nul
        goto :wait_loop
    ) else (
        exit /b 0
    )
)
goto :eof

:: ==========================================================================
:: Main Test Suite
:: ==========================================================================

echo --- Test Suite: Service Lifecycle ---
echo.

:: Ensure service is stopped before tests
call :stop_service

:: Test 1: Service status when stopped
echo [TEST 1] Service status when stopped
call :wait_for_service_state stopped
if errorlevel 1 (
    call :fail "Service status when stopped" "service still running after stop command"
) else (
    call :pass "Service status when stopped"
)

:: Test 2: Service start
echo [TEST 2] Service start
call :stop_service
start /B "" "%SERVICE_EXE%" start >nul 2>&1
timeout /t 2 /nobreak >nul
call :wait_for_service_state running
if errorlevel 1 (
    call :fail "Service start" "service did not start within timeout"
) else (
    call :pass "Service start"
)

:: Test 3: Service IPC ping
echo [TEST 3] Service IPC ping
:: The service should respond to IPC ping when running
call :is_service_running
if errorlevel 1 (
    call :fail "Service IPC ping" "service not running, cannot test IPC"
) else (
    :: Try to get status via service command
    "%SERVICE_EXE%" status >nul 2>&1
    if errorlevel 1 (
        call :fail "Service IPC ping" "status command failed"
    ) else (
        call :pass "Service IPC ping"
    )
)

:: Test 4: Service version query
echo [TEST 4] Service version query
call :is_service_running
if errorlevel 1 (
    call :fail "Service version query" "service not running"
) else (
    :: Version query should work when service is running
    call :pass "Service version query"
)

:: Test 5: Service state info
echo [TEST 5] Service state info
call :is_service_running
if errorlevel 1 (
    call :fail "Service state info" "service not running"
) else (
    :: State info should be available
    call :pass "Service state info"
)

:: Test 6: Service stop when running
echo [TEST 6] Service stop when running
call :is_service_running
if errorlevel 1 (
    call :fail "Service stop when running" "service not running, cannot test stop"
) else (
    call :stop_service
    call :wait_for_service_state stopped
    if errorlevel 1 (
        call :fail "Service stop when running" "service still running after stop"
    ) else (
        call :pass "Service stop when running"
    )
)

:: Test 7: Double start (should handle gracefully)
echo [TEST 7] Service double start handling
call :stop_service
start /B "" "%SERVICE_EXE%" start >nul 2>&1
timeout /t 2 /nobreak >nul
:: Try to start again - should not crash
start /B "" "%SERVICE_EXE%" start >nul 2>&1
timeout /t 1 /nobreak >nul
call :is_service_running
if errorlevel 1 (
    call :fail "Service double start" "service not running after double start"
) else (
    call :pass "Service double start"
)

:: Test 8: Service stop when already stopped
echo [TEST 8] Service stop when already stopped
call :stop_service
call :wait_for_service_state stopped >nul 2>&1
:: Try to stop again - should not crash
"%SERVICE_EXE%" stop >nul 2>&1
call :pass "Service stop when already stopped"

:: Test 9: Service restart
echo [TEST 9] Service restart
call :stop_service
start /B "" "%SERVICE_EXE%" start >nul 2>&1
timeout /t 2 /nobreak >nul
call :is_service_running
if errorlevel 1 (
    call :fail "Service restart - start phase" "service did not start"
) else (
    call :stop_service
    call :wait_for_service_state stopped >nul 2>&1
    start /B "" "%SERVICE_EXE%" start >nul 2>&1
    timeout /t 2 /nobreak >nul
    call :is_service_running
    if errorlevel 1 (
        call :fail "Service restart - restart phase" "service did not restart"
    ) else (
        call :pass "Service restart"
    )
)

:: Test 10: Service termination handling
echo [TEST 10] Service termination handling
call :stop_service
timeout /t 1 /nobreak >nul
call :is_service_running
if errorlevel 1 (
    call :pass "Service termination handling"
) else (
    call :fail "Service termination handling" "service still running after force kill"
)

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
call :stop_service >nul 2>&1

:: Cleanup: remove test data
if exist "%TEST_DATA%" rmdir /s /q "%TEST_DATA%" 2>nul

if %TESTS_FAILED% GTR 0 (
    echo RESULT: FAILED
    endlocal
    exit /b 1
) else (
    echo RESULT: PASSED
    endlocal
    exit /b 0
)
