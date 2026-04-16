@echo off
setlocal

set "TEST_DATA=C:\Users\Dan\AppData\Local\Temp\ncd_i10_test"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"

echo === PowerShell with batch-style expansion ===
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'python'; $psi.Arguments = '%CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000)"

echo.
echo === Direct NCD search via PowerShell (batch-style) ===
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
echo Exit code: %ERRORLEVEL%

endlocal
