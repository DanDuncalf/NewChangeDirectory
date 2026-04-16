@echo off
setlocal
set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_test_g2_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"

set "TESTROOT=%TEMP%\ncd_test_tree_%RANDOM%"
mkdir "%TESTROOT%\Users\scott\.hidden_config" 2>nul
attrib +h "%TESTROOT%\Users\scott\.hidden_config"

mkdir "%TESTROOT%\WinSys\System32" 2>nul
attrib +s "%TESTROOT%\WinSys"
attrib +s "%TESTROOT%\WinSys\System32"

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

echo === G2: /i hidden_config ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /i hidden_config

echo.
echo === G3: /s System32 ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /s System32

echo.
echo === Agent query ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent query hidden_config --json --limit 5

:: Cleanup
rmdir /s /q "%TEST_DATA%" 2>nul
rmdir /s /q "%TESTROOT%" 2>nul
