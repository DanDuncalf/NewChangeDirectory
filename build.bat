@echo off
::
:: build.bat  --  Build NewChangeDirectory with MSVC (cl.exe)
::
:: This script will automatically locate and run vcvars64.bat if cl.exe
:: is not already in the PATH.
::
::   Visual Studio 2019 / 2022  (Community edition is fine)
::

setlocal enabledelayedexpansion

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
set TARGET=NewChangeDirectory.exe
set SRCDIR=src
set SHARED=..\shared
set OBJDIR=obj

:: Create object directory
if not exist %OBJDIR% mkdir %OBJDIR%

set CFLAGS=/nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRCDIR% /I%SHARED% /c /Fo%OBJDIR%\

echo Building object files...

:: Compile NCD-specific source files
cl %CFLAGS% %SRCDIR%\main.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\database.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\scanner.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\matcher.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\ui.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\platform.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\state_backend_local.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\state_backend_service.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\shared_state.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\shm_platform_win.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\control_ipc_win.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\service_state.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\service_publish.c
if errorlevel 1 goto :error

:: Compile shared source files (with sh_ prefix to avoid name collision)
cl %CFLAGS% /Fo%OBJDIR%\sh_platform.obj %SHARED%\platform.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\sh_strbuilder.obj %SHARED%\strbuilder.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\sh_common.obj %SHARED%\common.c
if errorlevel 1 goto :error

echo Linking %TARGET%...
link /nologo /SUBSYSTEM:CONSOLE /OUT:%TARGET% ^
    %OBJDIR%\service_state.obj ^
    %OBJDIR%\service_publish.obj ^
    %OBJDIR%\main.obj ^
    %OBJDIR%\database.obj ^
    %OBJDIR%\scanner.obj ^
    %OBJDIR%\matcher.obj ^
    %OBJDIR%\ui.obj ^
    %OBJDIR%\platform.obj ^
    %OBJDIR%\sh_platform.obj ^
    %OBJDIR%\sh_strbuilder.obj ^
    %OBJDIR%\sh_common.obj ^
    %OBJDIR%\state_backend_local.obj ^
    %OBJDIR%\state_backend_service.obj ^
    %OBJDIR%\shared_state.obj ^
    %OBJDIR%\shm_platform_win.obj ^
    %OBJDIR%\control_ipc_win.obj ^
    kernel32.lib user32.lib advapi32.lib

if errorlevel 1 goto :error

echo.
echo Building service executable...
cl %CFLAGS% /Fo%OBJDIR%\service_main.obj %SRCDIR%\service_main.c
if errorlevel 1 goto :error

link /nologo /SUBSYSTEM:CONSOLE /OUT:NCDService.exe ^
    %OBJDIR%\service_main.obj ^
    %OBJDIR%\service_state.obj ^
    %OBJDIR%\service_publish.obj ^
    %OBJDIR%\state_backend_service.obj ^
    %OBJDIR%\state_backend_local.obj ^
    %OBJDIR%\database.obj ^
    %OBJDIR%\scanner.obj ^
    %OBJDIR%\matcher.obj ^
    %OBJDIR%\platform.obj ^
    %OBJDIR%\shared_state.obj ^
    %OBJDIR%\shm_platform_win.obj ^
    %OBJDIR%\control_ipc_win.obj ^
    %OBJDIR%\state_backend_local.obj ^
    %OBJDIR%\sh_platform.obj ^
    %OBJDIR%\sh_strbuilder.obj ^
    %OBJDIR%\sh_common.obj ^
    kernel32.lib user32.lib advapi32.lib

if errorlevel 1 goto :error

echo.
echo Build successful: %TARGET% and NCDService.exe
echo.

:: Clean up intermediate files
rmdir /s /q %OBJDIR% 2>nul

goto :end

:error
echo.
echo BUILD FAILED.
exit /b 1

:end
endlocal
