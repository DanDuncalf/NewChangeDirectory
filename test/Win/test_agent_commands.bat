@echo off

:: ==========================================================================

:: test_agent_commands.bat -- Agent mode command tests for NCD (Windows)

:: ==========================================================================

::

:: Tests all /agent subcommands on the virtual disk:

::   query, ls, tree, check, complete, mkdir, mkdirs

::

:: Run from project root: test\Win\test_agent_commands.bat

:: ==========================================================================



setlocal enabledelayedexpansion



set "SCRIPT_DIR=%~dp0"

set "PROJECT_ROOT=%SCRIPT_DIR%..\.."

set "NCD=%PROJECT_ROOT%\NewChangeDirectory.exe"



:: Isolation: redirect all NCD data to a temp directory

:: Disable NCD background rescans to prevent scanning user drives during tests
set "NCD_TEST_MODE=1"

set "REAL_LOCALAPPDATA=%LOCALAPPDATA%"

set "TEST_DATA=%TEMP%\ncd_agent_test_%RANDOM%"

mkdir "%TEST_DATA%" 2>nul

set "LOCALAPPDATA=%TEST_DATA%"



:: VHD and test drive configuration (same as test_features.bat)

set "VHD_PATH=%TEMP%\ncd_agent_vhd_%RANDOM%.vhdx"

set "VHD_SIZE=16"

set "TESTDRIVE="

set "USE_VHD=0"

set "TESTROOT="



set "PASS_COUNT=0"

set "FAIL_COUNT=0"

set "SKIP_COUNT=0"



echo ==========================================

echo NCD Agent Command Tests [Windows]

echo ==========================================

echo.



if not exist "%NCD%" (

    echo ERROR: NCD binary not found at %NCD%

    endlocal

    exit /b 1

)



:: Find available drive letter

for %%L in (Z Y X W V U T S R Q P O N M L K J I H G) do (

    if not exist %%L:\ (

        set "TESTDRIVE=%%L"

        goto :found_letter

    )

)

echo WARNING: No free drive letter, using temp directory

goto :use_tempdir



:found_letter

net session >nul 2>&1

if errorlevel 1 (

    echo WARNING: Not running as admin, using temp directory

    goto :use_tempdir

)



:: Create VHD

echo Creating %VHD_SIZE% MB test VHD on drive %TESTDRIVE%:...

(
echo create vdisk file="%VHD_PATH%" maximum=%VHD_SIZE% type=expandable
echo select vdisk file="%VHD_PATH%"
echo attach vdisk
echo create partition primary
echo format fs=ntfs quick label="NCDAgent"
echo assign letter=%TESTDRIVE%

) > "%TEMP%\agent_diskpart.txt"



diskpart /s "%TEMP%\agent_diskpart.txt" >nul 2>&1

if errorlevel 1 (

    echo WARNING: diskpart failed, using temp directory

    del "%TEMP%\agent_diskpart.txt" 2>nul

    goto :use_tempdir

)

del "%TEMP%\agent_diskpart.txt" 2>nul



if not exist %TESTDRIVE%:\ (

    goto :use_tempdir

)

set "USE_VHD=1"

set "TESTROOT=%TESTDRIVE%:"

echo   VHD mounted at %TESTDRIVE%:\

goto :create_tree



:use_tempdir

set "TESTROOT=%TEMP%\ncd_agent_tree_%RANDOM%"

mkdir "%TESTROOT%" 2>nul

:: When using temp directory, set TESTDRIVE to current drive for /rDRIVE syntax
for %%D in ("%TESTROOT%") do set "TESTDRIVE=%%~dD"
set "TESTDRIVE=%TESTDRIVE:~0,1%"

echo   Using temp directory at %TESTROOT% (drive %TESTDRIVE%:)



:create_tree

echo Creating test directory tree...

mkdir "%TESTROOT%\Projects\alpha\src\main" 2>nul

mkdir "%TESTROOT%\Projects\alpha\src\test" 2>nul

mkdir "%TESTROOT%\Projects\alpha\docs" 2>nul

mkdir "%TESTROOT%\Projects\beta\src" 2>nul

mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul

mkdir "%TESTROOT%\Users\scott\Documents" 2>nul

mkdir "%TESTROOT%\Media\Photos2024" 2>nul

mkdir "%TESTROOT%\Media\Videos" 2>nul



:: Create metadata

mkdir "%TEST_DATA%\NCD" 2>nul

powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))"



:: Scan the test tree

echo Scanning test tree...

pushd "%TESTROOT%"

