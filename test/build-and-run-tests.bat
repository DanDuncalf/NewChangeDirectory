@echo off
::
:: build-and-run-tests.bat  --  Build and run all NCD tests with MSVC
::
:: This script will automatically locate and run vcvars64.bat if cl.exe
:: is not already in the PATH.
::
::   Visual Studio 2019 / 2022  (Community edition is fine)
::

setlocal enabledelayedexpansion

echo ========================================
echo NCD Test Suite - Build and Run
echo ========================================
echo.

:: Check if cl.exe is available
where cl >nul 2>nul
if %errorlevel% == 0 goto :build

echo cl.exe not found in PATH. Searching for Visual Studio environment...

:: Common locations for vcvars64.bat
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS_FOUND="

:: Try vswhere.exe first (most reliable method for VS2017+)
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS_FOUND=%%i\VC\Auxiliary\Build\vcvars64.bat"
            goto :found_vcvars
        )
    )
)

:: Check common manual paths for VS2022
for %%V in (2022 2021 2020 2019) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS_FOUND=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :found_vcvars
        )
    )
)

:: Check version-numbered paths (VS2022 = 17, VS2019 = 16)
for %%N in (18 17 16 15) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS_FOUND=%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :found_vcvars
        )
    )
)

:: Check x86 (old VS versions)
for %%V in (2022 2019 2017) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            set "VCVARS_FOUND=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :found_vcvars
        )
    )
)

:: Check VS2015 and older
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC\bin\amd64\vcvars64.bat" (
    set "VCVARS_FOUND=%ProgramFiles(x86)%\Microsoft Visual Studio 14.0\VC\bin\amd64\vcvars64.bat"
    goto :found_vcvars
)

if not defined VCVARS_FOUND (
    echo.
    echo ERROR: Could not find Visual Studio vcvars64.bat
    echo.
    echo Please run this script from a "Visual Studio x64 Native Tools Command Prompt",
    echo or ensure Visual Studio 2019/2022 is installed.
    echo.
    exit /b 1
)

:found_vcvars
echo Found Visual Studio environment: %VCVARS_FOUND%
call "%VCVARS_FOUND%"
if errorlevel 1 (
    echo ERROR: Failed to initialize Visual Studio environment
    exit /b 1
)
echo.

:build
set SRCDIR=..\src
set SHARED=..\..\shared
set OBJDIR=obj

:: Create object directory
if not exist %OBJDIR% mkdir %OBJDIR%

set CFLAGS=/nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I. /I%SRCDIR% /I%SHARED% /c /Fo%OBJDIR%\

echo ========================================
echo Building object files...
echo ========================================

:: Compile NCD-specific source files (shared across tests)
echo Compiling database.c...
cl %CFLAGS% %SRCDIR%\database.c
if errorlevel 1 goto :error

echo Compiling matcher.c...
cl %CFLAGS% %SRCDIR%\matcher.c
if errorlevel 1 goto :error

echo Compiling scanner.c...
cl %CFLAGS% %SRCDIR%\scanner.c
if errorlevel 1 goto :error

echo Compiling cli.c...
cl %CFLAGS% %SRCDIR%\cli.c
if errorlevel 1 goto :error

echo Compiling platform.c (NCD-specific)...
cl %CFLAGS% /Fo%OBJDIR%\ncd_platform.obj %SRCDIR%\platform.c
if errorlevel 1 goto :error

:: Compile shared source files (with sh_ prefix to avoid name collision)
echo Compiling platform.c (shared)...
cl %CFLAGS% /Fo%OBJDIR%\sh_platform.obj %SHARED%\platform.c
if errorlevel 1 goto :error

echo Compiling strbuilder.c (shared)...
cl %CFLAGS% /Fo%OBJDIR%\sh_strbuilder.obj %SHARED%\strbuilder.c
if errorlevel 1 goto :error

echo Compiling common.c (shared)...
cl %CFLAGS% /Fo%OBJDIR%\sh_common.obj %SHARED%\common.c
if errorlevel 1 goto :error

:: Compile test framework
echo Compiling test_framework.c...
cl %CFLAGS% test_framework.c
if errorlevel 1 goto :error

