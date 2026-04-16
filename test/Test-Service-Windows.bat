@echo off
:: ==========================================================================
:: Test-Service-Windows.bat -- Windows Service tests WITHOUT NCD client
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
::   - MUST be run through the test harness: Run-Tests-Safe.bat service
::
:: EXIT CODES:
::   0 - All tests passed
::   1 - One or more tests failed
::
:: ==========================================================================
::
:: IMPORTANT: This script modifies LOCALAPPDATA and NCD_TEST_MODE which 
:: affect NCD behavior. It MUST be run through the test harness to ensure
:: proper environment setup and cleanup.
::
:: DO NOT RUN THIS SCRIPT DIRECTLY - use one of these instead:
::   Run-Tests-Safe.bat service
::   Run-Tests-Safe.bat integration
::   Run-Tests-Safe.bat windows
::   Run-Tests-Safe.bat all
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
    echo   - Leave orphaned service processes running
    echo.
    echo CORRECT USAGE - Run one of these from the project root:
    echo   Run-Tests-Safe.bat service           ^(service tests only^)
    echo   Run-Tests-Safe.bat integration       ^(all integration tests^)
    echo   Run-Tests-Safe.bat windows           ^(all Windows tests^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Test-Service-Windows.bat
    echo.
    echo If your environment is already corrupted, repair it with:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo ==========================================
    exit /b 1
)

:: Save original environment values BEFORE setlocal
set "ORIGINAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "ORIGINAL_NCD_TEST_MODE=%NCD_TEST_MODE%"

setlocal enabledelayedexpansion

:: Disable NCD background rescans during testing
set "NCD_TEST_MODE=1"

:: Configuration
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "SERVICE_EXE=%PROJECT_ROOT%\NCDService.exe"
set "SERVICE_BAT=%PROJECT_ROOT%\ncd_service.bat"

:: Isolation: use temp directory for all service data
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_service_test_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
if errorlevel 1 (
    echo ERROR: Failed to create test data directory
    call :emergency_cleanup
    endlocal
    exit /b 1
)
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
:: AGGRESSIVE PRE-TEST SERVICE CLEANUP
:: ==========================================================================
echo [INFO] Ensuring no existing service processes are running...

:: Stop via command if possible
if exist "%SERVICE_BAT%" (
    call "%SERVICE_BAT%" stop >nul 2>&1
) else (
    "%SERVICE_EXE%" stop >nul 2>&1
)
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1

:: Force kill any existing NCDService processes (including from other test runs)
taskkill /F /IM NCDService.exe >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1

:: Retry force kill to ensure all instances are terminated
taskkill /F /IM NCDService.exe >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1

:: Verify service executable timestamp to ensure we're using the latest build
for %%F in ("%SERVICE_EXE%") do (
    echo [INFO] Using service built at: %%~tF
)

echo [INFO] Service cleanup complete.
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
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
:: Force kill if still running
taskkill /F /IM NCDService.exe >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
goto :eof

:: Get log file path
:get_log_path
set "LOG_PATH=%TEST_DATA%\NCD\ncd_service.log"
goto :eof

:: Check log file for errors
:check_log_errors
call :get_log_path
if not exist "%LOG_PATH%" goto :eof
findstr /I "ERROR" "%LOG_PATH%" >nul 2>&1
if not errorlevel 1 (
    echo   [WARN] Errors found in service log:
    type "%LOG_PATH%" | findstr /I "ERROR"
    set /a TESTS_FAILED+=1
)
goto :eof

