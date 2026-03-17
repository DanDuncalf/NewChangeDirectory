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

set SOURCES=^
    %SRCDIR%\main.c     ^
    %SRCDIR%\database.c ^
    %SRCDIR%\scanner.c  ^
    %SRCDIR%\matcher.c  ^
    %SRCDIR%\ui.c       ^
    %SRCDIR%\platform.c

set CFLAGS=/nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRCDIR%

set LDFLAGS=/link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib

echo Building %TARGET% with MSVC...
cl %CFLAGS% %SOURCES% /Fe:%TARGET% %LDFLAGS%

if errorlevel 1 (
    echo.
    echo BUILD FAILED.
    exit /b 1
)

echo.
echo Build successful: %TARGET%
echo.

:: Clean up intermediate files
del /f /q *.obj 2>nul

endlocal
