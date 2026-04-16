@echo off
setlocal
set "LOCALAPPDATA=C:\TEST_LOCAL_APPDATA"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $p = Start-Process -PassThru -NoNewWindow -Wait -FilePath 'cmd' -ArgumentList '/c echo LOCALAPPDATA=%LOCALAPPDATA%'"
