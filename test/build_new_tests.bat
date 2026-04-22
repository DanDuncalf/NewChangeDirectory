@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
set CFLAGS=/nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I.
cl %CFLAGS% /Fe:test_agent_mkdir_extended.exe test_agent_mkdir_extended.c test_framework.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
cl %CFLAGS% /Fe:test_agent_rmdir.exe test_agent_rmdir.c test_framework.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
cl %CFLAGS% /Fe:test_agent_mv.exe test_agent_mv.c test_framework.c ..\src\database.c ..\src\platform_ncd.c ..\src\scanner.c ..\src\matcher.c ..\..\shared\platform.c ..\..\shared\strbuilder.c ..\..\shared\common.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
cl %CFLAGS% /Fe:test_agent_verify.exe test_agent_verify.c test_framework.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
