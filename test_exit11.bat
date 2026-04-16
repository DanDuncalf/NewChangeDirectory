@echo off
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"

:: Test exit 1 (no output from PowerShell)
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = 'nonexistent_xyz_42'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(5000); if (-not $ok) { $p.Kill(); exit 99 } else { exit $p.ExitCode }" >nul 2>&1
echo ERRORLEVEL for no match = %ERRORLEVEL%

:: Test exit 0 (no output from PowerShell)
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '/?'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(5000); if (-not $ok) { $p.Kill(); exit 99 } else { exit $p.ExitCode }" >nul 2>&1
echo ERRORLEVEL for /? = %ERRORLEVEL%
