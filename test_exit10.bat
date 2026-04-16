@echo off
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"

:: Test exit 0
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '/?'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(5000); exit $p.ExitCode"
echo ERRORLEVEL for /? = %ERRORLEVEL%

:: Test exit 1
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '/agent check --stats'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(5000); if (-not $ok) { $p.Kill(); exit 99 } else { exit $p.ExitCode }"
echo ERRORLEVEL for stats = %ERRORLEVEL%

:: Test timeout
powershell -NoProfile -Command "$psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'cmd'; $psi.Arguments = '/c ping -n 10 127.0.0.1'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(500); if (-not $ok) { $p.Kill(); exit 99 } else { exit $p.ExitCode }"
echo ERRORLEVEL for timeout = %ERRORLEVEL%
