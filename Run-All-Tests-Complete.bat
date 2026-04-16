@echo off
:: ==========================================================================
:: Run-All-Tests-Complete.bat -- Run ALL Pre-Built NCD Tests
:: ==========================================================================
::
:: This is the primary way to run all tests when binaries are already built.
:: Runs the complete test suite (unit + integration, Windows + WSL).
::
:: Uses PowerShell internally for guaranteed cleanup (Ctrl+C safe).
::
:: USAGE:
::   Run-All-Tests-Complete.bat                  :: Run all tests
::   Run-All-Tests-Complete.bat --windows-only   :: Windows only
::   Run-All-Tests-Complete.bat --wsl-only       :: WSL only
::   Run-All-Tests-Complete.bat --quick          :: Skip fuzz/benchmarks
::   Run-All-Tests-Complete.bat --no-service     :: Skip service tests
::   Run-All-Tests-Complete.bat --help           :: Show help
::
:: ==========================================================================

:: Delegate to the Run-All-Tests.bat runner
call "%~dp0Run-All-Tests.bat" %*
exit /b %ERRORLEVEL%
