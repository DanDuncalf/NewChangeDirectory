@echo off
setlocal

set "CONF_OVERRIDE=-conf "C:\test\ncd.metadata""

echo === PowerShell echo of args string ===
powershell -NoProfile -Command "$args = '%CONF_OVERRIDE% /r. /i /s /d ""C:\test\db.database""'; Write-Host $args"

echo.
echo === PowerShell echo with single quotes ===
powershell -NoProfile -Command "$args = '%CONF_OVERRIDE% /r. /i /s /d '""C:\test\db.database""''; Write-Host $args"

echo.
echo === PowerShell echo with explicit string ===
powershell -NoProfile -Command "$args = '-conf ""C:\test\ncd.metadata"" /r. /i /s /d ""C:\test\db.database""'; Write-Host $args"

endlocal
