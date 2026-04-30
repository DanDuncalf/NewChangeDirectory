@echo off
:: Service Race Condition Tester - Integration wrapper for Windows
setlocal

cd /d "%~dp0\.."

if not exist service_race_tester.exe (
    echo ERROR: service_race_tester.exe not found. Run build-tests.bat first.
    exit /b 1
)

echo Running Service Race Condition Tester...
service_race_tester.exe --duration 30
set RC=%ERRORLEVEL%

if %RC% == 0 (
    echo [PASS] Service race test
    echo Total: 1
    echo Passed: 1
    echo Failed: 0
    echo Skipped: 0
    exit /b 0
) else (
    echo [FAIL] Service race test (exit code %RC%)
    echo Total: 1
    echo Passed: 0
    echo Failed: 1
    echo Skipped: 0
    exit /b 1
)
