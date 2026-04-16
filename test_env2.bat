@echo off
setlocal enabledelayedexpansion
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_test_data_env_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"

set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_test.database"

mkdir "%TEST_DATA%\NCD" 2>nul
if exist "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" (
    copy "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\ncd.metadata" >nul 2>&1
) else (
    powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"
)

:: Setup config
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
set "NCD_UI_KEYS=ENTER"

:: Build test database
mkdir "%TEST_DATA%\tree\alpha" 2>nul
"%NCD%" %CONF_OVERRIDE% /r. /d "%DB_OVERRIDE%" >nul 2>&1

echo === Direct search ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" nonexistent_xyz_42 >nul 2>&1
echo Exit code: %ERRORLEVEL%

echo === Start-Process search ===
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $env:NCD_UI_KEYS='ENTER'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /d "%DB_OVERRIDE%" nonexistent_xyz_42'; if (-not $p.WaitForExit(10000)) { $p.Kill(); exit 99 } else { exit $p.ExitCode }" >nul 2>&1
echo Exit code: %ERRORLEVEL%

:: Cleanup
rmdir /s /q "%TEST_DATA%" 2>nul
