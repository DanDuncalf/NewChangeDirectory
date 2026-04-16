@echo off
:: ==========================================================================
:: build_and_run_alltests.bat -- Build NCD and Run ALL Tests
:: ==========================================================================
::
:: This is the primary way to build and verify everything works.
:: It builds all binaries, then runs the complete test suite.
::
:: Uses PowerShell internally for guaranteed cleanup (Ctrl+C safe).
::
:: USAGE:
::   build_and_run_alltests.bat              :: Build and run all tests
::   build_and_run_alltests.bat --windows-only  :: Windows only
::   build_and_run_alltests.bat --wsl-only      :: WSL only
::   build_and_run_alltests.bat --quick         :: Skip fuzz/benchmarks
::   build_and_run_alltests.bat --no-service    :: Skip service tests
::   build_and_run_alltests.bat --help          :: Show help
::
:: ==========================================================================

:: Delegate to the Build-And-Run-All-Tests.bat runner
call "%~dp0Build-And-Run-All-Tests.bat" %*
exit /b %ERRORLEVEL%
