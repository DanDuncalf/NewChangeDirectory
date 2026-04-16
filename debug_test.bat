@echo off
setlocal
set "NCD_TEST_MODE=1"
set "LOCALAPPDATA=%TEMP%\ncd_debug_test"
mkdir "%LOCALAPPDATA%\NCD" 2>nul
set "NCD=%CD%\NewChangeDirectory.exe"

:: Create test tree
set "TESTROOT=%TEMP%\ncd_debug_tree"
mkdir "%TESTROOT%\Projects\alpha\src" 2>nul
mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul
mkdir "%TESTROOT%\Deep\L1\L2" 2>nul
mkdir "%TESTROOT%\Special Chars\dir with spaces" 2>nul

:: Create metadata
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%LOCALAPPDATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"

set "CONF_OVERRIDE=-conf "%LOCALAPPDATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%LOCALAPPDATA%\NCD\ncd_test.database"

:: Scan
pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"
echo Scan exit code: %ERRORLEVEL%
popd

echo.
echo === Agent tree test ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%\Projects" --json --depth 2 > "%TEMP%\debug_tree.txt" 2>&1
type "%TEMP%\debug_tree.txt"
echo.
echo === Agent query test ===
"%NCD%" %CONF_OVERRIDE% /agent query scott --json > "%TEMP%\debug_query.txt" 2>&1
type "%TEMP%\debug_query.txt"
echo.
echo === Search spaces test ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" "dir with spaces" > "%TEMP%\debug_spaces.txt" 2>&1
type "%TEMP%\debug_spaces.txt"
echo.
echo === Exclusion test ===
"%NCD%" %CONF_OVERRIDE% -x "*/Deep"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10 > "%TEMP%\debug_excl.txt" 2>&1
type "%TEMP%\debug_excl.txt"
echo Exit code: %ERRORLEVEL%

rmdir /s /q "%LOCALAPPDATA%" 2>nul
rmdir /s /q "%TESTROOT%" 2>nul
endlocal
