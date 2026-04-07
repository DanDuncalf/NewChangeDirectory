@echo off
:: Build test executables for Windows (MSVC)

setlocal enabledelayedexpansion

:: Check if cl.exe is available
where cl >nul 2>nul
if %errorlevel% == 0 goto :build

echo cl.exe not found in PATH. Searching for Visual Studio environment...

:: Try vswhere.exe
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            call "%%i\VC\Auxiliary\Build\vcvars64.bat"
            goto :build
        )
    )
)

:: Check version-numbered paths
for %%N in (18 17 16 15) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            call "%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :build
        )
    )
)

echo ERROR: Could not find Visual Studio
exit /b 1

:build
set SRCDIR=..\src
set SHARED=..\..\shared
set OBJDIR=obj
set CFLAGS=/nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRCDIR% /I%SHARED% /c /Fo%OBJDIR%\

if not exist %OBJDIR% mkdir %OBJDIR%

echo Building object files...

:: Compile test framework
cl %CFLAGS% test_framework.c
if errorlevel 1 goto :error

:: Compile NCD-specific source files
cl %CFLAGS% %SRCDIR%\database.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\matcher.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\platform.obj %SRCDIR%\platform_ncd.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\scanner.obj %SRCDIR%\scanner.c
if errorlevel 1 goto :error

:: Compile shared source files (with sh_ prefix to avoid name collision)
cl %CFLAGS% /Fo%OBJDIR%\sh_platform.obj %SHARED%\platform.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\sh_strbuilder.obj %SHARED%\strbuilder.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\sh_common.obj %SHARED%\common.c
if errorlevel 1 goto :error

echo Linking test executables...

:: Database tests (includes scanner.obj for new Tier 2 tests)
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_database.exe test_database.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Matcher tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_matcher.exe test_matcher.c %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %OBJDIR%\database.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Corruption tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_db_corruption.exe test_db_corruption.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Bug detection tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_bugs.exe test_bugs.c %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %OBJDIR%\database.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 1: Strbuilder tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_strbuilder.exe test_strbuilder.c %OBJDIR%\test_framework.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 1: Common tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_common.exe test_common.c %OBJDIR%\test_framework.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 1: Platform tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_platform.exe test_platform.c %OBJDIR%\test_framework.obj %OBJDIR%\platform.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 2: Metadata tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_metadata.exe test_metadata.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 2: Scanner tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_scanner.exe test_scanner.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_lazy_load.exe
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_lazy_load.exe test_service_lazy_load.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_lifecycle.exe
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_lifecycle.exe test_service_lifecycle.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_integration.exe
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_integration.exe test_service_integration.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Compile IPC test dependencies
echo Compiling IPC test dependencies...
cl %CFLAGS% %SRCDIR%\control_ipc_win.c
if errorlevel 1 goto :error

cl %CFLAGS% ipc_test_common.c
if errorlevel 1 goto :error

:: Phase 1: IPC Ping Test
echo Building ipc_ping_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_ping_test.exe ipc_ping_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 1: IPC State Test
echo Building ipc_state_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_state_test.exe ipc_state_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 1: IPC Shutdown Test
echo Building ipc_shutdown_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_shutdown_test.exe ipc_shutdown_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 2: IPC Metadata Test
echo Building ipc_metadata_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_metadata_test.exe ipc_metadata_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 2: IPC Heuristic Test
echo Building ipc_heuristic_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_heuristic_test.exe ipc_heuristic_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 2: IPC Rescan Test
echo Building ipc_rescan_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_rescan_test.exe ipc_rescan_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 3: IPC Flush Test
echo Building ipc_flush_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_flush_test.exe ipc_flush_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 3: IPC CLI
echo Building ipc_cli.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_cli.exe ipc_cli.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 4: IPC Fuzzer
echo Building ipc_fuzzer.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_fuzzer.exe ipc_fuzzer.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 4: IPC Stress Test
echo Building ipc_stress_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_stress_test.exe ipc_stress_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo.
echo Build successful!
echo.

:: Clean up
rmdir /s /q %OBJDIR% 2>nul

goto :end

:error
echo.
echo BUILD FAILED.
exit /b 1

:end
endlocal
