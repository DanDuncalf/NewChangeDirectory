@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_i10_test3"
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

:: Initial scan via PowerShell
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"'; $ok = $p.WaitForExit(15000); if (-not $ok) { $p.Kill() }; exit $p.ExitCode" > %TEMP%\i10h_init.txt 2>&1
popd
echo === Initial scan (PowerShell) ===
type %TEMP%\i10h_init.txt
echo.

:: Add exclusion via PowerShell
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% -x ""*/Deep""'; $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill() }; exit $p.ExitCode" > %TEMP%\i10h_addx.txt 2>&1
echo === Add exclusion (PowerShell) ===
type %TEMP%\i10h_addx.txt
echo.

:: Rescan with exclusion via PowerShell
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"'; $ok = $p.WaitForExit(15000); if (-not $ok) { $p.Kill() }; exit $p.ExitCode" > %TEMP%\i10h_rescan.txt 2>&1
popd
echo === Rescan with exclusion (PowerShell) ===
type %TEMP%\i10h_rescan.txt
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
del %TEMP%\i10h_init.txt 2>nul
del %TEMP%\i10h_addx.txt 2>nul
del %TEMP%\i10h_rescan.txt 2>nul
endlocal
