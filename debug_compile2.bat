@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"

cd test

cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I. /c /Fotest_framework.obj test_framework.c
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I. /c /Fodatabase.obj ..\src\database.c
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I. /c /Fomatcher.obj ..\src\matcher.c
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I. /c /Foplatform_ncd.obj ..\src\platform_ncd.c
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I. /c /Fosh_platform.obj ..\..\shared\platform.c
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I. /c /Fosh_strbuilder.obj ..\..\shared\strbuilder.c
cl /nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I..\src /I..\..\shared /I. /c /Fosh_common.obj ..\..\shared\common.c

cl /nologo /W3 /O2 /I..\src /I..\..\shared /I. /DPLATFORM_WINDOWS=1 /Fe:test_odd_cases.exe test_odd_cases.c test_framework.obj database.obj matcher.obj platform_ncd.obj sh_platform.obj sh_strbuilder.obj sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 (
    echo Compilation failed
    exit /b 1
)
