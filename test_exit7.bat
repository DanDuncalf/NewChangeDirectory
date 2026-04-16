@echo off
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'cmd'; $psi.Arguments = '/c exit 42'; $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(); Write-Host ('ExitCode=' + $p.ExitCode); exit $p.ExitCode"
echo ERRORLEVEL=%ERRORLEVEL%
