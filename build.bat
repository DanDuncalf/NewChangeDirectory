@echo off
::
:: build.bat  --  Build NewChangeDirectory with MSVC (cl.exe)
::
:: This script will automatically locate and run vcvars64.bat if cl.exe
:: is not already in the PATH.
::
::   Visual Studio 2019 / 2022  (Community edition is fine)
::
:: Supports both x64 and ARM64 architectures.
::

setlocal enabledelayedexpansion

:: Parse arguments
set BUILD_X64=1
set BUILD_ARM64=1
set TARGET_ARCH=all

if "%~1"=="x64" (
    set BUILD_X64=1
    set BUILD_ARM64=0
    set TARGET_ARCH=x64
)
if "%~1"=="arm64" (
    set BUILD_X64=0
    set BUILD_ARM64=1
    set TARGET_ARCH=arm64
)
if "%~1"=="all" (
    set BUILD_X64=1
    set BUILD_ARM64=1
    set TARGET_ARCH=all
)

:: Check if cl.exe is available
where cl >nul 2>nul
if %errorlevel% == 0 goto :detect_arch

echo cl.exe not found in PATH. Searching for Visual Studio environment...

:: Common locations for vswhere.exe
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

:detect_arch
:: Detect host architecture
set HOST_ARCH=x64
if "%PROCESSOR_ARCHITECTURE%"=="ARM64" set HOST_ARCH=arm64

:: Determine which architectures to build
if "%TARGET_ARCH%"=="all" (
    :: Build for both x64 and ARM64 by default
    set BUILD_X64=1
    set BUILD_ARM64=1
)

echo Building for architectures: %TARGET_ARCH%
echo Host architecture: %HOST_ARCH%
echo.

set SRCDIR=src
set SHARED=..\shared
set OBJDIR=obj

:: Build x64 version
if %BUILD_X64%==1 (
    echo ========================================
    echo Building x64 version...
    echo ========================================
    call :build_arch x64
    if errorlevel 1 exit /b 1
)

:: Build ARM64 version
if %BUILD_ARM64%==1 (
    echo.
    echo ========================================
    echo Building ARM64 version...
    echo ========================================
    call :build_arch arm64
    if errorlevel 1 exit /b 1
)

echo.
echo ========================================
echo Build successful for all architectures
echo ========================================

:: Clean up intermediate files
rmdir /s /q %OBJDIR% 2>nul

goto :end

:: Subroutine to build for a specific architecture
:build_arch
set ARCH=%~1
set ARCH_OBJDIR=%OBJDIR%\%ARCH%
set ARCH_SUFFIX=_%ARCH%

:: Create object directory
if not exist %ARCH_OBJDIR% mkdir %ARCH_OBJDIR%

:: Set architecture-specific compiler flags
if "%ARCH%"=="x64" (
    set ARCH_CFLAGS=/arch:AVX2
    set TARGET_EXE=NewChangeDirectory.exe
    set SERVICE_EXE=NCDService.exe
) else if "%ARCH%"=="arm64" (
    set ARCH_CFLAGS=
    set TARGET_EXE=NewChangeDirectory_arm64.exe
    set SERVICE_EXE=NCDService_arm64.exe
) else (
    echo ERROR: Unknown architecture %ARCH%
    exit /b 1
)

:: Try to setup the correct environment for this architecture
if "%ARCH%"=="arm64" (
    :: For ARM64, we need to find and run vcvarsarm64.bat
    call :setup_arm64_env
    if errorlevel 1 (
        echo WARNING: Could not setup ARM64 build environment, skipping ARM64 build
        exit /b 0
    )
)

set CFLAGS=/nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRCDIR% /I%SHARED% /c %ARCH_CFLAGS%

echo Building object files for %ARCH%...

:: Compile NCD-specific source files
cl %CFLAGS% /Fo%ARCH_OBJDIR%\main.obj %SRCDIR%\main.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\database.obj %SRCDIR%\database.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\scanner.obj %SRCDIR%\scanner.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\matcher.obj %SRCDIR%\matcher.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\ui.obj %SRCDIR%\ui.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\platform_ncd.obj %SRCDIR%\platform_ncd.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\cli.obj %SRCDIR%\cli.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\result.obj %SRCDIR%\result.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\state_backend_local.obj %SRCDIR%\state_backend_local.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\state_backend_service.obj %SRCDIR%\state_backend_service.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\shared_state.obj %SRCDIR%\shared_state.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\shm_types.obj %SRCDIR%\shm_types.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\shm_platform_win.obj %SRCDIR%\shm_platform_win.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\control_ipc_win.obj %SRCDIR%\control_ipc_win.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\service_state.obj %SRCDIR%\service_state.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\service_publish.obj %SRCDIR%\service_publish.c
if errorlevel 1 exit /b 1

