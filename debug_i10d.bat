@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_i10_test"
set "TESTROOT=%TEST_DATA%\root"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

set "LOCALAPPDATA=%LOCALAPPDATA%"
set "NCD_TEST_MODE=1"

echo === Search via PowerShell (exact test command) ===
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d ""%DB_OVERRIDE%"" L10'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
echo Exit code: %ERRORLEVEL%

endlocal
