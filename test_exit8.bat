@echo off
powershell -NoProfile -Command "$p = Start-Process -PassThru -NoNewWindow -FilePath 'cmd' -ArgumentList '/c exit 42'; $p | Wait-Process; Write-Host ('ExitCode=' + $p.ExitCode); exit $p.ExitCode"
echo ERRORLEVEL=%ERRORLEVEL%
