@echo off
powershell -NoProfile -Command "Start-Process -NoNewWindow -Wait -FilePath 'cmd' -ArgumentList '/c exit 1'; exit $LASTEXITCODE"
echo ERRORLEVEL=%ERRORLEVEL%
