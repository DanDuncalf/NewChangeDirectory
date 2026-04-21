@echo off
setlocal

:: Call vcvars64 to set up the environment
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

:: Compile the debug program
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /Isrc /I..\shared /I. /Fe:test_debug_odd.exe test_debug_odd.c src\database.c src\matcher.c src\platform_ncd.c ..\shared\platform.c ..\shared\strbuilder.c ..\shared\common.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 (
    echo Compilation failed
    exit /b 1
)

:: Run it
test_debug_odd.exe
