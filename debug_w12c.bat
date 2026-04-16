@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TESTROOT=%TEMP%\ncd_w12c_test"

rmdir /S /Q "%TESTROOT%" 2>nul
mkdir "%TESTROOT%\Projects\alpha\src\main"
mkdir "%TESTROOT%\Projects\alpha\src\test"
mkdir "%TESTROOT%\Projects\alpha\docs"
mkdir "%TESTROOT%\Projects\beta\src"
mkdir "%TESTROOT%\Users\scott\Downloads"
mkdir "%TESTROOT%\Users\scott\Documents"
mkdir "%TESTROOT%\Media\Photos2024"
mkdir "%TESTROOT%\Media\Videos"

set "NCD_TEST_MODE=1"
pushd "%TESTROOT%"
"%NCD%" /r. /i /s >nul 2>&1
popd

echo === Agent tree %TESTROOT% depth 1 ===
"%NCD%" /agent tree "%TESTROOT%" --depth 1 > %TEMP%\w12c_d1.txt 2>&1
type %TEMP%\w12c_d1.txt

echo.
echo === Agent tree %TESTROOT% depth 3 ===
"%NCD%" /agent tree "%TESTROOT%" --depth 3 > %TEMP%\w12c_d3.txt 2>&1
type %TEMP%\w12c_d3.txt

for /f %%a in ('type %TEMP%\w12c_d1.txt ^| find /c /v ""') do set T1=%%a
for /f %%a in ('type %TEMP%\w12c_d3.txt ^| find /c /v ""') do set T3=%%a
echo T1=%T1% T3=%T3%

:: Also test drive root style (R:)
for %%D in ("%TESTROOT%") do set "DRV=%%~dD"
echo.
echo === Agent tree %DRV% depth 1 ===
"%NCD%" /agent tree "%DRV%" --depth 1 > %TEMP%\w12c_drv1.txt 2>&1
type %TEMP%\w12c_drv1.txt

echo.
echo === Agent tree %DRV% depth 3 ===
"%NCD%" /agent tree "%DRV%" --depth 3 > %TEMP%\w12c_drv3.txt 2>&1
type %TEMP%\w12c_drv3.txt

for /f %%a in ('type %TEMP%\w12c_drv1.txt ^| find /c /v ""') do set D1=%%a
for /f %%a in ('type %TEMP%\w12c_drv3.txt ^| find /c /v ""') do set D3=%%a
echo D1=%D1% D3=%D3%

rmdir /S /Q "%TESTROOT%" 2>nul
del %TEMP%\w12c_*.txt 2>nul
endlocal