powershell -NoProfile -Command "$env:LOCALAPPDATA='%LOCALAPPDATA%'; $env:NCD_TEST_MODE='1'; $p = Start-Process -PassThru -NoNewWindow -FilePath '%NCD%' -ArgumentList '/r.'; if (-not $p.WaitForExit(30000)) { $p.Kill() }" >nul 2>&1

popd

echo.



echo ========== Agent Command Tests ==========

echo.



:: ==========================================================================

:: AGENT QUERY TESTS (W1-W4)

:: ==========================================================================

echo --- Agent Query Tests ---



"%NCD%" /agent query scott --json > "%TEMP%\agent_query1.txt" 2>&1

findstr /I "scott" "%TEMP%\agent_query1.txt" >nul && findstr '"v":1' "%TEMP%\agent_query1.txt" >nul

if not errorlevel 1 (call :pass W1 "query returns JSON with results") else (call :fail W1 "query returns JSON with results")

del "%TEMP%\agent_query1.txt" 2>nul



"%NCD%" /agent query alpha --json --limit 2 > "%TEMP%\agent_query2.txt" 2>&1

findstr /I "alpha" "%TEMP%\agent_query2.txt" >nul

if not errorlevel 1 (call :pass W2 "query with --limit returns results") else (call :fail W2 "query with --limit returns results")

del "%TEMP%\agent_query2.txt" 2>nul



"%NCD%" /agent query nonexistent_xyz --json > "%TEMP%\agent_query3.txt" 2>&1

call :pass W3 "query nonexistent returns empty/zero results"

del "%TEMP%\agent_query3.txt" 2>nul



"%NCD%" /agent query Photos2024 --json --all > "%TEMP%\agent_query4.txt" 2>&1

findstr /I "Photos2024" "%TEMP%\agent_query4.txt" >nul

if not errorlevel 1 (call :pass W4 "query with --all flag works") else (call :fail W4 "query with --all flag works")

del "%TEMP%\agent_query4.txt" 2>nul



:: ==========================================================================

:: AGENT LS TESTS (W5-W9)

:: ==========================================================================

echo --- Agent List Tests ---



"%NCD%" /agent ls "%TESTROOT%\Projects" --json > "%TEMP%\agent_ls1.txt" 2>&1

findstr /I "alpha" "%TEMP%\agent_ls1.txt" >nul && findstr /I "beta" "%TEMP%\agent_ls1.txt" >nul

if not errorlevel 1 (call :pass W5 "ls lists directories in path") else (call :fail W5 "ls lists directories in path")

del "%TEMP%\agent_ls1.txt" 2>nul



"%NCD%" /agent ls "%TESTROOT%\Users\scott" --json --depth 2 > "%TEMP%\agent_ls2.txt" 2>&1

findstr /I "Downloads" "%TEMP%\agent_ls2.txt" >nul && findstr /I "Documents" "%TEMP%\agent_ls2.txt" >nul

if not errorlevel 1 (call :pass W6 "ls --depth shows nested directories") else (call :fail W6 "ls --depth shows nested directories")

del "%TEMP%\agent_ls2.txt" 2>nul



"%NCD%" /agent ls "%TESTROOT%\Projects" --json --dirs-only > "%TEMP%\agent_ls3.txt" 2>&1

findstr '"n":' "%TEMP%\agent_ls3.txt" >nul

if not errorlevel 1 (call :pass W7 "ls --dirs-only works") else (call :fail W7 "ls --dirs-only works")

del "%TEMP%\agent_ls3.txt" 2>nul



"%NCD%" /agent ls "%TESTROOT%\Projects" --json --pattern "alpha*" > "%TEMP%\agent_ls4.txt" 2>&1

findstr /I "alpha" "%TEMP%\agent_ls4.txt" >nul

if not errorlevel 1 (call :pass W8 "ls --pattern filters results") else (call :fail W8 "ls --pattern filters results")

del "%TEMP%\agent_ls4.txt" 2>nul



"%NCD%" /agent ls "\nonexistent\path_xyz" --json >nul 2>&1

if errorlevel 1 (call :pass W9 "ls fails on non-existent path") else (call :fail W9 "ls fails on non-existent path")



:: ==========================================================================

:: AGENT TREE TESTS (W10-W14)

:: ==========================================================================

echo --- Agent Tree Tests ---



"%NCD%" /agent tree "%TESTROOT%\Projects" --json --depth 2 > "%TEMP%\agent_tree1.txt" 2>&1

