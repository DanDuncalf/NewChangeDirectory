@echo off
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath 'cmd' -ArgumentList '/c exit 42'; $p.WaitForExit(); Write-Host ('ExitCode=' + $p.ExitCode); exit $p.ExitCode"
echo ERRORLEVEL=%ERRORLEVEL%
