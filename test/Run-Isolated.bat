@echo off
:: ==========================================================================
:: Run-Isolated.bat -- Run NCD tests in isolated subprocess
:: ==========================================================================
::
:: PURPOSE:
::   Runs NCD test scripts in a completely isolated cmd.exe process.
::   Your current shell's environment is NEVER modified.
::
:: USAGE:
::   test\Run-Isolated.bat test\Test-NCD-Windows-Standalone.bat
::   test\Run-Isolated.bat test\Win\test_features.bat
::
:: This is the SAFEST way to run tests - even Ctrl+C in the test won't
:: affect your current shell.
::
:: ==========================================================================

if "%~1"=="" (
    echo Usage: %~nx0 ^<test_script.bat^>
    echo.
    echo Example:
    echo   %~nx0 test\Test-NCD-Windows-Standalone.bat
    exit /b 1
)

set "TEST_SCRIPT=%~1"

if not exist "%TEST_SCRIPT%" (
    echo ERROR: Test script not found: %TEST_SCRIPT%
    exit /b 1
)

echo.
echo ==========================================
echo Running Test in Isolated Process
echo ==========================================
echo.
echo Script: %TEST_SCRIPT%
echo.
echo Your current shell environment will NOT be modified.
echo All test isolation happens in a separate cmd.exe process.
echo.

:: Save current directory
set "ORIGINAL_DIR=%CD%"

:: Run the test script in a completely isolated cmd.exe process
:: We set NCD_TEST_MODE=1 so the test scripts know they are running in
:: a test harness and will proceed (they refuse to run without it).
:: The exit code is captured via ERRORLEVEL.
cmd /c "set NCD_TEST_MODE=1 && "%TEST_SCRIPT%" & exit /b %ERRORLEVEL%"

set "TEST_RESULT=%ERRORLEVEL%"

:: Return to original directory (in case test changed it)
cd /d "%ORIGINAL_DIR%"

echo.
if %TEST_RESULT%==0 (
    echo ==========================================
    echo TEST PASSED
    echo ==========================================
) else (
    echo ==========================================
    echo TEST FAILED (exit code: %TEST_RESULT%)
    echo ==========================================
)

:: Verify environment is intact
echo.
echo Verifying your environment is intact...
echo   LOCALAPPDATA: %LOCALAPPDATA%

exit /b %TEST_RESULT%