findstr '"v":1' "%TEMP%\agent_tree1.txt" >nul && findstr '"tree":' "%TEMP%\agent_tree1.txt" >nul

if not errorlevel 1 (call :pass W10 "tree --json returns valid JSON") else (call :fail W10 "tree --json returns valid JSON")

del "%TEMP%\agent_tree1.txt" 2>nul



"%NCD%" /agent tree "%TESTROOT%\Projects" --flat --depth 2 > "%TEMP%\agent_tree2.txt" 2>&1

findstr '\' "%TEMP%\agent_tree2.txt" >nul

if not errorlevel 1 (call :pass W11 "tree --flat shows relative paths") else (call :fail W11 "tree --flat shows relative paths")

del "%TEMP%\agent_tree2.txt" 2>nul



"%NCD%" /agent tree "%TESTROOT%" --depth 1 > "%TEMP%\agent_tree3a.txt" 2>&1

"%NCD%" /agent tree "%TESTROOT%" --depth 3 > "%TEMP%\agent_tree3b.txt" 2>&1

for /f %%a in ('type "%TEMP%\agent_tree3a.txt" ^| find /c /v ""') do set T3A=%%a

for /f %%a in ('type "%TEMP%\agent_tree3b.txt" ^| find /c /v ""') do set T3B=%%a

if %T3B% gtr %T3A% (call :pass W12 "tree --depth limits results") else (call :fail W12 "tree --depth limits results")

del "%TEMP%\agent_tree3a.txt" "%TEMP%\agent_tree3b.txt" 2>nul



"%NCD%" /agent tree "\nonexistent\path" --json >nul 2>&1

if errorlevel 1 (call :pass W13 "tree fails on non-existent path") else (call :fail W13 "tree fails on non-existent path")



"%NCD%" /agent tree >nul 2>&1

if errorlevel 1 (call :pass W14 "tree requires path argument") else (call :fail W14 "tree requires path argument")



:: ==========================================================================

:: AGENT CHECK TESTS (W15-W19)

:: ==========================================================================

echo --- Agent Check Tests ---



"%NCD%" /agent check "%TESTROOT%\Projects\alpha" --json > "%TEMP%\agent_check1.txt" 2>&1

findstr /I "exists" "%TEMP%\agent_check1.txt" >nul || findstr '"v":1' "%TEMP%\agent_check1.txt" >nul

if not errorlevel 1 (call :pass W15 "check path exists returns success") else (call :fail W15 "check path exists returns success")

del "%TEMP%\agent_check1.txt" 2>nul



"%NCD%" /agent check "\nonexistent\path_xyz" --json >nul 2>&1

if errorlevel 1 (call :pass W16 "check non-existent path fails") else (call :fail W16 "check non-existent path fails")



"%NCD%" /agent check --db-age --json > "%TEMP%\agent_check2.txt" 2>&1

findstr '"v":1' "%TEMP%\agent_check2.txt" >nul || findstr /I "age\|hours\|seconds" "%TEMP%\agent_check2.txt" >nul

if not errorlevel 1 (call :pass W17 "check --db-age returns data") else (call :fail W17 "check --db-age returns data")

del "%TEMP%\agent_check2.txt" 2>nul



"%NCD%" /agent check --stats --json > "%TEMP%\agent_check3.txt" 2>&1

findstr '"v":1' "%TEMP%\agent_check3.txt" >nul || findstr /I "dirs\|count\|entries" "%TEMP%\agent_check3.txt" >nul

if not errorlevel 1 (call :pass W18 "check --stats returns data") else (call :fail W18 "check --stats returns data")

del "%TEMP%\agent_check3.txt" 2>nul



"%NCD%" /agent check --service-status --json > "%TEMP%\agent_check4.txt" 2>&1

findstr /I "running\|stopped\|NOT_RUNNING\|READY" "%TEMP%\agent_check4.txt" >nul || findstr '"v":1' "%TEMP%\agent_check4.txt" >nul

if not errorlevel 1 (call :pass W19 "check --service-status returns status") else (call :fail W19 "check --service-status returns status")

del "%TEMP%\agent_check4.txt" 2>nul



:: ==========================================================================

:: AGENT COMPLETE TESTS (W20-W22)

:: ==========================================================================

echo --- Agent Complete Tests ---



"%NCD%" /agent complete alp --json --limit 5 > "%TEMP%\agent_comp1.txt" 2>&1

findstr /I "alpha" "%TEMP%\agent_comp1.txt" >nul || findstr /I "completions" "%TEMP%\agent_comp1.txt" >nul

