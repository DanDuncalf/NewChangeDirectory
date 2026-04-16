@echo off
setlocal enabledelayedexpansion

set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_test_data_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

set "TESTROOT=%TEMP%\ncd_test_tree_%RANDOM%"
mkdir "%TESTROOT%" 2>nul
for %%D in ("%TESTROOT%") do set "TESTDRIVE=%%~dD"
set "TESTDRIVE=%TESTDRIVE:~0,1%"

mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul
mkdir "%TESTROOT%\Projects\alpha" 2>nul

echo Using TESTROOT=%TESTROOT% (drive %TESTDRIVE%:)

mkdir "%TEST_DATA%\NCD" 2>nul
if exist "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" (
    copy "%REAL_LOCALAPPDATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\ncd.metadata" >nul 2>&1
) else (
    powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"
)

set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_test.database"
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"

:: Setup config
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
set "NCD_UI_KEYS=ENTER"

:: Rescan with debug output
echo.
echo === RESCAN ===
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $env:NCD_UI_KEYS='ENTER'; $p = Start-Process -PassThru -NoNewWindow -Wait -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /d "%DB_OVERRIDE%"'"
popd

echo.
echo === SEARCH (scott\Downloads) ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" scott\Downloads

echo.
echo === SEARCH (nonexistent_xyz_42) ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" nonexistent_xyz_42

echo.
echo === AGENT CHECK STATS ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent check --stats

:: Cleanup
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TESTROOT%" 2>nul
