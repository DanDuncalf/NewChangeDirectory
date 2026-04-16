@echo off
:: ==========================================================================
:: Run-Tests-Safe.bat -- Safe Test Runner with Guaranteed Cleanup
:: ==========================================================================
::
:: PURPOSE:
::   This is the RECOMMENDED way to run NCD tests. It uses PowerShell's
::   try/finally blocks to guarantee cleanup runs even on Ctrl+C.
::
::   Unlike running batch files directly, this wrapper ensures that:
::   - Environment variables are always restored
::   - Temp directories are always cleaned up
::   - VHDs are always detached
::   - Service processes are always stopped
::
:: USAGE:
::   Run-Tests-Safe.bat [options]
::
:: OPTIONS:
::   all                Run all tests (default)
::   unit               Run only unit tests
::   integration        Run only integration tests
::   service            Run only service tests
::   ncd                Run only NCD standalone tests
::   ncd-service        Run only NCD with service tests
::   windows            Run all Windows tests (skip WSL)
::   wsl                Run only WSL tests
::   --skip-build       Skip build phase
::   --no-service       Skip tests requiring service
::   --quick            Skip fuzz/benchmark tests
::   --check            Check environment only
::   --repair           Repair corrupted environment
::   --help             Show this help
::
:: EXAMPLES:
::   Run-Tests-Safe.bat                    :: Run all tests
::   Run-Tests-Safe.bat unit               :: Run only unit tests
::   Run-Tests-Safe.bat integration        :: Run integration tests
::   Run-Tests-Safe.bat --check            :: Check if environment is clean
::   Run-Tests-Safe.bat --repair           :: Fix corrupted environment
::
:: SAFETY:
::   Even if you press Ctrl+C during tests, cleanup WILL run because
::   PowerShell's finally blocks execute even on interruption.
::
:: ==========================================================================

setlocal enabledelayedexpansion

:: Check if PowerShell is available
powershell -Command "exit 0" >nul 2>&1
if errorlevel 1 (
    echo ERROR: PowerShell is not available. This script requires PowerShell.
    exit /b 1
)

:: Parse arguments
set "TEST_SUITE=All"
set "EXTRA_ARGS="

:parse_args
if "%~1"=="" goto :done_parse

if /I "%~1"=="--help" goto :show_help
if /I "%~1"=="/help" goto :show_help
if /I "%~1"=="/h" goto :show_help
if /I "%~1"=="-h" goto :show_help

if /I "%~1"=="all" set "TEST_SUITE=All" & shift & goto :parse_args
if /I "%~1"=="unit" set "TEST_SUITE=Unit" & shift & goto :parse_args
if /I "%~1"=="integration" set "TEST_SUITE=Integration" & shift & goto :parse_args
if /I "%~1"=="service" set "TEST_SUITE=Service" & shift & goto :parse_args
if /I "%~1"=="ncd" set "TEST_SUITE=NcdStandalone" & shift & goto :parse_args
if /I "%~1"=="ncd-service" set "TEST_SUITE=NcdWithService" & shift & goto :parse_args
if /I "%~1"=="windows" set "TEST_SUITE=Windows" & shift & goto :parse_args
if /I "%~1"=="wsl" set "TEST_SUITE=Wsl" & shift & goto :parse_args

if /I "%~1"=="--skip-build" set "EXTRA_ARGS=!EXTRA_ARGS! -SkipBuild" & shift & goto :parse_args
if /I "%~1"=="--windows-only" set "EXTRA_ARGS=!EXTRA_ARGS! -WindowsOnly" & shift & goto :parse_args
if /I "%~1"=="--wsl-only" set "EXTRA_ARGS=!EXTRA_ARGS! -WslOnly" & shift & goto :parse_args
if /I "%~1"=="--no-service" set "EXTRA_ARGS=!EXTRA_ARGS! -NoService" & shift & goto :parse_args
if /I "%~1"=="--quick" set "EXTRA_ARGS=!EXTRA_ARGS! -Quick" & shift & goto :parse_args
if /I "%~1"=="--check" set "EXTRA_ARGS=!EXTRA_ARGS! -CheckEnvironment" & shift & goto :parse_args
if /I "%~1"=="--repair" set "EXTRA_ARGS=!EXTRA_ARGS! -RepairEnvironment" & shift & goto :parse_args

echo Unknown option: %~1
echo Use --help for usage
exit /b 1

:done_parse

echo.
echo ========================================
echo NCD Safe Test Runner
echo ========================================
echo.
echo Test Suite: %TEST_SUITE%
echo.
echo IMPORTANT: This runner uses PowerShell's try/finally blocks
echo            to guarantee cleanup even if you press Ctrl+C.
echo.
echo Starting in 3 seconds... (Press Ctrl+C now to cancel)
ping -n 4 127.0.0.1 >nul 2>&1

:: Run the PowerShell script
powershell -ExecutionPolicy Bypass -File "%~dp0Run-NcdTests.ps1" -TestSuite %TEST_SUITE% %EXTRA_ARGS%

set "EXIT_CODE=%ERRORLEVEL%"

:: Note: The PowerShell script handles all cleanup, but we do a
:: final verification here to be absolutely sure.
echo.
echo ========================================
echo Final Verification
echo ========================================
echo   LOCALAPPDATA: %LOCALAPPDATA%
echo   NCD_TEST_MODE: %NCD_TEST_MODE%

exit /b %EXIT_CODE%

:show_help
echo NCD Safe Test Runner
echo.
echo USAGE:
echo   Run-Tests-Safe.bat [suite] [options]
echo.
echo TEST SUITES:
echo   all          Run all tests (default)
echo   unit         Run only unit tests
echo   integration  Run only integration tests
echo   service      Run only service tests
echo   ncd          Run only NCD standalone tests
echo   ncd-service  Run only NCD with service tests
echo   windows      Run all Windows tests
echo   wsl          Run only WSL tests
echo.
echo OPTIONS:
echo   --skip-build     Skip build phase
echo   --windows-only   Run only Windows tests (skip WSL)
echo   --wsl-only       Run only WSL tests (skip Windows)
echo   --no-service     Skip tests requiring service
echo   --quick          Skip fuzz/benchmark tests
echo   --check          Check environment only
echo   --repair         Repair corrupted environment
echo   --help           Show this help
echo.
echo EXAMPLES:
echo   Run-Tests-Safe.bat              :: Run all tests
echo   Run-Tests-Safe.bat unit         :: Run only unit tests
echo   Run-Tests-Safe.bat --check      :: Check environment
echo   Run-Tests-Safe.bat --repair     :: Fix environment
echo.
echo SAFETY:
echo   This runner uses PowerShell's try/finally which CAN trap Ctrl+C
echo   and run cleanup code. Your environment WILL be restored.
exit /b 0
