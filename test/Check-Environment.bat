@echo off
:: ==========================================================================
:: Check-Environment.bat -- Check and fix NCD test environment
:: ==========================================================================
::
:: PURPOSE:
::   Diagnose and fix environment variable issues after interrupted tests.
::   Run this if NCD is behaving strangely after running tests.
::
:: DEPRECATION NOTICE:
::   This batch file is deprecated. Use Check-Environment.ps1 for better
::   diagnostics and repair capabilities:
::
::     .\Check-Environment.ps1         :: Check only
::     .\Check-Environment.ps1 -Repair :: Check and repair
::
:: USAGE (legacy):
::   test\Check-Environment.bat [fix]
::
::   Without arguments: Shows current environment state
::   With 'fix' argument: Attempts to fix environment issues
::
:: ==========================================================================

echo.
echo ==========================================
echo NCD Environment Check (Legacy Batch Version)
echo ==========================================
echo.
echo NOTE: For better diagnostics, use Check-Environment.ps1
echo.

:: Check if PowerShell is available and user hasn't explicitly requested legacy mode
if "%~1"=="--legacy" goto :legacy_mode

powershell -Command "exit 0" >nul 2>&1
if errorlevel 1 goto :legacy_mode

:: Use PowerShell version for better functionality
if "%~1"=="fix" (
    powershell -ExecutionPolicy Bypass -File "%~dp0..\Check-Environment.ps1" -Repair
) else (
    powershell -ExecutionPolicy Bypass -File "%~dp0..\Check-Environment.ps1"
)
exit /b %ERRORLEVEL%

:legacy_mode
:: Legacy batch implementation for systems without PowerShell

:: Check LOCALAPPDATA
echo Current LOCALAPPDATA:
echo   %LOCALAPPDATA%
echo.

:: Check if it looks like a temp directory (test isolation)
echo %LOCALAPPDATA% | findstr /I "temp\\ncd" >nul
if not errorlevel 1 (
    echo WARNING: LOCALAPPDATA points to a test temp directory!
    echo This means test isolation was not properly cleaned up.
    echo.
    echo Expected: C:\Users\[username]\AppData\Local
    echo Actual:   %LOCALAPPDATA%
    echo.
    
    if /I "%~1"=="fix" (
        echo Attempting to fix...
        for /f "tokens=*" %%a in ('echo %USERPROFILE%') do (
            set "LOCALAPPDATA=%%a\AppData\Local"
        )
        echo Fixed! New LOCALAPPDATA:
        echo   %LOCALAPPDATA%
        echo.
        echo You should close and reopen this command prompt to ensure
        echo all environment variables are properly reset.
    ) else (
        echo To fix this, run: %~nx0 fix
        echo Or close and reopen your command prompt.
    )
) else (
    echo LOCALAPPDATA looks correct.
)
echo.

:: Check NCD_TEST_MODE
echo Current NCD_TEST_MODE:
if defined NCD_TEST_MODE (
    echo   %NCD_TEST_MODE%
    if "%NCD_TEST_MODE%"=="1" (
        echo.
        echo WARNING: NCD_TEST_MODE is set to 1.
        echo This disables background rescans but is normally fine.
        echo Clear it with: set NCD_TEST_MODE=
    )
) else (
    echo   (not set)
)
echo.

:: Check for NCD database
echo Looking for NCD database files:
if exist "%LOCALAPPDATA%\NCD\ncd_*.database" (
    echo   Found database files:
    dir /b "%LOCALAPPDATA%\NCD\ncd_*.database" 2>nul
) else (
    echo   No database files found in %LOCALAPPDATA%\NCD\
    echo.
    echo If your databases are missing, they may have been:
    echo   - Deleted by accident
    echo   - Stored in a different location
    echo   - Not yet created (run 'ncd /r' to scan)
)
echo.

:: Check for orphaned test directories
echo Checking for orphaned test directories in TEMP:
set "ORPHAN_COUNT=0"
for /d %%D in ("%TEMP%\ncd_test_*" "%TEMP%\ncd_*_test_*") do (
    echo   Found: %%D
    set /a ORPHAN_COUNT+=1
)
if %ORPHAN_COUNT% GTR 0 (
    echo.
    echo WARNING: Found %ORPHAN_COUNT% orphaned test directories.
    if /I "%~1"=="fix" (
        echo Cleaning up...
        for /d %%D in ("%TEMP%\ncd_test_*" "%TEMP%\ncd_*_test_*") do (
            rmdir /s /q "%%D" 2>nul
            if errorlevel 1 (
                echo   Failed to remove: %%D
            ) else (
                echo   Removed: %%D
            )
        )
        echo Cleanup complete.
    ) else (
        echo To clean these up, run: %~nx0 fix
    )
) else (
    echo   No orphaned test directories found.
)
echo.

:: Summary
echo ==========================================
echo Summary
echo ==========================================
echo.
echo Environment check complete.
echo.
if /I "%~1"=="fix" (
    echo Fixes have been applied. Recommended actions:
    echo   1. Close and reopen your command prompt
    echo   2. Run 'ncd /?' to verify NCD works correctly
    echo   3. Run 'ncd /agent check --db-age' to check database
) else (
    echo To apply fixes, run: %~nx0 fix
    echo.
    echo For more comprehensive diagnostics, use:
    echo   .\Check-Environment.ps1
    echo   .\Check-Environment.ps1 -Repair
)
echo.

exit /b 0