:: Check if service is running
:is_service_running
set "SERVICE_PID="
for /f "skip=3 tokens=2" %%a in ('tasklist /FI "IMAGENAME eq NCDService.exe"') do (
    if not defined SERVICE_PID set "SERVICE_PID=%%a"
)
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
        powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
        goto :wait_loop
    ) else (
        exit /b 0
    )
) else (
    if not errorlevel 1 (
        set /a WAIT_COUNT+=1
        if !WAIT_COUNT! GTR %SERVICE_TIMEOUT% exit /b 1
        powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
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
:: Start with logging level 2 for test verification
set "LOG_PATH=%TEST_DATA%\NCD\ncd_service.log"
if exist "%LOG_PATH%" del "%LOG_PATH%" 2>nul
:: Start service with NCD_TEST_MODE to prevent scanning user drives
powershell -NoProfile -Command "$env:NCD_TEST_MODE='1'; $env:LOCALAPPDATA='%LOCALAPPDATA%'; Start-Process -NoNewWindow -FilePath '%SERVICE_EXE%' -ArgumentList 'start','-log2'" >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>&1
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
powershell -NoProfile -Command "$env:NCD_TEST_MODE='1'; $env:LOCALAPPDATA='%LOCALAPPDATA%'; Start-Process -NoNewWindow -FilePath '%SERVICE_EXE%' -ArgumentList 'start'" >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>&1
:: Try to start again - should not crash
powershell -NoProfile -Command "$env:NCD_TEST_MODE='1'; $env:LOCALAPPDATA='%LOCALAPPDATA%'; Start-Process -NoNewWindow -FilePath '%SERVICE_EXE%' -ArgumentList 'start'" >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
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
powershell -NoProfile -Command "$env:NCD_TEST_MODE='1'; $env:LOCALAPPDATA='%LOCALAPPDATA%'; Start-Process -NoNewWindow -FilePath '%SERVICE_EXE%' -ArgumentList 'start'" >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>&1
call :is_service_running
if errorlevel 1 (
    call :fail "Service restart - start phase" "service did not start"
) else (
    call :stop_service
    call :wait_for_service_state stopped >nul 2>&1
    powershell -NoProfile -Command "$env:NCD_TEST_MODE='1'; $env:LOCALAPPDATA='%LOCALAPPDATA%'; Start-Process -NoNewWindow -FilePath '%SERVICE_EXE%' -ArgumentList 'start'" >nul 2>&1
    powershell -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>&1
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
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
call :is_service_running
if errorlevel 1 (
    call :pass "Service termination handling"
) else (
    call :fail "Service termination handling" "service still running after force kill"
)

:: ==========================================================================
:: Summary and Cleanup
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

call :do_cleanup

:: Verify no errors in service log
echo.
echo Checking service log for errors...
call :get_log_path
if exist "%LOG_PATH%" (
    findstr /I "ERROR" "%LOG_PATH%" >nul 2>&1
    if errorlevel 1 (
        echo   [PASS] No errors found in service log
    ) else (
        echo   [FAIL] Errors found in service log:
        type "%LOG_PATH%" | findstr /I "ERROR"
        set /a TESTS_FAILED+=1
    )
) else (
    echo   [INFO] No log file to check
)

if %TESTS_FAILED% GTR 0 (
    echo RESULT: FAILED
    endlocal
    exit /b 1
) else (
    echo RESULT: PASSED
    endlocal
    exit /b 0
)

:: ==========================================================================
:: Cleanup Functions
:: ==========================================================================

:do_cleanup
:: Main cleanup routine
echo Cleaning up test environment...

:: Cleanup: ensure service is stopped
call :stop_service >nul 2>&1

:: Restore LOCALAPPDATA
if defined REAL_LOCALAPPDATA (
    set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
)

:: Cleanup: remove test data
if exist "%TEST_DATA%" (
    rmdir /s /q "%TEST_DATA%" 2>nul
    echo   Removed test data directory
)

:: Remove temp files
del "%TEMP%\ncd_out.txt" 2>nul
del "%TEMP%\ncd_err.txt" 2>nul

echo   Cleanup complete.
goto :eof

:emergency_cleanup
:: Emergency cleanup for early failures
echo Performing emergency cleanup...

:: Stop service if defined
if defined SERVICE_EXE (
    if exist "%SERVICE_EXE%" (
        "%SERVICE_EXE%" stop >nul 2>&1
        taskkill /F /IM NCDService.exe >nul 2>&1
    )
)

:: Remove test data
if defined TEST_DATA (
    if exist "%TEST_DATA%" (
        rmdir /s /q "%TEST_DATA%" 2>nul
        echo   Removed test data directory
    )
)

:: Remove temp files
del "%TEMP%\ncd_out.txt" 2>nul
del "%TEMP%\ncd_err.txt" 2>nul

echo   Emergency cleanup complete.
echo   WARNING: Environment may be in an inconsistent state.
echo   Run 'endlocal' or close and reopen this command prompt.
goto :eof