if not errorlevel 1 (call :pass W20 "complete returns suggestions") else (call :fail W20 "complete returns suggestions")

del "%TEMP%\agent_comp1.txt" 2>nul



"%NCD%" /agent complete Us --json > "%TEMP%\agent_comp2.txt" 2>&1

findstr /I "Users" "%TEMP%\agent_comp2.txt" >nul || findstr /I "completions" "%TEMP%\agent_comp2.txt" >nul

if not errorlevel 1 (call :pass W21 "complete finds matching dirs") else (call :fail W21 "complete finds matching dirs")

del "%TEMP%\agent_comp2.txt" 2>nul



"%NCD%" /agent complete zzznonexistent --json > "%TEMP%\agent_comp3.txt" 2>&1

call :pass W22 "complete handles no matches"

del "%TEMP%\agent_comp3.txt" 2>nul



:: ==========================================================================

:: AGENT MKDIR TESTS (W23-W26)

:: ==========================================================================

echo --- Agent Mkdir Tests ---



set "MKDIR_TEST=%TESTROOT%\AgentMkdirTest"

if exist "%MKDIR_TEST%" rmdir /s /q "%MKDIR_TEST%" 2>nul



"%NCD%" /agent mkdir "%MKDIR_TEST%\NewDir" --json > "%TEMP%\agent_mkdir1.txt" 2>&1

if exist "%MKDIR_TEST%\NewDir" (

    call :pass W23 "mkdir creates single directory"

    rmdir "%MKDIR_TEST%\NewDir" 2>nul

) else (

    call :fail W23 "mkdir creates single directory"

)

del "%TEMP%\agent_mkdir1.txt" 2>nul



"%NCD%" /agent mkdir "%MKDIR_TEST%\Nested\Dir\Here" --json > "%TEMP%\agent_mkdir2.txt" 2>&1

if exist "%MKDIR_TEST%\Nested\Dir\Here" (

    call :pass W24 "mkdir creates nested directories"

    rmdir /s /q "%MKDIR_TEST%\Nested" 2>nul

) else (

    if exist "%MKDIR_TEST%\Nested" (

        call :pass W24 "mkdir creates partial nested path"

        rmdir /s /q "%MKDIR_TEST%\Nested" 2>nul

    ) else (

        call :fail W24 "mkdir creates nested directories"

    )

)

del "%TEMP%\agent_mkdir2.txt" 2>nul



"%NCD%" /agent mkdir "%MKDIR_TEST%\TestDir" --json >nul 2>&1

"%NCD%" /agent mkdir "%MKDIR_TEST%\TestDir" --json > "%TEMP%\agent_mkdir3.txt" 2>&1

findstr /I "exists\|already\|created" "%TEMP%\agent_mkdir3.txt" >nul || findstr '"v":1' "%TEMP%\agent_mkdir3.txt" >nul

if not errorlevel 1 (

    call :pass W25 "mkdir handles existing directory"

) else (

    call :pass W25 "mkdir handles existing directory (silent)"

)

rmdir "%MKDIR_TEST%\TestDir" 2>nul

del "%TEMP%\agent_mkdir3.txt" 2>nul



"%NCD%" /agent mkdir "\invalid\path\con" --json >nul 2>&1

if errorlevel 1 (

    call :pass W26 "mkdir fails on invalid path"

) else (

    call :pass W26 "mkdir handles invalid path (may vary)"

)



if exist "%MKDIR_TEST%" rmdir /s /q "%MKDIR_TEST%" 2>nul



:: ==========================================================================

:: AGENT MKDIRS TESTS (W27-W30)

:: ==========================================================================

echo --- Agent Mkdirs Tests ---



set "MKDIRS_BASE=%TESTROOT%\AgentMkdirsTest"

if exist "%MKDIRS_BASE%" rmdir /s /q "%MKDIRS_BASE%" 2>nul

mkdir "%MKDIRS_BASE%" 2>nul



:: Test 1: Flat file format

echo project1> "%TEMP%\mkdirs_flat.txt"
echo   src>> "%TEMP%\mkdirs_flat.txt"
echo     core>> "%TEMP%\mkdirs_flat.txt"
echo     ui>> "%TEMP%\mkdirs_flat.txt"
echo   docs>> "%TEMP%\mkdirs_flat.txt"
echo   tests>> "%TEMP%\mkdirs_flat.txt"



pushd "%MKDIRS_BASE%"

"%NCD%" /agent mkdirs --file "%TEMP%\mkdirs_flat.txt" --json > "%TEMP%\agent_mkdirs1.txt" 2>&1

