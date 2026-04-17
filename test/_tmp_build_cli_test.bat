@echo off
setlocal

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do (
    call "%%i\VC\Auxiliary\Build\vcvars64.bat"
    goto :build
)

echo ERROR: Could not find Visual Studio environment
exit /b 1

:build
cd /d E:\llama\NewChangeDirectory\test
cl /nologo /W3 /O2 /I../src /I. /I../../shared test_cli_parse.c test_framework.c ../src/cli.c ../src/platform_ncd.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c /Fe:test_cli_parse.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib shlwapi.lib
if errorlevel 1 exit /b 1
cl /nologo /W3 /O2 /I../src /I. /I../../shared test_cli_parse_extended.c test_framework.c ../src/cli.c ../src/platform_ncd.c ../../shared/platform.c ../../shared/strbuilder.c ../../shared/common.c /Fe:test_cli_parse_extended.exe /link /SUBSYSTEM:CONSOLE kernel32.lib user32.lib shlwapi.lib
if errorlevel 1 exit /b 1
exit /b 0
