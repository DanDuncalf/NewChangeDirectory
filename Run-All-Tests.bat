@echo off
:: ==========================================================================
:: Run-All-Tests.bat -- Safe Test Runner (No Build)
:: ==========================================================================
::
:: PURPOSE:
::   Runs all NCD tests WITHOUT building first. Uses PowerShell internally
::   for guaranteed cleanup even on Ctrl+C.
::
::   This is one of TWO safe ways to run tests. Use this when binaries are
::   already built. For building + testing, use Build-And-Run-All-Tests.bat
::
:: TESTS RUN:
::   Phase 1: Unit Tests (all test_*.exe executables)
::   Phase 2: Integration Tests (6 test suites)
::     - Windows Service tests (isolated)
::     - Windows NCD with service
::     - Windows NCD standalone
::     - WSL Service tests (isolated)
::     - WSL NCD with service
::     - WSL NCD standalone
::
:: SAFETY:
::   This script delegates to PowerShell which uses try/finally blocks.
::   Even if you press Ctrl+C during tests, cleanup WILL run and your
::   environment will be restored.
::
:: USAGE:
::   Run-All-Tests.bat [options]
::
:: OPTIONS:
::   --windows-only     Run only Windows tests
::   --wsl-only         Run only WSL tests
::   --no-service       Skip tests requiring service
::   --skip-unit-tests  Skip unit test phase
::   --quick            Skip fuzz, benchmarks, and extended tests
::   --help             Show this help
::
:: EXAMPLES:
::   Run-All-Tests.bat                    :: Run all tests (binaries must exist)
::   Run-All-Tests.bat --windows-only     :: Windows tests only
::   Run-All-Tests.bat --quick            :: Skip fuzz/benchmarks
::
:: EXIT CODES:
::   0 = All tests passed
::   1 = Some tests failed
::   2 = Critical error
::
:: SEE ALSO:
::   Build-And-Run-All-Tests.bat  -- Build then run all tests
::   Run-Tests-Safe.bat           -- Run specific test suites
::
:: ==========================================================================

setlocal enabledelayedexpansion

:: Check if PowerShell is available
powershell -Command "exit 0" >nul 2>&1
if errorlevel 1 (
    echo ERROR: PowerShell is not available. This script requires PowerShell.
    exit /b 1
)

:: Parse arguments and pass through to PowerShell
set "PS_ARGS=-TestSuite All -SkipBuild"

:parse_args
if "%~1"=="" goto :done_parse

if /I "%~1"=="--help" goto :show_help
if /I "%~1"=="/help" goto :show_help
if /I "%~1"=="/h" goto :show_help
if /I "%~1"=="-h" goto :show_help

if /I "%~1"=="--windows-only" (
    set "PS_ARGS=!PS_ARGS! -WindowsOnly"
    shift
    goto :parse_args
)
if /I "%~1"=="--wsl-only" (
    set "PS_ARGS=!PS_ARGS! -WslOnly"
    shift
    goto :parse_args
)
if /I "%~1"=="--no-service" (
    set "PS_ARGS=!PS_ARGS! -NoService"
    shift
    goto :parse_args
)
if /I "%~1"=="--skip-unit-tests" (
    set "PS_ARGS=!PS_ARGS! -SkipUnit"
    shift
    goto :parse_args
)
if /I "%~1"=="--quick" (
    set "PS_ARGS=!PS_ARGS! -Quick"
    shift
    goto :parse_args
)

echo Unknown option: %~1
echo Use --help for usage information
exit /b 1

:done_parse

echo.
echo ========================================
echo NCD Safe Test Runner (Run Only)
echo ========================================
echo.
echo This script uses PowerShell for guaranteed
echo cleanup even if you press Ctrl+C.
echo.

:: Run the PowerShell script with all the safety
echo Starting tests via PowerShell...
echo.
powershell -ExecutionPolicy Bypass -File "%~dp0Run-NcdTests.ps1" %PS_ARGS%

set "EXIT_CODE=%ERRORLEVEL%"

exit /b %EXIT_CODE%

:show_help
echo NCD Safe Test Runner (Run Only)
echo.
echo USAGE:
echo   Run-All-Tests.bat [options]
echo.
echo DESCRIPTION:
echo   Runs all NCD tests WITHOUT building first.
echo   This is SAFE - uses PowerShell try/finally for guaranteed cleanup.
echo.
echo OPTIONS:
echo   --windows-only     Run only Windows tests (skip WSL)
echo   --wsl-only         Run only WSL tests (skip Windows)
echo   --no-service       Skip tests that require the service
echo   --skip-unit-tests  Skip unit test phase
echo   --quick            Skip fuzz tests and benchmarks
echo   --help             Show this help
echo.
echo EXAMPLES:
echo   Run-All-Tests.bat                  :: Run all pre-built tests
echo   Run-All-Tests.bat --windows-only   :: Windows tests only
echo   Run-All-Tests.bat --quick          :: Skip fuzz/benchmarks
echo.
echo SAFETY:
echo   Ctrl+C is trapped via PowerShell finally blocks.
echo   Your environment WILL be restored.
echo.
echo RELATED SCRIPTS:
echo   Build-And-Run-All-Tests.bat  :: Build then run all tests
echo   Run-Tests-Safe.bat           :: Run specific test suites
echo.
echo NOTE: For specific integration tests during development,
echo use the individual test scripts in the test\ directory.
exit /b 0
