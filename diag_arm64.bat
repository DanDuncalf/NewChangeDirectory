@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64_arm64
echo --- After vcvarsall x64_arm64 ---
where cl.exe
echo --- Compiler version ---
cl.exe /?
echo --- Testing compilation ---
echo int main(){return 0;} > %TEMP%\test_arm64.c
cl.exe /c /Fo%TEMP%\test_arm64.obj %TEMP%\test_arm64.c
echo --- Object machine type ---
"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.50.35717\bin\Hostx64\x64\dumpbin.exe" /headers %TEMP%\test_arm64.obj | findstr "machine"
del %TEMP%\test_arm64.c %TEMP%\test_arm64.obj 2>nul
