@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_initscan_test"
set "TESTROOT=%TEST_DATA%\root"

:: Clean up
rmdir /S /Q "%TEST_DATA%" 2>nul
mkdir "%TEST_DATA%\NCD"
mkdir "%TESTROOT%\Projects\alpha"

set "LOCALAPPDATA=%TEST_DATA%"
set "NCD_TEST_MODE=1"
set "CONF_PATH=%TEST_DATA%\NCD\ncd.metadata"
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_C.database"

pushd "%TESTROOT%"
echo === Running initial scan ===
"%NCD%" -conf "%CONF_PATH%" /r. /i /s /d "%DB_OVERRIDE%"
echo Exit code: %ERRORLEVEL%
popd

echo.
echo === Database files ===
dir "%TEST_DATA%\NCD\*.database" 2>nul

rmdir /S /Q "%TEST_DATA%" 2>nul
endlocal
