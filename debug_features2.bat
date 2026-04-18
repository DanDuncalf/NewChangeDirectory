@echo off
setlocal enabledelayedexpansion
set "NCD_TEST_MODE=1"
set "NCD_UI_KEYS=ENTER"

set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"
set "TEST_DATA=%TEMP%\ncd_feat_debug_%RANDOM%"
mkdir "%TEST_DATA%" 2>nul
set "LOCALAPPDATA=%TEST_DATA%"
set "NCD=%CD%\NewChangeDirectory.exe"
set "VHD_PATH=%TEMP%\ncd_feat_vhd_%RANDOM%.vhdx"
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
if not exist %TESTDRIVE%:\ goto :use_tempdir
set "USE_VHD=1"
set "TESTROOT=%TESTDRIVE%:"
echo VHD mounted at %TESTDRIVE%:\
goto :create_tree

:use_tempdir
set "TESTROOT=%TEMP%\ncd_feat_tree_%RANDOM%"
mkdir "%TESTROOT%" 2>nul
for %%D in ("%TESTROOT%") do set "TESTDRIVE=%%~dD"
set "TESTDRIVE=%TESTDRIVE:~0,1%"
echo Using temp dir at %TESTROOT%

:create_tree
mkdir "%TESTROOT%\Projects\alpha\src\main" 2>nul
mkdir "%TESTROOT%\Projects\alpha\src\test" 2>nul
mkdir "%TESTROOT%\Projects\alpha\docs" 2>nul
mkdir "%TESTROOT%\Projects\beta\src" 2>nul
mkdir "%TESTROOT%\Projects\beta\build" 2>nul
mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul
mkdir "%TESTROOT%\Special Chars\dir with spaces" 2>nul

mkdir "%TEST_DATA%\NCD" 2>nul
powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB_OVERRIDE=%TEST_DATA%\NCD\ncd_test.database"

pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d \"%DB_OVERRIDE%\"'; if (-not $p.WaitForExit(15000)) { $p.Kill() }" >nul 2>&1
popd

echo === Agent tree V1 ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%\Projects" --json --depth 2 > "%TEMP%\v1_out.txt" 2>&1
type "%TEMP%\v1_out.txt"
findstr '"v":1' "%TEMP%\v1_out.txt" >nul && echo FOUND v1 || echo NOT FOUND v1
findstr '"tree":' "%TEMP%\v1_out.txt" >nul && echo FOUND tree || echo NOT FOUND tree

echo.
echo === Search spaces P1 ===
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d \"%DB_OVERRIDE%\" \"dir with spaces\"'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $p.WaitForExit(10000) | Out-Null"
if exist "%TEMP%\ncd_result.bat" (
    type "%TEMP%\ncd_result.bat"
    del "%TEMP%\ncd_result.bat"
) else (
    echo No result file
)

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
