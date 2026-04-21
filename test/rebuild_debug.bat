@echo off
setlocal

:: Set up VS environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set SRCDIR=..\src
set SHARED=..\..\shared
set OBJDIR=obj

:: Compile test source
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /c /Fotest_ui_exclusions_debug.obj test_ui_exclusions.c
if errorlevel 1 exit /b 1

:: Link with map file
link /nologo /MAP:test_ui_exclusions_debug.map /OUT:test_ui_exclusions_debug.exe test_ui_exclusions_debug.obj %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\ui.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 exit /b 1

echo Done
test_ui_exclusions_debug.exe
