@echo off
setlocal
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_test_group_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

set "TESTROOT=%TEMP%\ncd_test_tree_%RANDOM%"
mkdir "%TESTROOT%\Projects" 2>nul
mkdir "%TESTROOT%\Users" 2>nul
mkdir "%TESTROOT%\Media" 2>nul

set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_test.database"

mkdir "%TEST_DATA%\NCD" 2>nul
copy "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\ncd.metadata" >nul 2>&1
if errorlevel 1 powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"

set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1

:: Scan
"%NCD%" %CONF_OVERRIDE% /r. /d "%DB_OVERRIDE%" >nul 2>&1

:: Create groups
cd /d "%TESTROOT%\Projects"
"%NCD%" %CONF_OVERRIDE% /g @proj >nul 2>&1
cd /d "%TESTROOT%\Users"
"%NCD%" %CONF_OVERRIDE% /g @users >nul 2>&1

echo === H4: @proj ===
"%NCD%" %CONF_OVERRIDE% @proj

echo.
echo === H5: @users ===
"%NCD%" %CONF_OVERRIDE% @users

:: Cleanup
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TESTROOT%" 2>nul
