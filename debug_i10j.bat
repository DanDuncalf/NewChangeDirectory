@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_i10_test5"
set "TESTROOT=%TEST_DATA%\root"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

set "NCD_TEST_MODE=1"

:: Clean up
rmdir /S /Q "%TEST_DATA%" 2>nul
mkdir "%TEST_DATA%\NCD"
mkdir "%TESTROOT%\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10"
mkdir "%TESTROOT%\Other"

:: Create config with -1 rescan
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
set "NCD_UI_KEYS="

:: Initial scan DIRECT from batch
pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%" > %TEMP%\i10j_init.txt 2>&1
popd
echo === Initial scan (direct) ===
type %TEMP%\i10j_init.txt
echo.

:: Add exclusion DIRECT from batch
"%NCD%" %CONF_OVERRIDE% -x "*/Deep" > %TEMP%\i10j_addx.txt 2>&1
echo === Add exclusion (direct) ===
type %TEMP%\i10j_addx.txt
echo.

:: Rescan with exclusion DIRECT from batch
pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%" > %TEMP%\i10j_rescan.txt 2>&1
popd
echo === Rescan with exclusion (direct) ===
type %TEMP%\i10j_rescan.txt
echo.

:: Agent tree
echo === Agent tree ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --flat --depth 11
echo.

:: Search L10
echo === Search L10 ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10 >nul 2>&1
echo Exit code: %ERRORLEVEL%

:: Cleanup
rmdir /S /Q "%TEST_DATA%" 2>nul
del %TEMP%\i10j_init.txt 2>nul
del %TEMP%\i10j_addx.txt 2>nul
del %TEMP%\i10j_rescan.txt 2>nul
endlocal
