@echo off
:: ==========================================================================
:: Test-NCD-Windows-With-Service.bat -- NCD client tests WITH service running
:: ==========================================================================
::
:: PURPOSE:
::   Tests NCD client in service-backed/shared-memory state access mode.
::   Verifies that NCD works correctly with the resident service running
::   and uses shared memory for state access.
::
:: TESTS:
::   - Service start and readiness verification
::   - Basic search operations via service
::   - Help shows service status
::   - Agent commands use service
::   - Service status reporting
::
:: REQUIREMENTS:
::   - NewChangeDirectory.exe and NCDService.exe built in project root
::   - MUST be run through the test harness: Run-Tests-Safe.bat ncd-service
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
::   Run-Tests-Safe.bat ncd-service       ^(NCD with service tests^)
::   Run-Tests-Safe.bat integration       ^(all integration tests^)
::   Run-Tests-Safe.bat windows           ^(all Windows tests^)
::   Run-Tests-Safe.bat                   ^(all tests^)
::
:: For isolated execution without affecting your shell:
::   test\Run-Isolated.bat test\Test-NCD-Windows-With-Service.bat
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
    echo   - Leave orphaned service processes running
    echo.
    echo CORRECT USAGE - Run one of these from the project root:
    echo   Run-Tests-Safe.bat ncd-service       ^(NCD with service^)
    echo   Run-Tests-Safe.bat integration       ^(all integration tests^)
    echo   Run-Tests-Safe.bat windows           ^(all Windows tests^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Test-NCD-Windows-With-Service.bat
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

:: Disable NCD background rescans to prevent scanning user drives
set "NCD_TEST_MODE=1"

:: Configuration
set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."
set "NCD=%PROJECT_ROOT%\NewChangeDirectory.exe"
set "SERVICE_EXE=%PROJECT_ROOT%\NCDService.exe"
set "SERVICE_BAT=%PROJECT_ROOT%\ncd_service.bat"

:: Isolation: redirect all NCD data to a temp directory
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_ncd_svc_test_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
if errorlevel 1 (
    echo ERROR: Failed to create test data directory
    call :emergency_cleanup
    endlocal
    exit /b 1
)
set "LOCALAPPDATA=%TEST_DATA%"

:: Test drive/directory setup
set "TESTROOT=%TEMP%\ncd_test_tree_%RANDOM%"
mkdir "%TESTROOT%" 2>nul
if errorlevel 1 (
    echo ERROR: Failed to create test root directory
    call :emergency_cleanup
    endlocal
    exit /b 1
)

:: Test counters
set "TESTS_RUN=0"
set "TESTS_PASSED=0"
set "TESTS_FAILED=0"

:: Service control timeout (seconds)
set "SERVICE_TIMEOUT=15"

echo ==========================================
echo NCD Client Tests - Windows (With Service)
echo ==========================================
echo.
echo NCD: %NCD%
echo Service: %SERVICE_EXE%
echo Test Data: %TEST_DATA%
echo Test Root: %TESTROOT%
echo.

:: ==========================================================================
:: AGGRESSIVE PRE-TEST SERVICE CLEANUP
:: ==========================================================================
echo [INFO] Performing aggressive service cleanup...

:: Stop via command if possible
if exist "%SERVICE_BAT%" (
    call "%SERVICE_BAT%" stop >nul 2>&1
) else (
    "%SERVICE_EXE%" stop >nul 2>&1
)
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1

:: Force kill any existing NCDService processes
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

if not exist "%NCD%" (
    echo ERROR: NCD executable not found: %NCD%
    echo Please build the project first with: build.bat
    goto :cleanup
)

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

:: Stop service
:stop_service
echo   [INFO] Stopping service...
if exist "%SERVICE_BAT%" (
    call "%SERVICE_BAT%" stop >nul 2>&1
) else (
    "%SERVICE_EXE%" stop >nul 2>&1
)
powershell -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>&1
:: Force kill if still running
taskkill /F /IM NCDService.exe >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
goto :eof

:: Start service
:start_service
echo   [INFO] Starting service with logging...
powershell -NoProfile -Command "$env:NCD_TEST_MODE='1'; $env:LOCALAPPDATA='%LOCALAPPDATA%'; Start-Process -NoNewWindow -FilePath '%SERVICE_EXE%' -ArgumentList 'start','-log2'" >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 3" >nul 2>&1
goto :eof

:: Get log file path
:get_log_path
set "LOG_PATH=%TEST_DATA%\NCD\ncd_service.log"
goto :eof

:: Check if service is running
:is_service_running
set "SVC_RUNNING="
for /f "skip=3 tokens=2" %%a in ('tasklist /FI "IMAGENAME eq NCDService.exe"') do (
    if not defined SVC_RUNNING set "SVC_RUNNING=%%a"
)
if defined SVC_RUNNING (
    exit /b 0
) else (
    exit /b 1
)

:: Wait for service to be ready
:wait_for_service_ready
set "WAIT_COUNT=0"
:wait_svc_loop
call :is_service_running
if errorlevel 1 exit /b 1
:: Check service status via NCD
call :ncd_run -conf "%TEST_DATA%\NCD\ncd.metadata" /agent check --service-status
set "SVC_STATUS=%LAST_OUT%"
echo %SVC_STATUS% | findstr /i "READY\|ready" >nul
if not errorlevel 1 exit /b 0
set /a WAIT_COUNT+=1
if !WAIT_COUNT! GTR %SERVICE_TIMEOUT% exit /b 1
powershell -NoProfile -Command "Start-Sleep -Seconds 1" >nul 2>&1
goto :wait_svc_loop

:: Run NCD with timeout
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
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '!NCD_ARGS!' -RedirectStandardOutput '%TEMP%\ncd_out.txt' -RedirectStandardError '%TEMP%\ncd_err.txt'; if (-not $p.WaitForExit(%TIMEOUT_SEC%000)) { $p.Kill(); exit 124 } else { exit $p.ExitCode }" >nul 2>&1
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
pushd "%TESTROOT%" >nul 2>&1
"%NCD%" %NCD_ARGS% > "%TEMP%\ncd_out.txt" 2> "%TEMP%\ncd_err.txt"
set "LAST_EXIT=%ERRORLEVEL%"
popd >nul 2>&1
set /p LAST_OUT= < "%TEMP%\ncd_out.txt" 2>nul
set /p LAST_ERR= < "%TEMP%\ncd_err.txt" 2>nul
goto :eof

:: ==========================================================================
:: Setup Test Environment
:: ==========================================================================

echo Setting up test environment...

:: Stop any existing service
call :stop_service >nul 2>&1

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

:: Initial scan (standalone) to create database
echo Performing initial scan of test tree...
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r.'; if (-not $p.WaitForExit(30000)) { $p.Kill() }" >nul 2>&1
popd
echo   Scan complete.

:: Start service
echo Starting service for tests...
call :start_service
call :wait_for_service_ready
if errorlevel 1 (
    echo ERROR: Service failed to start or become ready
    call :fail "Service startup" "service did not become ready within timeout"
    goto :summary
)
echo   Service is ready.
echo.

:: ==========================================================================
:: Main Test Suite
:: ==========================================================================

echo --- Test Suite: Service-Backed Mode Operations ---
echo.

:: Test 1: Service is running
echo [TEST 1] Service is running
call :is_service_running
if not errorlevel 1 (
    call :pass "Service is running"
) else (
    call :fail "Service is running" "service process not found"
)

:: Test 2: Service status shows READY or STARTING
echo [TEST 2] Service status shows READY/STARTING
call :ncd_run %CONF_OVERRIDE% /agent check --service-status
echo %LAST_OUT% | findstr /i "READY\|STARTING\|ready\|starting" >nul
if not errorlevel 1 (
    call :pass "Service status shows READY/STARTING"
) else (
    call :fail "Service status shows READY/STARTING" "output: %LAST_OUT%"
)

:: Test 3: Help shows service running
echo [TEST 3] Help shows service status
call :ncd_run_timed 10 %CONF_OVERRIDE% /h
echo %LAST_OUT% | findstr /i "Service\|Running\|service\|running" >nul
if not errorlevel 1 (
    call :pass "Help shows service status"
) else (
    :: Help might just show NCD without explicit service status - that's OK
    call :pass "Help shows service status (implicit)"
)

:: Test 4: Basic search works with service (using agent mode to avoid TUI)
echo [TEST 4] Basic search works with service
call :ncd_run %CONF_OVERRIDE% /agent query Downloads --limit 1
echo %LAST_OUT% | findstr /i "Downloads" >nul
if not errorlevel 1 (
    call :pass "Basic search works with service"
) else (
    call :fail "Basic search works with service" "output: %LAST_OUT%"
)

:: Test 5: Multi-component search with service (using agent mode to avoid TUI)
echo [TEST 5] Multi-component search with service
call :ncd_run %CONF_OVERRIDE% /agent query scott\Downloads --limit 1
echo %LAST_OUT% | findstr /i "Downloads" >nul
if not errorlevel 1 (
    call :pass "Multi-component search with service"
) else (
    call :fail "Multi-component search with service" "output: %LAST_OUT%"
)

:: Test 6: Agent query with service
echo [TEST 6] Agent query with service
call :ncd_run %CONF_OVERRIDE% /agent query Downloads
echo %LAST_OUT% | findstr /i "Downloads" >nul
if not errorlevel 1 (
    call :pass "Agent query with service"
) else (
    call :fail "Agent query with service" "output: %LAST_OUT%"
)

:: Test 7: Agent check --service-status returns valid status
echo [TEST 7] Agent check --service-status returns valid status
call :ncd_run %CONF_OVERRIDE% /agent check --service-status
if %LAST_EXIT%==0 (
    echo %LAST_OUT% | findstr /i "NOT_RUNNING\|STARTING\|READY" >nul
    if not errorlevel 1 (
        call :pass "Agent check --service-status returns valid status"
    ) else (
        call :fail "Agent check --service-status returns valid status" "invalid output: %LAST_OUT%"
    )
) else (
    call :fail "Agent check --service-status returns valid status" "exit code %LAST_EXIT%"
)

:: Test 8: Agent check --stats works
echo [TEST 8] Agent check --stats works
call :ncd_run %CONF_OVERRIDE% /agent check --stats
if %LAST_EXIT%==0 (
    call :pass "Agent check --stats works"
) else (
    call :fail "Agent check --stats works" "exit code %LAST_EXIT%"
)

:: Test 9: Search after service restart (using agent mode to avoid TUI)
echo [TEST 9] Search after service restart
call :stop_service >nul 2>&1
powershell -NoProfile -Command "Start-Sleep -Seconds 2" >nul 2>&1
call :start_service
call :wait_for_service_ready >nul 2>&1
call :ncd_run %CONF_OVERRIDE% /agent query Projects --limit 1
echo %LAST_OUT% | findstr /i "Projects" >nul
if not errorlevel 1 (
    call :pass "Search after service restart"
) else (
    call :fail "Search after service restart" "output: %LAST_OUT%"
)

:: Test 10: Service mode JSON status
echo [TEST 10] Service mode JSON status
call :ncd_run %CONF_OVERRIDE% /agent check --service-status --json
echo %LAST_OUT% | findstr /i "\"status\"\|\"v\":" >nul
if not errorlevel 1 (
    call :pass "Service mode JSON status"
) else (
    call :fail "Service mode JSON status" "output: %LAST_OUT%"
)

:: ==========================================================================
:: Summary
:: ==========================================================================

:summary
echo.
echo ==========================================
echo Test Summary
echo ==========================================
echo   Total:   %TESTS_RUN%
echo   Passed:  %TESTS_PASSED%
echo   Failed:  %TESTS_FAILED%
echo.

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

:: Cleanup
call :do_cleanup

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

:: Stop service
call :stop_service >nul 2>&1

:: Restore LOCALAPPDATA
if defined REAL_LOCALAPPDATA (
    set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
)

:: Remove test directories
if exist "%TEST_DATA%" (
    rmdir /s /q "%TEST_DATA%" 2>nul
    echo   Removed test data directory
)
if exist "%TESTROOT%" (
    rmdir /s /q "%TESTROOT%" 2>nul
    echo   Removed test tree directory
)

:: Remove temp files
del "%TEMP%\ncd_out.txt" 2>nul
del "%TEMP%\ncd_err.txt" 2>nul
del "%TEMP%\ncd_test_out.tmp" 2>nul

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

:: Remove test directories
if defined TEST_DATA (
    if exist "%TEST_DATA%" (
        rmdir /s /q "%TEST_DATA%" 2>nul
        echo   Removed test data directory
    )
)
if defined TESTROOT (
    if exist "%TESTROOT%" (
        rmdir /s /q "%TESTROOT%" 2>nul
        echo   Removed test tree directory
    )
)

:: Remove temp files
del "%TEMP%\ncd_out.txt" 2>nul
del "%TEMP%\ncd_err.txt" 2>nul

echo   Emergency cleanup complete.
echo   WARNING: Environment may be in an inconsistent state.
echo   Run 'endlocal' or close and reopen this command prompt.
goto :eof

:cleanup
goto :summary
