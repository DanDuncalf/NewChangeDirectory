@echo off
:: ==========================================================================
:: build_and_run_alltests.bat -- Complete NCD Build and Test Runner
:: ==========================================================================
::
:: This script builds NCD for both Windows and WSL, then runs all 6 test
:: suites:
::
::   1. test_service_win.bat         - Windows Service tests (no client)
::   2. test_service_wsl.sh          - WSL Service tests (no client)
::   3. test_ncd_win_standalone.bat  - NCD Windows without service
::   4. test_ncd_win_with_service.bat- NCD Windows with service
::   5. test_ncd_wsl_standalone.sh   - NCD WSL without service
::   6. test_ncd_wsl_with_service.sh - NCD WSL with service
::
:: USAGE:
::   Run from project root: build_and_run_alltests.bat [options]
::
:: OPTIONS:
::   --windows-only     Run only Windows tests
::   --wsl-only         Run only WSL tests
::   --no-service       Skip tests that require the service
::   --skip-build       Skip building, just run tests
::   --help             Show this help message
::
:: REQUIREMENTS:
::   - Visual Studio 2019/2022 (for Windows build)
::   - WSL with GCC installed (for WSL tests)
::   - Administrator privileges (recommended for service tests)
::
:: ==========================================================================

setlocal enabledelayedexpansion

:: Parse command line arguments
set "WINDOWS_ONLY=0"
set "WSL_ONLY=0"
set "NO_SERVICE=0"
set "SKIP_BUILD=0"
set "SHOW_HELP=0"

:parse_args
if "%~1"=="" goto :done_parse
if /I "%~1"=="--windows-only" set "WINDOWS_ONLY=1" & shift & goto :parse_args
if /I "%~1"=="--wsl-only" set "WSL_ONLY=1" & shift & goto :parse_args
if /I "%~1"=="--no-service" set "NO_SERVICE=1" & shift & goto :parse_args
if /I "%~1"=="--skip-build" set "SKIP_BUILD=1" & shift & goto :parse_args
if /I "%~1"=="--help" set "SHOW_HELP=1" & shift & goto :parse_args
if /I "%~1"=="-h" set "SHOW_HELP=1" & shift & goto :parse_args
if /I "%~1"=="/h" set "SHOW_HELP=1" & shift & goto :parse_args
if /I "%~1"=="/help" set "SHOW_HELP=1" & shift & goto :parse_args
echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parse

if "%SHOW_HELP%"=="1" (
    type "%~f0" | findstr "^::" | findstr /V "^:::$" | findstr /V "^::=$"
    exit /b 0
)

:: Disable NCD background rescans during testing
set "NCD_TEST_MODE=1"

set "SCRIPT_DIR=%~dp0"
set "TEST_DIR=%SCRIPT_DIR%test"
set "TOTAL_PASSED=0"
set "TOTAL_FAILED=0"
set "TOTAL_SKIPPED=0"
set "ANY_FAILED=0"

echo.
echo ========================================
echo NCD Complete Build and Test Runner
echo ========================================
echo.
echo Test Plan:
if "%WSL_ONLY%"=="0" (
    echo   [Windows] test_service_win.bat         - Service tests (no client)
    if "%NO_SERVICE%"=="0" echo   [Windows] test_ncd_win_with_service.bat  - NCD with service
    echo   [Windows] test_ncd_win_standalone.bat    - NCD without service
)
if "%WINDOWS_ONLY%"=="0" (
    echo   [WSL]     test_service_wsl.sh          - Service tests (no client)
    if "%NO_SERVICE%"=="0" echo   [WSL]     test_ncd_wsl_with_service.sh   - NCD with service
    echo   [WSL]     test_ncd_wsl_standalone.sh     - NCD without service
)
echo.

:: ==========================================================================
:: BUILD PHASE
:: ==========================================================================

if "%SKIP_BUILD%"=="1" goto :skip_build

echo ========================================
echo BUILD PHASE
echo ========================================
echo.

:: Build Windows binaries
if "%WSL_ONLY%"=="0" (
    echo --- Building Windows binaries ---
    if exist "build.bat" (
        call build.bat
        if errorlevel 1 (
            echo ERROR: Windows build failed
            exit /b 1
        )
    ) else (
        echo WARNING: build.bat not found, skipping Windows build
    )
    echo.
    
    :: Build Windows test executables
    echo --- Building Windows test executables ---
    if exist "test\build-tests.bat" (
        pushd test
        call build-tests.bat
        if errorlevel 1 (
            echo WARNING: Windows test build had issues
        )
        popd
    ) else (
        echo WARNING: test\build-tests.bat not found
    )
    echo.
)

