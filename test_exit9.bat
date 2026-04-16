@echo off
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"

:: Test 1: exit 1
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '/agent check --stats'; $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $p = [System.Diagnostics.Process]::Start($psi); $stdout = $p.StandardOutput.ReadToEnd(); $stderr = $p.StandardError.ReadToEnd(); $p.WaitForExit(5000); Write-Host ('Exit=' + $p.ExitCode); Write-Host ('Out=' + $stdout); Write-Host ('Err=' + $stderr); exit $p.ExitCode"
echo ERRORLEVEL=%ERRORLEVEL%

:: Test 2: timeout
echo.
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'cmd'; $psi.Arguments = '/c ping -n 10 127.0.0.1'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(1000); if (-not $ok) { $p.Kill(); Write-Host 'TIMED OUT'; exit 99 } else { exit $p.ExitCode }"
echo ERRORLEVEL=%ERRORLEVEL%
