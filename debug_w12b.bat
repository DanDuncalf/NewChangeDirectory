@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"

:: Find free drive
set "TESTDRIVE="
for %%D in (R S T U V W X Y Z) do if not exist "%%D:\" set "TESTDRIVE=%%D" & goto :found_drive
:found_drive
if "%TESTDRIVE%"=="" (
    echo No free drive letter
    exit /b 1
)

set "VHD_PATH=%TEMP%\ncd_agent_debug.vhd"
set "VHD_SIZE=16"

:: Create VHD
echo create vdisk file="%VHD_PATH%" maximum=%VHD_SIZE% type=expandable > "%TEMP%\agent_dp.txt"
echo select vdisk file="%VHD_PATH%" >> "%TEMP%\agent_dp.txt"
echo attach vdisk >> "%TEMP%\agent_dp.txt"
echo create partition primary >> "%TEMP%\agent_dp.txt"
echo format fs=ntfs quick label="NCDAgent" >> "%TEMP%\agent_dp.txt"
echo assign letter=%TESTDRIVE% >> "%TEMP%\agent_dp.txt"
diskpart /s "%TEMP%\agent_dp.txt" >nul 2>&1
timeout /t 2 >nul

set "TESTROOT=%TESTDRIVE%:"

mkdir "%TESTROOT%\Projects\alpha\src\main" 2>nul
mkdir "%TESTROOT%\Projects\alpha\src\test" 2>nul
mkdir "%TESTROOT%\Projects\alpha\docs" 2>nul
mkdir "%TESTROOT%\Projects\beta\src" 2>nul
mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul
mkdir "%TESTROOT%\Users\scott\Documents" 2>nul
mkdir "%TESTROOT%\Media\Photos2024" 2>nul
mkdir "%TESTROOT%\Media\Videos" 2>nul

set "NCD_TEST_MODE=1"
pushd "%TESTROOT%"
"%NCD%" /r. /i /s >nul 2>&1
popd

echo === Agent tree %TESTROOT%. depth 1 ===
"%NCD%" /agent tree "%TESTROOT%." --depth 1 > %TEMP%\w12b_d1.txt 2>&1
type %TEMP%\w12b_d1.txt

echo.
echo === Agent tree %TESTROOT%. depth 3 ===
"%NCD%" /agent tree "%TESTROOT%." --depth 3 > %TEMP%\w12b_d3.txt 2>&1
type %TEMP%\w12b_d3.txt

for /f %%a in ('type %TEMP%\w12b_d1.txt ^| find /c /v ""') do set T1=%%a
for /f %%a in ('type %TEMP%\w12b_d3.txt ^| find /c /v ""') do set T3=%%a
echo T1=%T1% T3=%T3%

:: Cleanup
echo select vdisk file="%VHD_PATH%" > "%TEMP%\agent_dp2.txt"
echo detach vdisk >> "%TEMP%\agent_dp2.txt"
diskpart /s "%TEMP%\agent_dp2.txt" >nul 2>&1
del "%TEMP%\agent_dp.txt" 2>nul
del "%TEMP%\agent_dp2.txt" 2>nul
del "%VHD_PATH%" 2>nul
del %TEMP%\w12b_d1.txt 2>nul
del %TEMP%\w12b_d3.txt 2>nul
endlocal