:: Compile shared source files (with sh_ prefix to avoid name collision)
cl %CFLAGS% /Fo%ARCH_OBJDIR%\sh_platform.obj %SHARED%\platform.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\sh_strbuilder.obj %SHARED%\strbuilder.c
if errorlevel 1 exit /b 1
cl %CFLAGS% /Fo%ARCH_OBJDIR%\sh_common.obj %SHARED%\common.c
if errorlevel 1 exit /b 1

echo Linking %TARGET_EXE%...
link /nologo /SUBSYSTEM:CONSOLE /OUT:%TARGET_EXE% ^
    %ARCH_OBJDIR%\service_state.obj ^
    %ARCH_OBJDIR%\service_publish.obj ^
    %ARCH_OBJDIR%\main.obj ^
    %ARCH_OBJDIR%\database.obj ^
    %ARCH_OBJDIR%\scanner.obj ^
    %ARCH_OBJDIR%\matcher.obj ^
    %ARCH_OBJDIR%\ui.obj ^
    %ARCH_OBJDIR%\platform_ncd.obj ^
    %ARCH_OBJDIR%\cli.obj ^
    %ARCH_OBJDIR%\result.obj ^
    %ARCH_OBJDIR%\sh_platform.obj ^
    %ARCH_OBJDIR%\sh_strbuilder.obj ^
    %ARCH_OBJDIR%\sh_common.obj ^
    %ARCH_OBJDIR%\state_backend_local.obj ^
    %ARCH_OBJDIR%\state_backend_service.obj ^
    %ARCH_OBJDIR%\shared_state.obj ^
    %ARCH_OBJDIR%\shm_types.obj ^
    %ARCH_OBJDIR%\shm_platform_win.obj ^
    %ARCH_OBJDIR%\control_ipc_win.obj ^
    kernel32.lib user32.lib advapi32.lib shlwapi.lib

if errorlevel 1 exit /b 1

echo Building service executable %SERVICE_EXE%...
cl %CFLAGS% /Fo%ARCH_OBJDIR%\service_main.obj %SRCDIR%\service_main.c
if errorlevel 1 exit /b 1

link /nologo /SUBSYSTEM:CONSOLE /OUT:%SERVICE_EXE% ^
    %ARCH_OBJDIR%\service_main.obj ^
    %ARCH_OBJDIR%\service_state.obj ^
    %ARCH_OBJDIR%\service_publish.obj ^
    %ARCH_OBJDIR%\state_backend_service.obj ^
    %ARCH_OBJDIR%\state_backend_local.obj ^
    %ARCH_OBJDIR%\database.obj ^
    %ARCH_OBJDIR%\scanner.obj ^
    %ARCH_OBJDIR%\matcher.obj ^
    %ARCH_OBJDIR%\platform_ncd.obj ^
    %ARCH_OBJDIR%\cli.obj ^
    %ARCH_OBJDIR%\result.obj ^
    %ARCH_OBJDIR%\shared_state.obj ^
    %ARCH_OBJDIR%\shm_types.obj ^
    %ARCH_OBJDIR%\shm_platform_win.obj ^
    %ARCH_OBJDIR%\control_ipc_win.obj ^
    %ARCH_OBJDIR%\sh_platform.obj ^
    %ARCH_OBJDIR%\sh_strbuilder.obj ^
    %ARCH_OBJDIR%\sh_common.obj ^
    kernel32.lib user32.lib advapi32.lib shlwapi.lib

if errorlevel 1 exit /b 1

echo %ARCH% build successful: %TARGET_EXE% and %SERVICE_EXE%
exit /b 0

:: Subroutine to setup ARM64 build environment
:setup_arm64_env
:: Find vcvarsarm64.bat
set "VCVARSARM64="

:: Try vswhere.exe first
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvarsarm64.bat" (
            set "VCVARSARM64=%%i\VC\Auxiliary\Build\vcvarsarm64.bat"
            goto :found_arm64_vcvars
        )
    )
)

:: Check common manual paths for VS2022
for %%V in (2022 2021 2020 2019) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsarm64.bat" (
            set "VCVARSARM64=%ProgramFiles%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsarm64.bat"
            goto :found_arm64_vcvars
        )
    )
)

:: Check version-numbered paths
for %%N in (18 17 16 15) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvarsarm64.bat" (
            set "VCVARSARM64=%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvarsarm64.bat"
            goto :found_arm64_vcvars
        )
    )
)

:: Check x86 paths
for %%V in (2022 2019 2017) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsarm64.bat" (
            set "VCVARSARM64=%ProgramFiles(x86)%\Microsoft Visual Studio\%%V\%%E\VC\Auxiliary\Build\vcvarsarm64.bat"
            goto :found_arm64_vcvars
        )
    )
)

if not defined VCVARSARM64 (
    echo WARNING: Could not find vcvarsarm64.bat for ARM64 build
    exit /b 1
)

:found_arm64_vcvars
echo Setting up ARM64 build environment: %VCVARSARM64%
call "%VCVARSARM64%"
if errorlevel 1 (
    echo ERROR: Failed to initialize ARM64 build environment
    exit /b 1
)
exit /b 0

:error
echo.
echo BUILD FAILED.
exit /b 1

:end
endlocal
