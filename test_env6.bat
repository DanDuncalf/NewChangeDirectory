@echo off
setlocal
set "LOCALAPPDATA=C:\TEST_LOCAL_APPDATA"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = 'cmd'; $psi.Arguments = '/c echo LOCALAPPDATA=%LOCALAPPDATA%'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $null = $p.WaitForExit(5000)"
