@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cd /d "%~dp0"
cl /nologo /W3 /O2 /I..\src /I..\..\shared /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_mkdir_extended.exe test_agent_mkdir_extended.c test_framework.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
cl /nologo /W3 /O2 /I..\src /I..\..\shared /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_rmdir.exe test_agent_rmdir.c test_framework.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
cl /nologo /W3 /O2 /I..\src /I..\..\shared /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_mv.exe test_agent_mv.c test_framework.c ..\database.obj ..\platform.obj ..\platform_ncd.obj ..\scanner.obj ..\matcher.obj ..\strbuilder.obj ..\common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
cl /nologo /W3 /O2 /I..\src /I..\..\shared /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_verify.exe test_agent_verify.c test_framework.c kernel32.lib user32.lib shlwapi.lib advapi32.lib
