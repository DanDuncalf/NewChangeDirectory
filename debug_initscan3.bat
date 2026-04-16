@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_initscan_test3"
set "TESTROOT=%TEST_DATA%\root"

:: Clean up
rmdir /S /Q "%TEST_DATA%" 2>nul
mkdir "%TEST_DATA%\NCD"
mkdir "%TESTROOT%\Projects\alpha\src\main"
mkdir "%TESTROOT%\Projects\alpha\src\test"
mkdir "%TESTROOT%\Projects\alpha\docs"
mkdir "%TESTROOT%\Projects\beta\src"
mkdir "%TESTROOT%\Users\scott\Downloads"
mkdir "%TESTROOT%\Users\scott\Documents"
mkdir "%TESTROOT%\Media\Photos2024"
mkdir "%TESTROOT%\Media\Videos"

set "LOCALAPPDATA=%TEST_DATA%"
set "NCD_TEST_MODE=1"
set "CONF_PATH=%TEST_DATA%\NCD\ncd.metadata"
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_C.database"

pushd "%TESTROOT%"
echo === Running initial scan via PowerShell (exact test_features.bat style) ===
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '-conf "%CONF_PATH%" /r. /i /s /d "%DB_OVERRIDE%"'; $ok = $p.WaitForExit(30000); if (-not $ok) { $p.Kill(); Write-Host 'WARN: Initial scan timed out' }; exit $p.ExitCode"
echo Exit code: %ERRORLEVEL%
popd

echo.
echo === Database files ===
set "DB_DIR=%TEST_DATA%\NCD"
set "B2_FOUND=0"
if exist "%DB_DIR%" (
    for %%F in ("%DB_DIR%\ncd_*.database") do set "B2_FOUND=1"
)
echo B2_FOUND=%B2_FOUND%
dir "%DB_DIR%\*.database" 2>nul

rmdir /S /Q "%TEST_DATA%" 2>nul
endlocal
