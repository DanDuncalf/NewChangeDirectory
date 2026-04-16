@echo off
setlocal
set "NCD_TEST_MODE=1"
set "LOCALAPPDATA=%TEMP%\ncd_debug_test2"
mkdir "%LOCALAPPDATA%\NCD" 2>nul
set "NCD=%CD%\NewChangeDirectory.exe"
set "TESTROOT=%TEMP%\ncd_debug_tree2"
mkdir "%TESTROOT%\Special Chars\dir with spaces" 2>nul

powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%LOCALAPPDATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"
set "CONF_OVERRIDE=-conf "%LOCALAPPDATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%LOCALAPPDATA%\NCD\ncd_test.database"

pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"
popd

echo === Search for dir with spaces ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" "dir with spaces" >nul 2>&1
echo Exit code: %ERRORLEVEL%
if exist "%TEMP%\ncd_result.bat" (
    type "%TEMP%\ncd_result.bat"
    del "%TEMP%\ncd_result.bat"
) else (
    echo No result file
)

rmdir /s /q "%LOCALAPPDATA%" 2>nul
rmdir /s /q "%TESTROOT%" 2>nul
endlocal
