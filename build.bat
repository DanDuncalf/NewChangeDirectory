@echo off
::
:: build.bat  --  Build NewChangeDirectory with MSVC (cl.exe)
::
:: Requirements:
::   Run this from a Visual Studio x64 Native Tools Command Prompt,
::   or call vcvars64.bat first.
::
::   Visual Studio 2019 / 2022  (Community edition is fine)
::

setlocal

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