echo.
echo ========================================
echo Building test executables...
echo ========================================

set LINK_FLAGS=/nologo /SUBSYSTEM:CONSOLE
set WIN_LIBS=kernel32.lib user32.lib shlwapi.lib advapi32.lib
set COMMON_OBJS=%OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\ncd_platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj

:: test_database.exe (needs matcher.obj for name_index_free)
echo Building test_database.exe...
cl %CFLAGS% test_database.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_database.exe %OBJDIR%\test_database.obj %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: test_matcher.exe
echo Building test_matcher.exe...
cl %CFLAGS% test_matcher.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_matcher.exe %OBJDIR%\test_matcher.obj %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: test_db_corruption.exe (needs matcher.obj for name_index_free)
echo Building test_db_corruption.exe...
cl %CFLAGS% test_db_corruption.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_db_corruption.exe %OBJDIR%\test_db_corruption.obj %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: test_bugs.exe
echo Building test_bugs.exe...
cl %CFLAGS% test_bugs.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_bugs.exe %OBJDIR%\test_bugs.obj %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: fuzz_database.exe (needs matcher.obj for name_index_free)
echo Building fuzz_database.exe...
cl %CFLAGS% fuzz_database.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:fuzz_database.exe %OBJDIR%\fuzz_database.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: bench_matcher.exe
echo Building bench_matcher.exe...
cl %CFLAGS% bench_matcher.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:bench_matcher.exe %OBJDIR%\bench_matcher.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: Tier 1: test_strbuilder.exe
echo Building test_strbuilder.exe...
cl %CFLAGS% test_strbuilder.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_strbuilder.exe %OBJDIR%\test_strbuilder.obj %OBJDIR%\test_framework.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj %WIN_LIBS%
if errorlevel 1 goto :error

:: Tier 1: test_common.exe
echo Building test_common.exe...
cl %CFLAGS% test_common.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_common.exe %OBJDIR%\test_common.obj %OBJDIR%\test_framework.obj %OBJDIR%\sh_common.obj %WIN_LIBS%
if errorlevel 1 goto :error

:: Tier 1: test_platform.exe
echo Building test_platform.exe...
cl %CFLAGS% test_platform.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_platform.exe %OBJDIR%\test_platform.obj %OBJDIR%\test_framework.obj %OBJDIR%\ncd_platform.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj %WIN_LIBS%
if errorlevel 1 goto :error

:: Tier 2: test_metadata.exe
echo Building test_metadata.exe...
cl %CFLAGS% test_metadata.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_metadata.exe %OBJDIR%\test_metadata.obj %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: Tier 2: test_scanner.exe
echo Building test_scanner.exe...
cl %CFLAGS% test_scanner.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_scanner.exe %OBJDIR%\test_scanner.obj %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %COMMON_OBJS% %WIN_LIBS%
if errorlevel 1 goto :error

:: Tier 3: test_cli_parse.exe
echo Building test_cli_parse.exe...
cl %CFLAGS% test_cli_parse.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_cli_parse.exe %OBJDIR%\test_cli_parse.obj %OBJDIR%\test_framework.obj %OBJDIR%\cli.obj %OBJDIR%\ncd_platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj %WIN_LIBS%
if errorlevel 1 goto :error

:: Compile service dependencies for service tests
echo Compiling service_state.c...
cl %CFLAGS% %SRCDIR%\service_state.c
if errorlevel 1 goto :error

echo Compiling control_ipc_win.c...
cl %CFLAGS% /Fo%OBJDIR%\control_ipc_win.obj %SRCDIR%\control_ipc_win.c
if errorlevel 1 goto :error

