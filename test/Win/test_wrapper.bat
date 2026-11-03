@echo off
setlocal EnableExtensions

set "PROJECT_ROOT=%~dp0..\.."
set "NCD_BAT=%PROJECT_ROOT%\ncd.bat"
:: ncd.bat runs NewChangeDirectory.exe by name: test the build, not a deployed copy.
set "PATH=%PROJECT_ROOT%;%PATH%"
set "OUT_FILE=%TEMP%\ncd_wrapper_noargs_%RANDOM%.txt"
set "NCD_TEST_MODE=1"
set "NCD_UI_KEYS=ESC"

if not exist "%NCD_BAT%" (
    echo FAIL  WRP1  Wrapper accepts no arguments
    echo       Reason: ncd.bat not found
    echo Total: 1
    echo Passed: 0
    echo Failed: 1
    echo Skipped: 0
    echo RESULT: FAILED
    exit /b 1
)

call "%NCD_BAT%" >"%OUT_FILE%" 2>&1
findstr /I /C:"unexpected at this time" "%OUT_FILE%" >nul 2>&1
if not errorlevel 1 (
    echo FAIL  WRP1  Wrapper accepts no arguments
    echo       Reason: CMD parser error
    set "RESULT_CODE=1"
) else (
    echo PASS  WRP1  Wrapper accepts no arguments
    set "RESULT_CODE=0"
)

del /f /q "%OUT_FILE%" >nul 2>&1
if "%RESULT_CODE%"=="0" (
    echo Total: 1
    echo Passed: 1
    echo Failed: 0
    echo Skipped: 0
    echo RESULT: PASSED
) else (
    echo Total: 1
    echo Passed: 0
    echo Failed: 1
    echo Skipped: 0
    echo RESULT: FAILED
)

endlocal & exit /b %RESULT_CODE%
