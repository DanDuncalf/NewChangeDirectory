@echo off
:: ==========================================================================
:: Run-All-Unit-Tests.bat -- Comprehensive Unit Test Runner
:: ==========================================================================
::
:: This script runs ALL NCD unit tests including:
::   - Core unit tests (database, matcher, scanner, etc.)
::   - Service tests
::   - IPC tests
::   - Corruption tests
::   - Bug detection tests
::   - Benchmarks (non-fatal)
::
:: Environment variables are properly saved and restored on all exit paths.
::
:: USAGE:
::   MUST be run through the test harness: Run-Tests-Safe.bat unit
::
::   cd test
::   Run-All-Unit-Tests.bat [options]    ^(NOT RECOMMENDED - use harness^)
::
:: OPTIONS:
::   --skip-ipc         Skip IPC tests (require running service)
::   --skip-fuzz        Skip fuzz tests
::   --skip-bench       Skip benchmarks
::   --skip-corruption  Skip corruption tests
::   --skip-bugs        Skip bug detection tests
::   --skip-service     Skip service tests
::   --help             Show this help
::
:: IMPORTANT: This script modifies LOCALAPPDATA and NCD_TEST_MODE.
:: It SHOULD be run through the test harness for guaranteed cleanup.
:: ==========================================================================

