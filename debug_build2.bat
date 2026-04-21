@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1

set SRC=src
set SHARED=..\shared
set TEST=test

:: Compile test source WITHOUT NCD_TEST_BUILD (simulating original bug)
cl /nologo /W3 /O2 /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Fotest_ui_exclusions_bug.obj %TEST%\test_ui_exclusions.c
if errorlevel 1 exit /b 1

:: Compile other sources WITH NCD_TEST_BUILD
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Fotest_framework.obj %TEST%\test_framework.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Fodatabase.obj %SRC%\database.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Foscanner.obj %SRC%\scanner.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Fomatcher.obj %SRC%\matcher.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Foplatform_ncd.obj %SRC%\platform_ncd.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Foui.obj %SRC%\ui.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Fosh_platform.obj %SHARED%\platform.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Fosh_strbuilder.obj %SHARED%\strbuilder.c
cl /nologo /W3 /O2 /DNCD_TEST_BUILD /DPLATFORM_WINDOWS=1 /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /std:c11 /I%SRC% /I%SHARED% /I%TEST% /Zi /c /Fosh_common.obj %SHARED%\common.c

link /nologo /DEBUG /OUT:test_ui_exclusions_bug.exe ^
  test_ui_exclusions_bug.obj test_framework.obj database.obj scanner.obj matcher.obj platform_ncd.obj ui.obj sh_platform.obj sh_strbuilder.obj sh_common.obj ^
  kernel32.lib user32.lib shlwapi.lib advapi32.lib

if errorlevel 1 exit /b 1

echo Build succeeded. Running...
test_ui_exclusions_bug.exe
