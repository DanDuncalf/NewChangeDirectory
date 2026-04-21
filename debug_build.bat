@echo off
setlocal

:: Use VS environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set SRC=src
set SHARED=..\shared
set TEST=test

cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /Fe:test_ui_exclusions_debug.exe ^
  %TEST%\test_ui_exclusions.c ^
  %TEST%\test_framework.c ^
  %SRC%\database.c ^
  %SRC%\scanner.c ^
  %SRC%\matcher.c ^
  %SRC%\platform_ncd.c ^
  %SRC%\ui.c ^
  %SHARED%\platform.c ^
  %SHARED%\strbuilder.c ^
  %SHARED%\common.c ^
  kernel32.lib user32.lib shlwapi.lib advapi32.lib

if errorlevel 1 exit /b 1

echo Build succeeded.
