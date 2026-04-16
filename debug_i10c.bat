@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_i10_test"
set "TESTROOT=%TEST_DATA%\root"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

set "NCD_TEST_MODE=1"

echo === Agent tree (shows DB contents) ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --flat --depth 11

echo.
echo === Search L10 with debug ===
set "NCD_DEBUG=1"
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10 > %TEMP%\i10_search.txt 2>&1
set "NCD_DEBUG="
type %TEMP%\i10_search.txt

del %TEMP%\i10_search.txt 2>nul
endlocal