:: Build WSL binaries
if "%WINDOWS_ONLY%"=="0" (
    echo --- Building WSL binaries ---
    
    :: Check if WSL is available
    wsl --status >nul 2>&1
    if errorlevel 1 (
        echo WARNING: WSL is not available, skipping WSL build
        set "WSL_ONLY=1"
        if "%WINDOWS_ONLY%"=="1" (
            echo ERROR: Neither Windows nor WSL builds are available
            exit /b 1
        )
    ) else (
        :: Build via WSL
        set "DRIVE_LETTER=%SCRIPT_DIR:~0,1%"
        set "WSL_PATH=%SCRIPT_DIR:~2%"
        set "WSL_PATH=!WSL_PATH:\=/!"
        set "WSL_FULL_PATH=/mnt/!DRIVE_LETTER!!WSL_PATH!"
        
        if exist "build.sh" (
            wsl bash -c "cd '!WSL_FULL_PATH!' && chmod +x build.sh && ./build.sh"
            if errorlevel 1 (
                echo WARNING: WSL build had issues
            )
        ) else (
            echo WARNING: build.sh not found
        )
        
        :: Build WSL tests
        if exist "test\build-tests.sh" (
            wsl bash -c "cd '!WSL_FULL_PATH!/test' && chmod +x build-tests.sh && ./build-tests.sh"
            if errorlevel 1 (
                echo WARNING: WSL test build had issues
            )
        ) else (
            echo INFO: test\build-tests.sh not found, will use Makefile
            wsl bash -c "cd '!WSL_FULL_PATH!/test' && make"
        )
    )
    echo.
)

:skip_build

:: ==========================================================================
:: TEST PHASE
:: ==========================================================================

echo ========================================
echo TEST PHASE
echo ========================================
echo.

:: --------------------------------------------------------------------------
:: Windows Service Tests (no client)
:: --------------------------------------------------------------------------
if "%WSL_ONLY%"=="0" (
    call :run_test "Windows Service (no client)" "test\test_service_win.bat"
)

:: --------------------------------------------------------------------------
:: WSL Service Tests (no client)
:: --------------------------------------------------------------------------
if "%WINDOWS_ONLY%"=="0" (
    call :run_wsl_test "WSL Service (no client)" "test/test_service_wsl.sh"
)

:: --------------------------------------------------------------------------
:: Windows NCD with Service
:: --------------------------------------------------------------------------
if "%WSL_ONLY%"=="0" if "%NO_SERVICE%"=="0" (
    call :run_test "Windows NCD (with service)" "test\test_ncd_win_with_service.bat"
)

:: --------------------------------------------------------------------------
:: Windows NCD Standalone
:: --------------------------------------------------------------------------
if "%WSL_ONLY%"=="0" (
    call :run_test "Windows NCD (standalone)" "test\test_ncd_win_standalone.bat"
)

:: --------------------------------------------------------------------------
:: WSL NCD with Service
:: --------------------------------------------------------------------------
if "%WINDOWS_ONLY%"=="0" if "%NO_SERVICE%"=="0" (
    call :run_wsl_test "WSL NCD (with service)" "test/test_ncd_wsl_with_service.sh"
)

:: --------------------------------------------------------------------------
:: WSL NCD Standalone
:: --------------------------------------------------------------------------
if "%WINDOWS_ONLY%"=="0" (
    call :run_wsl_test "WSL NCD (standalone)" "test/test_ncd_wsl_standalone.sh"
)

:: ==========================================================================
:: SUMMARY
:: ==========================================================================

echo.
echo ========================================
echo TEST SUMMARY
echo ========================================
echo   Total tests run:    %TOTAL_RUNS%
echo   Passed:             %TOTAL_PASSED%
echo   Failed:             %TOTAL_FAILED%
echo   Skipped:            %TOTAL_SKIPPED%
echo.

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

:: ==========================================================================
:: Helper Functions
:: ==========================================================================

:run_test
:: Usage: call :run_test "Test Name" "script_path"
set "TEST_NAME=%~1"
set "TEST_SCRIPT=%~2"
set /a TOTAL_RUNS+=1

echo --- Running %TEST_NAME% ---
if exist "%~2" (
    call "%~2"
    if errorlevel 1 (
        echo [FAIL] %TEST_NAME%
        set /a TOTAL_FAILED+=1
        set "ANY_FAILED=1"
    ) else (
        echo [PASS] %TEST_NAME%
        set /a TOTAL_PASSED+=1
    )
) else (
    echo [SKIP] %TEST_NAME% - script not found: %~2
    set /a TOTAL_SKIPPED+=1
)
echo.
goto :eof

:run_wsl_test
:: Usage: call :run_wsl_test "Test Name" "script_path"
set "TEST_NAME=%~1"
set "TEST_SCRIPT=%~2"
set /a TOTAL_RUNS+=1

echo --- Running %TEST_NAME% ---

:: Check if WSL is available
wsl --status >nul 2>&1
if errorlevel 1 (
    echo [SKIP] %TEST_NAME% - WSL not available
    set /a TOTAL_SKIPPED+=1
    goto :eof
)

:: Convert path to WSL format
set "DRIVE_LETTER=%SCRIPT_DIR:~0,1%"
set "WSL_PATH=%SCRIPT_DIR:~2%"
set "WSL_PATH=!WSL_PATH:\=/!"
set "WSL_FULL_PATH=/mnt/!DRIVE_LETTER!!WSL_PATH!"
set "WSL_SCRIPT=!WSL_FULL_PATH!/%TEST_SCRIPT:\=/%"

:: Run test in WSL
wsl bash -c "export NCD_TEST_MODE=1; cd '!WSL_FULL_PATH!' && chmod +x '!WSL_SCRIPT!' && '!WSL_SCRIPT!'"
if errorlevel 1 (
    echo [FAIL] %TEST_NAME%
    set /a TOTAL_FAILED+=1
    set "ANY_FAILED=1"
) else (
    echo [PASS] %TEST_NAME%
    set /a TOTAL_PASSED+=1
)
echo.
goto :eof
