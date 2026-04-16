@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_i10_test"
set "TESTROOT=%TEST_DATA%\root"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

:: Clean up
rmdir /S /Q "%TEST_DATA%" 2>nul
mkdir "%TEST_DATA%\NCD"
mkdir "%TESTROOT%\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10"
mkdir "%TESTROOT%\Other"

:: Create config with -1 rescan
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
set "NCD_UI_KEYS="

:: Initial scan
pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%" > %TEMP%\i10_init.txt 2>&1
popd

echo === Initial scan output ===
type %TEMP%\i10_init.txt

echo.
echo === Exclusions before adding ===
"%NCD%" %CONF_OVERRIDE% -xl

:: Add exclusion
"%NCD%" %CONF_OVERRIDE% -x "*/Deep" > %TEMP%\i10_addx.txt 2>&1
echo.
echo === Add exclusion output ===
type %TEMP%\i10_addx.txt

echo.
echo === Exclusions after adding ===
"%NCD%" %CONF_OVERRIDE% -xl

:: Rescan with exclusion
pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%" > %TEMP%\i10_rescan.txt 2>&1
popd

echo.
echo === Rescan output ===
type %TEMP%\i10_rescan.txt

echo.
echo === Search L10 ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10 >nul 2>&1
echo Exit code: %ERRORLEVEL%

:: Show agent tree
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --flat --depth 11 > %TEMP%\i10_tree.txt 2>&1
echo.
echo === Agent tree output ===
type %TEMP%\i10_tree.txt

:: Cleanup
rmdir /S /Q "%TEST_DATA%" 2>nul
del %TEMP%\i10_init.txt 2>nul
del %TEMP%\i10_addx.txt 2>nul
del %TEMP%\i10_rescan.txt 2>nul
del %TEMP%\i10_tree.txt 2>nul
endlocal
