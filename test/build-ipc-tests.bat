@echo off
:: Build IPC test executables for Windows (MSVC)

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

echo ========================================
echo Building IPC Test Programs
echo ========================================
echo.

echo Building object files...

:: Compile shared source files
cl %CFLAGS% /Fo%OBJDIR%\sh_platform.obj %SHARED%\platform.c
if errorlevel 1 goto :error

cl %CFLAGS% /Fo%OBJDIR%\sh_strbuilder.obj %SHARED%\strbuilder.c
if errorlevel 1 goto :error

cl %CFLAGS% /Fo%OBJDIR%\sh_common.obj %SHARED%\common.c
if errorlevel 1 goto :error

:: Compile NCD-specific source files
cl %CFLAGS% %SRCDIR%\platform.c
if errorlevel 1 goto :error

:: Compile IPC implementation
cl %CFLAGS% %SRCDIR%\control_ipc_win.c
if errorlevel 1 goto :error

:: Compile IPC test common
cl %CFLAGS% ipc_test_common.c
if errorlevel 1 goto :error

echo.
echo Linking IPC test executables...

:: Phase 1: Basic Connectivity
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_ping_test.exe ipc_ping_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_state_test.exe ipc_state_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_shutdown_test.exe ipc_shutdown_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 2: Metadata and Operations
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_metadata_test.exe ipc_metadata_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_heuristic_test.exe ipc_heuristic_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_rescan_test.exe ipc_rescan_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 3: Persistence and Interactive
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_flush_test.exe ipc_flush_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_cli.exe ipc_cli.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 4: Fuzzing and Stress
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_fuzzer.exe ipc_fuzzer.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_stress_test.exe ipc_stress_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo.
echo ========================================
echo IPC Test Programs Built Successfully!
echo ========================================
echo.
echo Available executables:
echo   ipc_ping_test.exe      - Basic connectivity testing
echo   ipc_state_test.exe     - State information retrieval
echo   ipc_shutdown_test.exe  - Service shutdown testing
echo   ipc_metadata_test.exe  - Metadata operations
echo   ipc_heuristic_test.exe - Heuristic submissions
echo   ipc_rescan_test.exe    - Rescan operations
echo   ipc_flush_test.exe     - Persistence testing
echo   ipc_cli.exe            - Interactive CLI
echo   ipc_fuzzer.exe         - Protocol fuzzing
echo   ipc_stress_test.exe    - Load testing
echo.
echo Example usage:
echo   ipc_ping_test.exe --once
echo   ipc_state_test.exe --all
echo   ipc_cli.exe
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
