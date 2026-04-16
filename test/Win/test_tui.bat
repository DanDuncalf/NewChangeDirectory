@echo off
::
:: test_tui.bat -- Run TUI tests with stdio backend
::
:: This script builds the TUI test driver in debug mode, runs each test
:: case by feeding key scripts via NCD_UI_KEYS_FILE, captures stdout,
:: and compares against expected output files.
::
:: Prerequisites:
::   - Visual Studio (cl.exe) must be in PATH or auto-detected
::   - MUST be run through the test harness for proper isolation
::
:: Usage:
::   Run-Tests-Safe.bat unit              -- run via harness ^(recommended^)
::   test_tui.bat                         -- run directly ^(NOT recommended^)
::   test_tui.bat generate                -- generate expected output
::
:: IMPORTANT: This script is best run through the test harness for
:: guaranteed cleanup. Running directly risks environment corruption
:: if interrupted with Ctrl+C.
::

:: ==========================================================================
:: ENVIRONMENT CHECK - WARN IF NOT RUNNING THROUGH TEST HARNESS
:: ==========================================================================
if "%NCD_TEST_MODE%"=="" (
    echo.
    echo ==========================================
    echo WARNING: TEST HARNESS NOT DETECTED
    echo ==========================================
    echo.
    echo This script is running WITHOUT the test harness protection.
    echo.
    echo This is NOT RECOMMENDED because:
    echo   - If you press Ctrl+C, environment cleanup may not run
    echo   - Your shell environment may be left in an inconsistent state
    echo.
    echo RECOMMENDED: Run through the test harness from project root:
    echo   Run-Tests-Safe.bat unit              ^(unit tests including TUI^)
    echo   Run-Tests-Safe.bat                   ^(all tests^)
    echo.
    echo For isolated execution without affecting your shell:
    echo   test\Run-Isolated.bat test\Win\test_tui.bat
    echo.
    echo If you continue and interrupt with Ctrl+C, run:
    echo   Run-Tests-Safe.bat --repair
    echo.
    echo Waiting 5 seconds before continuing...
    echo ==========================================
    echo.
    ping -n 6 127.0.0.1 >nul 2>&1
)

setlocal enabledelayedexpansion

set TESTDIR=%~dp0..
cd /d "%TESTDIR%"

echo ========================================
echo NCD TUI Test Suite
echo ========================================
echo.

:: Check if cl.exe is available
where cl >nul 2>nul
if %errorlevel% == 0 goto :build

echo cl.exe not found in PATH. Searching for Visual Studio environment...

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS_FOUND="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS_FOUND=%%i\VC\Auxiliary\Build\vcvars64.bat"
            goto :found_vcvars
        )
    )
)

for %%V in (2022 2021 2020 2019) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS_FOUND=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :found_vcvars
        )
    )
)

if not defined VCVARS_FOUND (
    echo ERROR: Could not find Visual Studio
    exit /b 1
)

:found_vcvars
echo Found: %VCVARS_FOUND%
call "%VCVARS_FOUND%"
if errorlevel 1 exit /b 1
echo.

:build
set SRCDIR=..\src
set SHARED=..\..\shared
set OBJDIR=obj\tui

:: Create object directory
if not exist %OBJDIR% mkdir %OBJDIR%

:: Debug build flags (NO /DNDEBUG so stdio backend is available)
set CFLAGS=/nologo /W3 /Od /Zi /DDEBUG /DNCD_TEST_BUILD /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I. /I%SRCDIR% /I%SHARED% /c /Fo%OBJDIR%\

echo Building TUI test driver (debug mode)...

:: Compile required source files
cl %CFLAGS% tui_test_driver.c >nul 2>&1
if errorlevel 1 ( echo FAILED: tui_test_driver.c & goto :error )

cl %CFLAGS% %SRCDIR%\ui.c >nul 2>&1
if errorlevel 1 ( echo FAILED: ui.c & goto :error )

cl %CFLAGS% %SRCDIR%\database.c >nul 2>&1
if errorlevel 1 ( echo FAILED: database.c & goto :error )

cl %CFLAGS% %SRCDIR%\matcher.c >nul 2>&1
if errorlevel 1 ( echo FAILED: matcher.c & goto :error )

cl %CFLAGS% %SRCDIR%\scanner.c >nul 2>&1
if errorlevel 1 ( echo FAILED: scanner.c & goto :error )

cl %CFLAGS% %SRCDIR%\platform_ncd.c >nul 2>&1
if errorlevel 1 ( echo FAILED: platform_ncd.c & goto :error )

cl %CFLAGS% /Fo%OBJDIR%\sh_platform.obj %SHARED%\platform.c >nul 2>&1
if errorlevel 1 ( echo FAILED: platform.c & goto :error )