:: Service tests: test_service_lazy_load.exe
echo Building test_service_lazy_load.exe...
cl %CFLAGS% test_service_lazy_load.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_service_lazy_load.exe %OBJDIR%\test_service_lazy_load.obj %OBJDIR%\test_framework.obj %OBJDIR%\service_state.obj %OBJDIR%\matcher.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\ncd_platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_lifecycle.exe
echo Building test_service_lifecycle.exe...
cl %CFLAGS% test_service_lifecycle.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_service_lifecycle.exe %OBJDIR%\test_service_lifecycle.obj %OBJDIR%\test_framework.obj %OBJDIR%\service_state.obj %OBJDIR%\matcher.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\ncd_platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_integration.exe
echo Building test_service_integration.exe...
cl %CFLAGS% test_service_integration.c
if errorlevel 1 goto :error
link %LINK_FLAGS% /OUT:test_service_integration.exe %OBJDIR%\test_service_integration.obj %OBJDIR%\test_framework.obj %OBJDIR%\service_state.obj %OBJDIR%\matcher.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\ncd_platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo.
echo ========================================
echo Build successful! Running tests...
echo ========================================
echo.

:: Run all tests
echo --- Running test_strbuilder.exe ---
test_strbuilder.exe
if errorlevel 1 (
    echo FAILED: test_strbuilder.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_strbuilder.exe
)
echo.

echo --- Running test_common.exe ---
test_common.exe
if errorlevel 1 (
    echo FAILED: test_common.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_common.exe
)
echo.

echo --- Running test_platform.exe ---
test_platform.exe
if errorlevel 1 (
    echo FAILED: test_platform.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_platform.exe
)
echo.

echo --- Running test_database.exe ---
test_database.exe
if errorlevel 1 (
    echo FAILED: test_database.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_database.exe
)
echo.

echo --- Running test_matcher.exe ---
test_matcher.exe
if errorlevel 1 (
    echo FAILED: test_matcher.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_matcher.exe
)
echo.

echo --- Running test_metadata.exe ---
test_metadata.exe
if errorlevel 1 (
    echo FAILED: test_metadata.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_metadata.exe
)
echo.

echo --- Running test_scanner.exe ---
test_scanner.exe
if errorlevel 1 (
    echo FAILED: test_scanner.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_scanner.exe
)
echo.

echo --- Running test_cli_parse.exe ---
test_cli_parse.exe
if errorlevel 1 (
    echo FAILED: test_cli_parse.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_cli_parse.exe
)
echo.

echo --- Running test_service_lazy_load.exe ---
test_service_lazy_load.exe
if errorlevel 1 (
    echo FAILED: test_service_lazy_load.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_service_lazy_load.exe
)
echo.

echo --- Running test_service_lifecycle.exe ---
test_service_lifecycle.exe
if errorlevel 1 (
    echo FAILED: test_service_lifecycle.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_service_lifecycle.exe
)
echo.

echo --- Running test_service_integration.exe ---
test_service_integration.exe
if errorlevel 1 (
    echo FAILED: test_service_integration.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_service_integration.exe
)
echo.

echo --- Running test_db_corruption.exe ---
test_db_corruption.exe
if errorlevel 1 (
    echo FAILED: test_db_corruption.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_db_corruption.exe
)
echo.

echo --- Running test_bugs.exe ---
test_bugs.exe
if errorlevel 1 (
    echo FAILED: test_bugs.exe
    set TEST_FAILED=1
) else (
    echo PASSED: test_bugs.exe
)
echo.

echo --- Running bench_matcher.exe (quick run) ---
bench_matcher.exe
if errorlevel 1 (
    echo WARNING: bench_matcher.exe had issues (non-fatal)
)
echo.

echo --- Running fuzz_database.exe (5 second run) ---
echo Starting fuzz test (5 seconds)...
start /b fuzz_database.exe > fuzz_output.tmp 2>&1
set FUZZ_PID=!errorlevel!
ping -n 6 127.0.0.1 >nul 2>&1
taskkill /f /im fuzz_database.exe >nul 2>&1
type fuzz_output.tmp 2>nul
del fuzz_output.tmp 2>nul
echo Fuzz test completed.
echo.

:: Cleanup
echo ========================================
echo Cleaning up intermediate files...
echo ========================================
rmdir /s /q %OBJDIR% 2>nul

echo.
echo ========================================
if defined TEST_FAILED (
    echo SOME TESTS FAILED
    exit /b 1
) else (
    echo ALL UNIT TESTS PASSED
)
echo ========================================
echo.
echo To run comprehensive feature tests (requires Administrator):
echo   test\Win\test_features.bat

goto :end

:error
echo.
echo BUILD FAILED.
exit /b 1

:end
endlocal