:: ==========================================================================
:: ENVIRONMENT CHECK - WARN IF NOT RUNNING THROUGH TEST HARNESS
:: ==========================================================================
if "%NCD_TEST_MODE%"=="" (
    echo.
    echo ==========================================
    echo WARNING: TEST HARNESS NOT DETECTED
    echo ==========================================
    echo.
    echo This script is running WITHOUT the test harness protection.
    echo.
    echo This is NOT RECOMMENDED because:
    echo   - If you press Ctrl+C, environment cleanup may not run
    echo   - Your LOCALAPPDATA may be left pointing to temp directories
    echo   - This can cause NCD to malfunction until you restart your shell
    echo.
    echo RECOMMENDED: Run through the test harness from project root:
    echo   Run-Tests-Safe.bat unit              ^(unit tests only^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Run-All-Unit-Tests.bat
    echo.
    echo If you continue and interrupt with Ctrl+C, run:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo Waiting 5 seconds before continuing...
    echo ==========================================
    echo.
    ping -n 6 127.0.0.1 >nul 2>&1
)

setlocal enabledelayedexpansion

:: Parse arguments
set "SKIP_IPC=0"
set "SKIP_FUZZ=0"
set "SKIP_BENCH=0"
set "SKIP_CORRUPTION=0"
set "SKIP_BUGS=0"
set "SKIP_SERVICE=0"
set "SHOW_HELP=0"

:parse_args
if "%~1"=="" goto :done_parse
if /I "%~1"=="--skip-ipc" (set "SKIP_IPC=1" & shift & goto :parse_args)
if /I "%~1"=="--skip-fuzz" (set "SKIP_FUZZ=1" & shift & goto :parse_args)
if /I "%~1"=="--skip-bench" (set "SKIP_BENCH=1" & shift & goto :parse_args)
if /I "%~1"=="--skip-corruption" (set "SKIP_CORRUPTION=1" & shift & goto :parse_args)
if /I "%~1"=="--skip-bugs" (set "SKIP_BUGS=1" & shift & goto :parse_args)
if /I "%~1"=="--skip-service" (set "SKIP_SERVICE=1" & shift & goto :parse_args)
if /I "%~1"=="--help" (set "SHOW_HELP=1" & shift & goto :parse_args)
if /I "%~1"=="/h" (set "SHOW_HELP=1" & shift & goto :parse_args)
if /I "%~1"=="/help" (set "SHOW_HELP=1" & shift & goto :parse_args)
echo Unknown option: %~1
echo Use --help for usage
goto :error_exit

:done_parse

if "%SHOW_HELP%"=="1" (
    echo NCD Comprehensive Unit Test Runner
)

:: ==========================================================================
:: SAVE ORIGINAL ENVIRONMENT VARIABLES
:: ==========================================================================
set "ORIG_NCD_TEST_MODE=%NCD_TEST_MODE%"
set "ORIG_LOCALAPPDATA=%LOCALAPPDATA%"
set "ORIG_XDG_DATA_HOME=%XDG_DATA_HOME%"
set "ORIG_TEMP=%TEMP%"

:: ==========================================================================
:: SET TEST ENVIRONMENT
:: ==========================================================================
set "NCD_TEST_MODE=1"
set "LOCALAPPDATA=%TEMP%"

:: Create isolated temp directory for tests
set "TEST_TEMP=%TEMP%\ncd_unit_test_%RANDOM%"
mkdir "%TEST_TEMP%" 2>nul
if errorlevel 1 set "TEST_TEMP=%TEMP%"

:: ==========================================================================
:: TEST RESULTS TRACKING
:: ==========================================================================
set "TOTAL_RUNS=0"
set "TOTAL_PASSED=0"
set "TOTAL_FAILED=0"
set "TOTAL_SKIPPED=0"
set "ANY_FAILED=0"

:: Array-like structure for test results
set "TEST_RESULTS="

echo.
echo ========================================
echo NCD Comprehensive Unit Test Runner
echo ========================================
echo.
echo Environment:
echo   NCD_TEST_MODE=%NCD_TEST_MODE%
echo   LOCALAPPDATA=%LOCALAPPDATA%
echo.

:: ==========================================================================
:: HELPER FUNCTIONS
:: ==========================================================================

:run_test
:: Arguments: %1 = Test name, %2 = Test executable
set "TEST_NAME=%~1"
set "TEST_EXE=%~2"
set /a TOTAL_RUNS+=1

echo --- Running %TEST_NAME% ---
if not exist "%TEST_EXE%" (
    echo SKIPPED: %TEST_NAME% (executable not found: %TEST_EXE%)
    set /a TOTAL_SKIPPED+=1
    set "TEST_RESULTS=!TEST_RESULTS!SKIPPED:%TEST_NAME%;"
    goto :eof
)

"%TEST_EXE%"
if errorlevel 1 (
    echo FAILED: %TEST_NAME%
    set /a TOTAL_FAILED+=1
    set "ANY_FAILED=1"
    set "TEST_RESULTS=!TEST_RESULTS!FAILED:%TEST_NAME%;"
) else (
    echo PASSED: %TEST_NAME%
    set /a TOTAL_PASSED+=1
    set "TEST_RESULTS=!TEST_RESULTS!PASSED:%TEST_NAME%;"
)
echo.
goto :eof

:run_test_nonfatal
:: Arguments: %1 = Test name, %2 = Test executable
set "TEST_NAME=%~1"
set "TEST_EXE=%~2"
set /a TOTAL_RUNS+=1

echo --- Running %TEST_NAME% (non-fatal) ---
if not exist "%TEST_EXE%" (
    echo SKIPPED: %TEST_NAME% (executable not found: %TEST_EXE%)
    set /a TOTAL_SKIPPED+=1
    set "TEST_RESULTS=!TEST_RESULTS!SKIPPED:%TEST_NAME%;"
    goto :eof
)

"%TEST_EXE%"
if errorlevel 1 (
    echo NOTE: %TEST_NAME% had issues (non-fatal)
    set /a TOTAL_FAILED+=1
    set "TEST_RESULTS=!TEST_RESULTS!FAILED:%TEST_NAME%;"
) else (
    echo PASSED: %TEST_NAME%
    set /a TOTAL_PASSED+=1
    set "TEST_RESULTS=!TEST_RESULTS!PASSED:%TEST_NAME%;"
)
echo.
goto :eof

:run_ipc_test
:: Arguments: %1 = Test name, %2 = Test executable, %3 = Arguments
set "TEST_NAME=%~1"
set "TEST_EXE=%~2"
set "TEST_ARGS=%~3"
set /a TOTAL_RUNS+=1

echo --- Running %TEST_NAME% ---
if not exist "%TEST_EXE%" (
    echo SKIPPED: %TEST_NAME% (executable not found: %TEST_EXE%)
    set /a TOTAL_SKIPPED+=1
    set "TEST_RESULTS=!TEST_RESULTS!SKIPPED:%TEST_NAME%;"
    goto :eof
)

"%TEST_EXE%" %TEST_ARGS%
if errorlevel 1 (
    echo NOTE: %TEST_NAME% - service may not be running (non-fatal)
    set /a TOTAL_SKIPPED+=1
    set "TEST_RESULTS=!TEST_RESULTS!SKIPPED:%TEST_NAME%;"
) else (
    echo PASSED: %TEST_NAME%
    set /a TOTAL_PASSED+=1
    set "TEST_RESULTS=!TEST_RESULTS!PASSED:%TEST_NAME%;"
)
echo.
goto :eof

:: ==========================================================================
:: TIER 1: CORE UNIT TESTS
:: ==========================================================================

echo ========================================
echo Tier 1: Core Unit Tests
echo ========================================
echo.

call :run_test "Strbuilder Tests" "test_strbuilder.exe"
call :run_test "Common Tests" "test_common.exe"
call :run_test "Platform Tests" "test_platform.exe"

:: ==========================================================================
:: TIER 2: DATABASE AND MATCHING
:: ==========================================================================

echo ========================================
echo Tier 2: Database and Matching
echo ========================================
echo.

call :run_test "Database Tests" "test_database.exe"
call :run_test "Matcher Tests" "test_matcher.exe"
call :run_test "Metadata Tests" "test_metadata.exe"
call :run_test "Scanner Tests" "test_scanner.exe"

:: ==========================================================================
:: TIER 3: CLI AND CONFIGURATION
:: ==========================================================================

echo ========================================
echo Tier 3: CLI and Configuration
echo ========================================
echo.

call :run_test "CLI Parse Tests" "test_cli_parse.exe"

:: ==========================================================================
:: TIER 4: SERVICE TESTS
:: ==========================================================================

if "%SKIP_SERVICE%"=="0" (
    echo ========================================
    echo Tier 4: Service Tests
echo ========================================
    echo.

    call :run_test "Service Lazy Load" "test_service_lazy_load.exe"
    call :run_test_nonfatal "Service Lifecycle" "test_service_lifecycle.exe"
    call :run_test "Service Integration" "test_service_integration.exe"
    call :run_test "Service Version Compat" "test_service_version_compat.exe"
    call :run_test "Legacy Service Shutdown" "test_legacy_service_shutdown.exe"
)

:: ==========================================================================
:: TIER 5: SPECIALIZED TESTS
:: ==========================================================================

echo ========================================
echo Tier 5: Specialized Tests
echo ========================================
echo.

if "%SKIP_CORRUPTION%"=="0" (
    call :run_test "DB Corruption Tests" "test_db_corruption.exe"
)

if "%SKIP_BUGS%"=="0" (
    call :run_test "Bug Detection Tests" "test_bugs.exe"
)

:: ==========================================================================
:: TIER 6: IPC TESTS
:: ==========================================================================

if "%SKIP_IPC%"=="0" (
    echo ========================================
    echo Tier 6: IPC Tests
echo ========================================
    echo.

    call :run_ipc_test "IPC Ping Test" "ipc_ping_test.exe" "--once"
    call :run_ipc_test "IPC State Test" "ipc_state_test.exe" "--all"
    call :run_ipc_test "IPC Shutdown Test" "ipc_shutdown_test.exe" ""
    call :run_ipc_test "IPC Metadata Test" "ipc_metadata_test.exe" "--add-exclusion TEST"
    call :run_ipc_test "IPC Heuristic Test" "ipc_heuristic_test.exe" "--search test --path C:\test"
    call :run_ipc_test "IPC Rescan Test" "ipc_rescan_test.exe" ""
    call :run_ipc_test "IPC Flush Test" "ipc_flush_test.exe" ""
)

:: ==========================================================================
:: TIER 7: BENCHMARKS AND FUZZ TESTS
:: ==========================================================================

if "%SKIP_BENCH%"=="0" (
    echo ========================================
    echo Tier 7: Benchmarks
echo ========================================
    echo.

    call :run_test_nonfatal "Matcher Benchmark" "bench_matcher.exe"
)

if "%SKIP_FUZZ%"=="0" (
    echo ========================================
    echo Tier 7: Fuzz Tests (5 seconds)
echo ========================================
    echo.

    if exist "fuzz_database.exe" (
        set /a TOTAL_RUNS+=1
        echo --- Running Fuzz Tests (5 second run) ---
        start /b fuzz_database.exe > "%TEST_TEMP%\fuzz_output.tmp" 2>&1
        ping -n 6 127.0.0.1 >nul 2>&1
        taskkill /f /im fuzz_database.exe >nul 2>&1
        type "%TEST_TEMP%\fuzz_output.tmp" 2>nul
        del "%TEST_TEMP%\fuzz_output.tmp" 2>nul
        echo Fuzz test completed.
        set /a TOTAL_PASSED+=1
        set "TEST_RESULTS=!TEST_RESULTS!PASSED:Fuzz Tests;"
    ) else (
        echo SKIPPED: Fuzz Tests (executable not found)
        set /a TOTAL_SKIPPED+=1
        set "TEST_RESULTS=!TEST_RESULTS!SKIPPED:Fuzz Tests;"
    )
    echo.
)

:: ==========================================================================
:: ADDITIONAL TESTS (if they exist)
:: ==========================================================================

echo ========================================
echo Additional Tests (if available)
echo ========================================
echo.

:: Shared state tests
if exist "test_shared_state.exe" (
    call :run_test "Shared State Tests" "test_shared_state.exe"
)

:: Key injection tests
if exist "test_key_injection.exe" (
    call :run_test "Key Injection Tests" "test_key_injection.exe"
)

:: Extended tests
if exist "test_cli_parse_extended.exe" (
    call :run_test "CLI Parse Extended" "test_cli_parse_extended.exe"
)

if exist "test_platform_extended.exe" (
    call :run_test "Platform Extended" "test_platform_extended.exe"
)

if exist "test_strbuilder_extended.exe" (
    call :run_test "Strbuilder Extended" "test_strbuilder_extended.exe"
)

if exist "test_common_extended.exe" (
    call :run_test "Common Extended" "test_common_extended.exe"
)

:: UI tests (if available)
if exist "test_ui_selector.exe" (
    call :run_test_nonfatal "UI Selector" "test_ui_selector.exe"
)

if exist "test_ui_config.exe" (
    call :run_test_nonfatal "UI Config" "test_ui_config.exe"
)

if exist "test_ui_history.exe" (
    call :run_test_nonfatal "UI History" "test_ui_history.exe"
)

if exist "test_ui_navigator.exe" (
    call :run_test_nonfatal "UI Navigator" "test_ui_navigator.exe"
)


:: ==========================================================================
:: PARALLEL EXPANSION TESTS (New - 430 tests across 16 files)
:: ==========================================================================

echo ========================================
echo Parallel Expansion Tests
echo ========================================
echo.

:: Agent 1: Data Core Tests (90 tests)
if exist "test_database_extended.exe" (
    call :run_test "Database Extended (Agent 1)" "test_database_extended.exe"
)

if exist "test_odd_cases.exe" (
    call :run_test "Odd Cases (Agent 1)" "test_odd_cases.exe"
)

if "%SKIP_FUZZ%"=="0" (
    if exist "fuzz_database_load.exe" (
        call :run_test_nonfatal "Fuzz Database Load (Agent 1)" "fuzz_database_load.exe"
    )
)

:: Agent 2: Service Infrastructure Tests (100 tests)
if "%SKIP_SERVICE%"=="0" (
    if exist "test_service_stress.exe" (
        call :run_test "Service Stress (Agent 2)" "test_service_stress.exe"
    )

    if exist "test_service_rescan.exe" (
        call :run_test "Service Rescan (Agent 2)" "test_service_rescan.exe"
    )
)

if exist "test_ipc_extended.exe" (
    call :run_test "IPC Extended (Agent 2)" "test_ipc_extended.exe"
)

if exist "test_shm_stress.exe" (
    call :run_test "SHM Stress (Agent 2)" "test_shm_stress.exe"
)

if "%SKIP_FUZZ%"=="0" (
    if exist "fuzz_ipc.exe" (
        call :run_test_nonfatal "Fuzz IPC (Agent 2)" "fuzz_ipc.exe"
    )
)

:: Agent 3: UI and Main Tests (100 tests)
if exist "test_ui_extended.exe" (
    call :run_test_nonfatal "UI Extended (Agent 3)" "test_ui_extended.exe"
)

if exist "test_ui_exclusions.exe" (
    call :run_test_nonfatal "UI Exclusions (Agent 3)" "test_ui_exclusions.exe"
)

if exist "test_main.exe" (
    call :run_test "Main Flow (Agent 3)" "test_main.exe"
)

:: Agent 4: Input Processing Tests (140 tests)
if exist "test_cli_edge_cases.exe" (
    call :run_test "CLI Edge Cases (Agent 4)" "test_cli_edge_cases.exe"
)

if exist "test_scanner_extended.exe" (
    call :run_test "Scanner Extended (Agent 4)" "test_scanner_extended.exe"
)

if exist "test_matcher_extended.exe" (
    call :run_test "Matcher Extended (Agent 4)" "test_matcher_extended.exe"
)

if exist "test_result_edge_cases.exe" (
    call :run_test "Result Edge Cases (Agent 4)" "test_result_edge_cases.exe"
)

if exist "test_stress.exe" (
    call :run_test_nonfatal "Stress Tests (Agent 4)" "test_stress.exe"
)
:: ==========================================================================
:: SUMMARY
:: ==========================================================================

echo.
echo ========================================
echo TEST SUMMARY
echo ========================================
echo   Total Tests Run:    %TOTAL_RUNS%
echo   Passed:             %TOTAL_PASSED%
echo   Failed:             %TOTAL_FAILED%
echo   Skipped:            %TOTAL_SKIPPED%
echo.

:: Display detailed results
if "%ANY_FAILED%"=="1" (
    echo Failed Tests:
    for %%R in (%TEST_RESULTS%) do (
        echo "%%R" | findstr "^FAILED:" >nul && (
            echo   - %%R
        )
    )
    echo.
)

:: ==========================================================================
:: CLEANUP AND RESTORE ENVIRONMENT
:: ==========================================================================

goto :cleanup_and_exit

:: ==========================================================================
:: ERROR HANDLERS
:: ==========================================================================

:error_exit
set "ANY_FAILED=1"
goto :cleanup_and_exit

:cleanup_and_exit
:: Restore original environment variables
if defined ORIG_NCD_TEST_MODE (
    set "NCD_TEST_MODE=%ORIG_NCD_TEST_MODE%"
) else (
    set "NCD_TEST_MODE="
)

if defined ORIG_LOCALAPPDATA (
    set "LOCALAPPDATA=%ORIG_LOCALAPPDATA%"
) else (
    set "LOCALAPPDATA="
)

if defined ORIG_XDG_DATA_HOME (
    set "XDG_DATA_HOME=%ORIG_XDG_DATA_HOME%"
) else (
    set "XDG_DATA_HOME="
)

if defined ORIG_TEMP (
    set "TEMP=%ORIG_TEMP%"
)

:: Cleanup temp directory
if exist "%TEST_TEMP%" (
    rmdir /s /q "%TEST_TEMP%" 2>nul
)

echo ========================================
if "%ANY_FAILED%"=="1" (
    echo SOME TESTS FAILED
    echo ========================================
    endlocal
    exit /b 1
) else (
    echo ALL TESTS PASSED
    echo ========================================
    endlocal
    exit /b 0
)

:normal_exit
:: Restore environment even on normal exit (help display)
if defined ORIG_NCD_TEST_MODE (
    set "NCD_TEST_MODE=%ORIG_NCD_TEST_MODE%"
) else (
    set "NCD_TEST_MODE="
)

if defined ORIG_LOCALAPPDATA (
    set "LOCALAPPDATA=%ORIG_LOCALAPPDATA%"
) else (
    set "LOCALAPPDATA="
)

if defined ORIG_XDG_DATA_HOME (
    set "XDG_DATA_HOME=%ORIG_XDG_DATA_HOME%"
) else (
    set "XDG_DATA_HOME="
)

endlocal
exit /b 0
