@echo off
setlocal EnableDelayedExpansion
set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_w12_test"
set "TESTROOT=%TEST_DATA%\root"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""

set "NCD_TEST_MODE=1"

:: Clean up and create minimal tree
rmdir /S /Q "%TEST_DATA%" 2>nul
mkdir "%TEST_DATA%\NCD"
mkdir "%TESTROOT%\Projects\alpha"
mkdir "%TESTROOT%\Projects\beta"
mkdir "%TESTROOT%\Users\scott\Downloads"

:: Config
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
set "NCD_UI_KEYS="

:: Scan
pushd "%TESTROOT%"
"%NCD%" %CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%" >nul 2>&1
popd

:: Test with dot
echo === With dot ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%." --depth 1 > %TEMP%\w12_d1.txt 2>&1
type %TEMP%\w12_d1.txt
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%." --depth 3 > %TEMP%\w12_d3.txt 2>&1
type %TEMP%\w12_d3.txt

for /f %%a in ('type %TEMP%\w12_d1.txt ^| find /c /v ""') do set T1=%%a
for /f %%a in ('type %TEMP%\w12_d3.txt ^| find /c /v ""') do set T3=%%a
echo T1=%T1% T3=%T3%

:: Test without dot
echo.
echo === Without dot ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --depth 1 > %TEMP%\w12_nd1.txt 2>&1
type %TEMP%\w12_nd1.txt
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --depth 3 > %TEMP%\w12_nd3.txt 2>&1
type %TEMP%\w12_nd3.txt

for /f %%a in ('type %TEMP%\w12_nd1.txt ^| find /c /v ""') do set N1=%%a
for /f %%a in ('type %TEMP%\w12_nd3.txt ^| find /c /v ""') do set N3=%%a
echo N1=%N1% N3=%N3%

:: Cleanup
rmdir /S /Q "%TEST_DATA%" 2>nul
del %TEMP%\w12_d1.txt 2>nul
del %TEMP%\w12_d3.txt 2>nul
del %TEMP%\w12_nd1.txt 2>nul
del %TEMP%\w12_nd3.txt 2>nul
endlocal
