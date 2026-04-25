@echo off
:: ============================================================================
:: clean.bat  --  Remove all build artifacts (objects + test binaries)
:: ============================================================================
::
:: This script performs a FULL clean of all intermediate and test output files
:: across ALL Windows build targets (MSVC main build, MSVC test builds, and
:: any manually-compiled objects that may have been left behind).
::
:: Run this before any full rebuild-and-test cycle to prevent stale object
:: files from causing link-time mismatches.
::
:: Usage:
::   cmd /c clean.bat
::
:: After cleaning, rebuild in this order:
::   1. cmd /c build.bat
::   2. cd test && cmd /c build-tests.bat
::   3. cd test && cmd /c build_new_tests.bat
::
:: ============================================================================

setlocal enabledelayedexpansion
echo ========================================
echo NCD Full Clean
echo ========================================
echo.

set "ROOTDIR=%~dp0"
set "TESTDIR=%ROOTDIR%test"

:: ---------------------------------------------------------------------------
:: 1. Remove object directories used by build.bat
:: ---------------------------------------------------------------------------
if exist "%ROOTDIR%obj" (
    echo [clean] Removing %ROOTDIR%obj  ...
    rmdir /s /q "%ROOTDIR%obj" 2>nul
    if exist "%ROOTDIR%obj" (
        echo [clean] WARNING: Could not fully remove %ROOTDIR%obj
    ) else (
        echo [clean] OK
    )
)

:: ---------------------------------------------------------------------------
:: 2. Remove object directory used by test\build-tests.bat
:: ---------------------------------------------------------------------------
if exist "%TESTDIR%\obj" (
    echo [clean] Removing %TESTDIR%\obj  ...
    rmdir /s /q "%TESTDIR%\obj" 2>nul
    if exist "%TESTDIR%\obj" (
        echo [clean] WARNING: Could not fully remove %TESTDIR%\obj
    ) else (
        echo [clean] OK
    )
)

:: ---------------------------------------------------------------------------
:: 3. Remove stray .obj files in project root (legacy / manual builds)
:: ---------------------------------------------------------------------------
set "ROOT_OBJ_COUNT=0"
for %%f in ("%ROOTDIR%*.obj") do (
    echo [clean] Deleting root object: %%~nxf
    del /q "%%f" 2>nul
    set /a ROOT_OBJ_COUNT+=1
)
if !ROOT_OBJ_COUNT!==0 (
    echo [clean] No stray .obj files in project root
) else (
    echo [clean] Removed !ROOT_OBJ_COUNT! root object files
)

:: ---------------------------------------------------------------------------
:: 4. Remove stray .obj files in test directory
:: ---------------------------------------------------------------------------
set "TEST_OBJ_COUNT=0"
for %%f in ("%TESTDIR%\*.obj") do (
    echo [clean] Deleting test object: %%~nxf
    del /q "%%f" 2>nul
    set /a TEST_OBJ_COUNT+=1
)
if !TEST_OBJ_COUNT!==0 (
    echo [clean] No stray .obj files in test directory
) else (
    echo [clean] Removed !TEST_OBJ_COUNT! test object files
)

:: ---------------------------------------------------------------------------
:: 5. Remove shared PDB that can retain stale debug info
:: ---------------------------------------------------------------------------
if exist "%ROOTDIR%vc140.pdb" (
    echo [clean] Deleting shared PDB: vc140.pdb
    del /q "%ROOTDIR%vc140.pdb" 2>nul
)

:: ---------------------------------------------------------------------------
:: 6. Remove main binaries so they are rebuilt from scratch
::     (Also removes stale copies that may have been left in test\)
:: ---------------------------------------------------------------------------
set "MAIN_EXE_COUNT=0"
for %%p in (NCDService NewChangeDirectory) do (
    for %%s in ("" _arm64 _riscv64) do (
        if exist "%ROOTDIR%%%p%%~s.exe" (
            echo [clean] Deleting main binary: %%p%%~s.exe
            del /q "%ROOTDIR%%%p%%~s.exe" 2>nul
            set /a MAIN_EXE_COUNT+=1
        )
        if exist "%ROOTDIR%%%p%%~s.pdb" (
            del /q "%ROOTDIR%%%p%%~s.pdb" 2>nul
        )
        if exist "%ROOTDIR%%%p%%~s.ilk" (
            del /q "%ROOTDIR%%%p%%~s.ilk" 2>nul
        )
        if exist "%TESTDIR%%%p%%~s.exe" (
            echo [clean] Deleting stale test-dir binary: %%p%%~s.exe
            del /q "%TESTDIR%%%p%%~s.exe" 2>nul
            set /a MAIN_EXE_COUNT+=1
        )
        if exist "%TESTDIR%%%p%%~s.pdb" (
            del /q "%TESTDIR%%%p%%~s.pdb" 2>nul
        )
        if exist "%TESTDIR%%%p%%~s.ilk" (
            del /q "%TESTDIR%%%p%%~s.ilk" 2>nul
        )
    )
)
if !MAIN_EXE_COUNT!==0 (
    echo [clean] No main binaries to remove
) else (
    echo [clean] Removed !MAIN_EXE_COUNT! main binaries
)

:: ---------------------------------------------------------------------------
:: 7. Remove test executables so generate_report.py rebuilds from scratch
:: ---------------------------------------------------------------------------
set "TEST_EXE_COUNT=0"
for %%p in (test_* fuzz_* ipc_* bench_* tui_test_driver) do (
    if exist "%TESTDIR%\%%p.exe" (
        echo [clean] Deleting test binary: %%p.exe
        del /q "%TESTDIR%\%%p.exe" 2>nul
        set /a TEST_EXE_COUNT+=1
    )
    if exist "%TESTDIR%\%%p.pdb" (
        del /q "%TESTDIR%\%%p.pdb" 2>nul
    )
    if exist "%TESTDIR%\%%p.ilk" (
        del /q "%TESTDIR%\%%p.ilk" 2>nul
    )
)
if !TEST_EXE_COUNT!==0 (
    echo [clean] No test executables to remove
) else (
    echo [clean] Removed !TEST_EXE_COUNT! test executables
)

:: ---------------------------------------------------------------------------
:: Summary
:: ---------------------------------------------------------------------------
echo.
echo ========================================
echo Clean complete.
echo ========================================
echo.
echo Rebuild order:
echo   1. cmd /c build.bat
echo   2. cd test ^&^& cmd /c build-tests.bat
echo   3. cd test ^&^& cmd /c build_new_tests.bat
echo   4. python test\generate_report.py
echo.

endlocal
exit /b 0
