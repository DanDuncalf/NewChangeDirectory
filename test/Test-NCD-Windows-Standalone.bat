@echo off
:: ==========================================================================
:: Test-NCD-Windows-Standalone.bat -- NCD client tests on Windows WITHOUT service
:: ==========================================================================
::
:: PURPOSE:
::   Tests NCD client in standalone/disk-based state access mode.
::   Verifies that NCD works correctly without the resident service running.
::   Delegates to a PowerShell script for better process control and
::   timeout handling.
::
:: REQUIREMENTS:
::   - NewChangeDirectory.exe built in project root
::   - MUST be run through the test harness: Run-Tests-Safe.bat ncd
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
::   Run-Tests-Safe.bat ncd                 (NCD standalone tests)
::   Run-Tests-Safe.bat integration         (all integration tests)
::   Run-Tests-Safe.bat windows             (all Windows tests)
::   Run-Tests-Safe.bat                     (all tests)
::
:: For isolated execution without affecting your shell:
::   test\Run-Isolated.bat test\Test-NCD-Windows-Standalone.bat
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
    echo.
    echo CORRECT USAGE - Run one of these from the project root:
    echo   Run-Tests-Safe.bat ncd               ^(NCD standalone^)
    echo   Run-Tests-Safe.bat integration       ^(all integration tests^)
    echo   Run-Tests-Safe.bat windows           ^(all Windows tests^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Test-NCD-Windows-Standalone.bat
    echo.
    echo If your environment is already corrupted, repair it with:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo ==========================================
    exit /b 1
)

:: Delegate to PowerShell for better process control and timeout handling
powershell -ExecutionPolicy Bypass -Command "& '%~dp0Test-NCD-Windows-Standalone.ps1'"
exit /b %ERRORLEVEL%