cl %CFLAGS% /Fo%OBJDIR%\sh_strbuilder.obj %SHARED%\strbuilder.c >nul 2>&1
if errorlevel 1 ( echo FAILED: strbuilder.c & goto :error )

cl %CFLAGS% /Fo%OBJDIR%\sh_common.obj %SHARED%\common.c >nul 2>&1
if errorlevel 1 ( echo FAILED: common.c & goto :error )

echo Linking tui_test_driver.exe...
link /nologo /SUBSYSTEM:CONSOLE /DEBUG /OUT:tui_test_driver.exe ^
    %OBJDIR%\tui_test_driver.obj ^
    %OBJDIR%\ui.obj ^
    %OBJDIR%\database.obj ^
    %OBJDIR%\matcher.obj ^
    %OBJDIR%\scanner.obj ^
    %OBJDIR%\platform_ncd.obj ^
    %OBJDIR%\sh_platform.obj ^
    %OBJDIR%\sh_strbuilder.obj ^
    %OBJDIR%\sh_common.obj ^
    kernel32.lib user32.lib advapi32.lib shlwapi.lib >nul 2>&1
if errorlevel 1 ( echo FAILED: link & goto :error )

echo Build successful.
echo.

:: Create actual output directory
if not exist tui\actual mkdir tui\actual

:: Check mode
set GENERATE_MODE=0
if "%~1"=="generate" set GENERATE_MODE=1

:: Set environment for stdio TUI backend
set NCD_TUI_TEST=1
set NCD_TUI_COLS=80
set NCD_TUI_ROWS=25

set PASS_COUNT=0
set FAIL_COUNT=0
set TOTAL_COUNT=0

:: ========================================
:: Run each TUI test case
:: ========================================

call :run_test "selector_esc"        "selector"   "Selector: ESC cancels"
call :run_test "selector_enter"      "selector"   "Selector: ENTER selects first"
call :run_test "selector_down_enter" "selector"   "Selector: DOWN+ENTER selects second"
call :run_test "selector_end_enter"  "selector"   "Selector: END+ENTER selects last"
call :run_test "selector_filter"     "selector"   "Selector: filter by typing"
call :run_test "selector5_pgdn_enter" "selector5" "Selector5: END+ENTER selects last"
call :run_test "history_down_enter"  "history"    "History: DOWN+DOWN+ENTER selects third"

echo.
echo ========================================
if %GENERATE_MODE%==1 (
    echo Generated expected output for %TOTAL_COUNT% tests
    echo Run again without 'generate' to validate.
) else (
    echo TUI Test Results: %PASS_COUNT%/%TOTAL_COUNT% passed, %FAIL_COUNT% failed
    if %FAIL_COUNT% GTR 0 (
        echo SOME TUI TESTS FAILED
        exit /b 1
    ) else (
        echo ALL TUI TESTS PASSED
    )
)
echo ========================================

goto :cleanup

:: ========================================
:: Subroutine: run a single TUI test
::   %1 = test name (matches input/*.ioinp and expected/*.out)
::   %2 = driver mode (selector, selector5, history, etc.)
::   %3 = description
:: ========================================
:run_test
set TEST_NAME=%~1
set TEST_MODE=%~2
set TEST_DESC=%~3
set /a TOTAL_COUNT+=1

set NCD_UI_KEYS_FILE=tui\input\%TEST_NAME%.ioinp

:: Run the test driver, capture stdout
tui_test_driver.exe %TEST_MODE% > tui\actual\%TEST_NAME%.out 2>nul

if %GENERATE_MODE%==1 (
    :: Generate mode: copy actual to expected
    copy /y tui\actual\%TEST_NAME%.out tui\expected\%TEST_NAME%.out >nul
    echo   GENERATED: %TEST_DESC%
    exit /b 0
)

:: Compare actual vs expected
if not exist tui\expected\%TEST_NAME%.out (
    echo   SKIP: %TEST_DESC% (no expected output - run with 'generate' first)
    exit /b 0
)

fc /l tui\actual\%TEST_NAME%.txt tui\expected\%TEST_NAME%.txt >nul 2>&1
if errorlevel 1 (
    echo   FAIL: %TEST_DESC%
    echo         Diff between actual and expected:
    fc /l tui\actual\%TEST_NAME%.out tui\expected\%TEST_NAME%.out 2>nul | findstr /v "Comparing" | findstr /v "FC:"
    set /a FAIL_COUNT+=1
) else (
    echo   PASS: %TEST_DESC%
    set /a PASS_COUNT+=1
)
exit /b 0

:cleanup
:: Clean up build artifacts
rmdir /s /q %OBJDIR% 2>nul
del tui_test_driver.pdb 2>nul
del tui_test_driver.ilk 2>nul
goto :end

:error
echo.
echo TUI TEST BUILD FAILED.
exit /b 1

:end
endlocal
