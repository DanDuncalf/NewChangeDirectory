@echo off
setlocal enabledelayedexpansion
set "NCD_TEST_MODE=1"
set "NCD_UI_KEYS=ENTER"

set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_rem_debug_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"
set "NCD=%CD%\NewChangeDirectory.exe"
set "VHD_PATH=%TEMP%\ncd_rem_vhd_%RANDOM%.vhdx"
set "VHD_SIZE=16"
set "TESTDRIVE="
set "USE_VHD=0"
set "TESTROOT="

for %%L in (Z Y X W V U T S R Q P O N M L K J I H G) do (
    if not exist %%L:\ (
        set "TESTDRIVE=%%L"
        goto :found_letter
    )
)
goto :use_tempdir

:found_letter
net session >nul 2>&1
if errorlevel 1 goto :use_tempdir

(
echo create vdisk file="%VHD_PATH%" maximum=%VHD_SIZE% type=expandable
echo select vdisk file="%VHD_PATH%"
echo attach vdisk
echo create partition primary
echo format fs=ntfs quick label="NCDTest"
echo assign letter=%TESTDRIVE%
) > "%TEMP%\ncd_diskpart_create.txt"
diskpart /s "%TEMP%\ncd_diskpart_create.txt" >nul 2>&1
if errorlevel 1 (
    del "%TEMP%\ncd_diskpart_create.txt" 2>nul
    goto :use_tempdir
)
del "%TEMP%\ncd_diskpart_create.txt" 2>nul
if not exist !TESTDRIVE!:\ goto :use_tempdir
set "USE_VHD=1"
set "TESTROOT=!TESTDRIVE!:\"
echo VHD mounted at !TESTDRIVE!:\
goto :create_tree

:use_tempdir
set "TESTROOT=%TEMP%\ncd_rem_tree_%RANDOM%"
mkdir "%TESTROOT%" 2>nul
for %%D in ("%TESTROOT%") do set "TESTDRIVE=%%~dD"
set "TESTDRIVE=%TESTDRIVE:~0,1%"
echo Using temp dir at %TESTROOT%

:create_tree
mkdir "%TESTROOT%\Projects\alpha\src\main" 2>nul
mkdir "%TESTROOT%\Projects\alpha\docs" 2>nul
mkdir "%TESTROOT%\Projects\beta\src" 2>nul
mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul
mkdir "%TESTROOT%\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10" 2>nul
mkdir "%TESTROOT%\Special Chars\dir with spaces" 2>nul

mkdir "%TEST_DATA%\NCD" 2>nul
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_test.database"

pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d \"%DB_OVERRIDE%\"'; if (-not $p.WaitForExit(15000)) { $p.Kill() }" >nul 2>&1
popd

echo.
echo === V5: Tree depth on drive root ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --depth 1 > "%TEMP%\v5_d1.txt" 2>&1
echo depth1 output:
type "%TEMP%\v5_d1.txt%"
for /f %%a in ('type "%TEMP%\v5_d1.txt" ^| find /c /v ""') do echo depth1 lines=%%a
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --depth 2 > "%TEMP%\v5_d2.txt" 2>&1
echo depth2 output:
type "%TEMP%\v5_d2.txt%"
for /f %%a in ('type "%TEMP%\v5_d2.txt" ^| find /c /v ""') do echo depth2 lines=%%a

echo.
echo === I10a: Exclusion test ===
"%NCD%" %CONF_OVERRIDE% -x "*/Deep"
call :rescan_testroot
echo Searching for L10 after exclusion+rescan:
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10 > "%TEMP%\i10.txt" 2>&1
type "%TEMP%\i10.txt%"

echo.
echo === P1: Spaces in dir name ===
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d \"%DB_OVERRIDE%\" \"dir with spaces\"'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(10000) | Out-Null"
if exist "%TEMP%\ncd_result.bat" (
    type "%TEMP%\ncd_result.bat"
    del "%TEMP%\ncd_result.bat"
) else (
    echo No result file created
)

echo.
echo === W7: ls --dirs-only ===
"%NCD%" /agent ls "%TESTROOT%\Projects" --json --dirs-only > "%TEMP%\agent_ls3.txt" 2>&1
type "%TEMP%\agent_ls3.txt%"
findstr /C:"\"n\"" "%TEMP%\agent_ls3.txt" >nul
echo findstr_n_exit=%ERRORLEVEL%

echo.
echo === W12: tree depth on drive root (agent commands) ===
"%NCD%" /agent tree "%TESTROOT%" --depth 1 > "%TEMP%\agent_tree3a.txt" 2>&1
echo tree depth1:
type "%TEMP%\agent_tree3a.txt%"
for /f %%a in ('type "%TEMP%\agent_tree3a.txt" ^| find /c /v ""') do echo depth1 lines=%%a
"%NCD%" /agent tree "%TESTROOT%" --depth 3 > "%TEMP%\agent_tree3b.txt" 2>&1
echo tree depth3:
type "%TEMP%\agent_tree3b.txt%"
for /f %%a in ('type "%TEMP%\agent_tree3b.txt" ^| find /c /v ""') do echo depth3 lines=%%a

if "%USE_VHD%"=="1" (
    (
    echo select vdisk file="%VHD_PATH%"
    echo detach vdisk
    ) > "%TEMP%\ncd_diskpart_cleanup.txt"
    diskpart /s "%TEMP%\ncd_diskpart_cleanup.txt" >nul 2>&1
    del "%TEMP%\ncd_diskpart_cleanup.txt" 2>nul
    del "%VHD_PATH%" 2>nul
) else (
    if exist "%TESTROOT%" rmdir /s /q "%TESTROOT%" 2>nul
)
set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"
if exist "%TEST_DATA%" rmdir /s /q "%TEST_DATA%" 2>nul
endlocal
goto :eof

:rescan_testroot
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d \"%DB_OVERRIDE%\"'; if (-not $p.WaitForExit(15000)) { $p.Kill() }" >nul 2>&1
popd
goto :eof
