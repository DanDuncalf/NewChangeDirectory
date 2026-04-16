@echo off
setlocal EnableDelayedExpansion

set "NCD=%CD%\NewChangeDirectory.exe"
set "TEST_DATA=%TEMP%\ncd_i10_test2"
set "TESTROOT=%TEST_DATA%\root"
set "DB_OVERRIDE=%TEST_DATA%\test.database"
set "CONF_OVERRIDE=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "RESULT_FILE=%TEMP%\ncd_result.bat"

:: Clean up
rmdir /S /Q "%TEST_DATA%" 2>nul
mkdir "%TEST_DATA%\NCD"
mkdir "%TESTROOT%\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10"
mkdir "%TESTROOT%\Other"

:: Create config with -1 rescan
set "NCD_UI_KEYS=DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,TEXT:-1,ENTER,ENTER"
"%NCD%" %CONF_OVERRIDE% /c >nul 2>&1
set "NCD_UI_KEYS="

:: Initial scan
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"'; if (-not $p.WaitForExit(15000)) { $p.Kill() }" >nul 2>&1
popd

:: Simulate test history buildup (like test_features.bat does)
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /d "%DB_OVERRIDE%" scott\Downloads'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1

:: Add exclusion
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% -x ""*/Deep""'; if (-not $p.WaitForExit(10000)) { $p.Kill() }" >nul 2>&1

:: Rescan with exclusion
pushd "%TESTROOT%"
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -WorkingDirectory '%TESTROOT%' -FilePath '%NCD%' -ArgumentList '%CONF_OVERRIDE% /r. /i /s /d "%DB_OVERRIDE%"'; if (-not $p.WaitForExit(15000)) { $p.Kill() }" >nul 2>&1
popd

:: Now test search for L10 using exact same method as test_features.bat
echo === Search L10 via PowerShell (exact test method) ===
powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $psi = New-Object System.Diagnostics.ProcessStartInfo; $psi.FileName = '%NCD%'; $psi.Arguments = '%CONF_OVERRIDE% /d ""%DB_OVERRIDE%"" L10'; $psi.UseShellExecute = $false; $p = [System.Diagnostics.Process]::Start($psi); $ok = $p.WaitForExit(10000); if (-not $ok) { $p.Kill(); exit 1 } else { exit $p.ExitCode }" >nul 2>&1
echo Exit code: %ERRORLEVEL%

:: Also test with direct batch call
echo.
echo === Search L10 via batch direct ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" L10 >nul 2>&1
echo Exit code: %ERRORLEVEL%

:: Agent tree
echo.
echo === Agent tree ===
"%NCD%" %CONF_OVERRIDE% /d "%DB_OVERRIDE%" /agent tree "%TESTROOT%" --flat --depth 11

:: Cleanup
rmdir /S /Q "%TEST_DATA%" 2>nul
endlocal
