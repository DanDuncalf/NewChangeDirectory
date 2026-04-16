@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_w12d_test"
set "TESTROOT=%TEST_DATA%\root"

:: Clean up
rmdir /S /Q "%TEST_DATA%" 2>nul
mkdir "%TEST_DATA%\NCD"
mkdir "%TESTROOT%\Projects\alpha\src\main"
mkdir "%TESTROOT%\Projects\alpha\src\test"
mkdir "%TESTROOT%\Projects\alpha\docs"
mkdir "%TESTROOT%\Projects\beta\src"
mkdir "%TESTROOT%\Users\scott\Downloads"
mkdir "%TESTROOT%\Users\scott\Documents"
mkdir "%TESTROOT%\Media\Photos2024"
mkdir "%TESTROOT%\Media\Videos"

set "LOCALAPPDATA=%TEST_DATA%"
set "NCD_TEST_MODE=1"

:: Scan from TESTROOT
pushd "%TESTROOT%"
"%NCD%" /r. /i /s > %TEMP%\w12d_scan.txt 2>&1
popd

echo === Scan output ===
type %TEMP%\w12d_scan.txt
echo.

:: Check if database exists
echo === Database files ===
dir "%TEST_DATA%\NCD\*.database" 2>nul
echo.

:: Now run agent tree on TESTROOT
echo === Agent tree depth 1 ===
"%NCD%" /agent tree "%TESTROOT%" --depth 1 > %TEMP%\w12d_d1.txt 2>&1
type %TEMP%\w12d_d1.txt

echo.
echo === Agent tree depth 3 ===
"%NCD%" /agent tree "%TESTROOT%" --depth 3 > %TEMP%\w12d_d3.txt 2>&1
type %TEMP%\w12d_d3.txt

for /f %%a in ('type %TEMP%\w12d_d1.txt ^| find /c /v ""') do set T1=%%a
for /f %%a in ('type %TEMP%\w12d_d3.txt ^| find /c /v ""') do set T3=%%a
echo T1=%T1% T3=%T3%

:: Also try drive letter
for %%D in ("%TESTROOT%") do set "DRV=%%~dD"
echo.
echo === Agent tree drive %DRV% depth 1 ===
"%NCD%" /agent tree "%DRV%" --depth 1 > %TEMP%\w12d_drv1.txt 2>&1
type %TEMP%\w12d_drv1.txt

echo.
echo === Agent tree drive %DRV% depth 3 ===
"%NCD%" /agent tree "%DRV%" --depth 3 > %TEMP%\w12d_drv3.txt 2>&1
type %TEMP%\w12d_drv3.txt

for /f %%a in ('type %TEMP%\w12d_drv1.txt ^| find /c /v ""') do set D1=%%a
for /f %%a in ('type %TEMP%\w12d_drv3.txt ^| find /c /v ""') do set D3=%%a
echo D1=%D1% D3=%D3%

rmdir /S /Q "%TEST_DATA%" 2>nul
del %TEMP%\w12d_*.txt 2>nul
endlocal