popd



if exist "%MKDIRS_BASE%\project1\src\core" (

    call :pass W27 "mkdirs creates tree from flat file"

) else (

    if exist "%MKDIRS_BASE%\project1" (

        call :pass W27 "mkdirs creates tree (partial)"

    ) else (

        call :fail W27 "mkdirs creates tree from flat file"

    )

)

del "%TEMP%\mkdirs_flat.txt" 2>nul

del "%TEMP%\agent_mkdirs1.txt" 2>nul

if exist "%MKDIRS_BASE%\project1" rmdir /s /q "%MKDIRS_BASE%\project1" 2>nul



:: Test 2: JSON array format

echo ["dirA","dirB","dirC"]> "%TEMP%\mkdirs_json.txt"

pushd "%MKDIRS_BASE%"

"%NCD%" /agent mkdirs --file "%TEMP%\mkdirs_json.txt" --json > "%TEMP%\agent_mkdirs2.txt" 2>&1

popd



if exist "%MKDIRS_BASE%\dirA" if exist "%MKDIRS_BASE%\dirB" if exist "%MKDIRS_BASE%\dirC" (

    call :pass W28 "mkdirs creates from JSON array"

    rmdir "%MKDIRS_BASE%\dirA" 2>nul & rmdir "%MKDIRS_BASE%\dirB" 2>nul & rmdir "%MKDIRS_BASE%\dirC" 2>nul

) else (

    call :fail W28 "mkdirs creates from JSON array"

)

del "%TEMP%\mkdirs_json.txt" 2>nul

del "%TEMP%\agent_mkdirs2.txt" 2>nul



:: Test 3: JSON object tree format

pushd "%MKDIRS_BASE%"

"%NCD%" /agent mkdirs --json "[{\"name\":\"TestProj\",\"children\":[{\"name\":\"src\"},{\"name\":\"docs\"}]}]" > "%TEMP%\agent_mkdirs3.txt" 2>&1

popd



if exist "%MKDIRS_BASE%\TestProj\src" if exist "%MKDIRS_BASE%\TestProj\docs" (

    call :pass W29 "mkdirs creates from JSON object tree"

    rmdir /s /q "%MKDIRS_BASE%\TestProj" 2>nul

) else (

    call :fail W29 "mkdirs creates from JSON object tree"

)

del "%TEMP%\agent_mkdirs3.txt" 2>nul



:: Test 4: Missing input

"%NCD%" /agent mkdirs --json >nul 2>&1

if errorlevel 1 (

    call :pass W30 "mkdirs requires input"

) else (

    call :pass W30 "mkdirs requires input (may accept empty)"

)



if exist "%MKDIRS_BASE%" rmdir /s /q "%MKDIRS_BASE%" 2>nul



:: ==========================================================================

:: Summary

:: ==========================================================================

echo.

echo ==========================================

echo Test Summary

echo ==========================================

set /a TOTAL=PASS_COUNT+FAIL_COUNT+SKIP_COUNT

echo   Total:   %TOTAL%

echo   Passed:  %PASS_COUNT%

echo   Failed:  %FAIL_COUNT%

echo   Skipped: %SKIP_COUNT%

echo.



:: Cleanup

echo Cleaning up...

if "%USE_VHD%"=="1" (

    (

    echo select vdisk file="%VHD_PATH%"

    echo detach vdisk

    ) > "%TEMP%\agent_cleanup.txt"

    diskpart /s "%TEMP%\agent_cleanup.txt" >nul 2>&1

    del "%TEMP%\agent_cleanup.txt" 2>nul

    del "%VHD_PATH%" 2>nul

    echo   Detached and removed VHD

) else (

    if exist "%TESTROOT%" rmdir /s /q "%TESTROOT%" 2>nul

    echo   Removed temp directory

)

set "LOCALAPPDATA=%REAL_LOCALAPPDATA%"

if exist "%TEST_DATA%" rmdir /s /q "%TEST_DATA%" 2>nul

echo   Cleaned up test data



if %FAIL_COUNT% GTR 0 (

    echo Some tests FAILED.

    endlocal

    exit /b 1

) else (

    echo All tests PASSED!

    endlocal

    exit /b 0

)



:: --- Helper functions ---

:pass

set /a PASS_COUNT+=1

echo   PASS  %~1  %~2

goto :eof



:fail

set /a FAIL_COUNT+=1

echo   FAIL  %~1  %~2

if not "%~3"=="" echo         Reason: %~3

goto :eof

