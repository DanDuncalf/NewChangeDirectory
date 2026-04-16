@echo off
:: ==========================================================================
:: Build-And-Run-All-Tests.bat -- Safe Build and Test Runner
:: ==========================================================================
::
:: PURPOSE:
::   Builds NCD and runs all tests. Uses PowerShell internally for
::   guaranteed cleanup even on Ctrl+C.
::
::   This is one of TWO safe ways to run tests. Use this for full build+
::   test cycles. For running pre-built tests, use Run-All-Tests.bat
::
:: BUILD PHASE:
::   - Windows binaries (via build.bat)
::   - Windows test executables
::   - WSL binaries (via build.sh)
::   - WSL test executables
::
:: TEST PHASE:
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
::   Build-And-Run-All-Tests.bat [options]
::
:: OPTIONS:
::   --windows-only     Build and test Windows only
::   --wsl-only         Build and test WSL only
::   --no-service       Skip tests requiring service
::   --skip-unit-tests  Skip unit test phase
::   --quick            Skip fuzz, benchmarks, and extended tests
::   --help             Show this help
::
:: EXAMPLES:
::   Build-And-Run-All-Tests.bat              :: Full build and test
::   Build-And-Run-All-Tests.bat --windows-only  :: Windows only
::   Build-And-Run-All-Tests.bat --quick      :: Skip fuzz/benchmarks
::
:: EXIT CODES:
::   0 = All tests passed
::   1 = Some tests failed
::   2 = Critical error (build failure, etc.)
::
:: REQUIREMENTS:
::   - Visual Studio 2019/2022 (for Windows build)
::   - WSL with GCC installed (for WSL tests)
::   - PowerShell 5.1 or later
::
:: SEE ALSO:
::   Run-All-Tests.bat      :: Run tests without building
::   Run-Tests-Safe.bat     :: Run specific test suites
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
set "PS_ARGS=-TestSuite All"

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
echo NCD Safe Build and Test Runner
echo ========================================
echo.
echo This script uses PowerShell for guaranteed
echo cleanup even if you press Ctrl+C.
echo.

:: Run the PowerShell script with all the safety
echo Starting build and tests via PowerShell...
echo.
powershell -ExecutionPolicy Bypass -File "%~dp0Run-NcdTests.ps1" %PS_ARGS%

set "EXIT_CODE=%ERRORLEVEL%"

exit /b %EXIT_CODE%

:show_help
echo NCD Safe Build and Test Runner
echo.
echo USAGE:
echo   Build-And-Run-All-Tests.bat [options]
echo.
echo DESCRIPTION:
echo   Builds NCD and runs all tests.
echo   This is SAFE - uses PowerShell try/finally for guaranteed cleanup.
echo.
echo OPTIONS:
echo   --windows-only     Build and test Windows only (skip WSL)
echo   --wsl-only         Build and test WSL only (skip Windows)
echo   --no-service       Skip tests that require the service
echo   --skip-unit-tests  Skip unit test phase
echo   --quick            Skip fuzz tests and benchmarks
echo   --help             Show this help
echo.
echo EXAMPLES:
echo   Build-And-Run-All-Tests.bat              :: Full build and test cycle
echo   Build-And-Run-All-Tests.bat --windows-only  :: Windows only
echo   Build-And-Run-All-Tests.bat --quick      :: Skip fuzz/benchmarks
echo.
echo SAFETY:
echo   Ctrl+C is trapped via PowerShell finally blocks.
echo   Your environment WILL be restored.
echo.
echo RELATED SCRIPTS:
echo   Run-All-Tests.bat      :: Run tests without building
echo   Run-Tests-Safe.bat     :: Run specific test suites
echo.
echo NOTE: For specific integration tests during development,
echo use the individual test scripts in the test\ directory.
exit /b 0
