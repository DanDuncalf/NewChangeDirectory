@echo off
setlocal
set "NCD=%CD%\NewChangeDirectory.exe"
set "TESTROOT=E:\llama\NewChangeDirectory"

echo === Test E:\llama\NewChangeDirectory\ ===
"%NCD%" /agent tree "%TESTROOT%" --json --depth 1 > %TEMP%\q1.txt 2>&1
type %TEMP%\q1.txt
echo.

echo === Test with trailing dot ===
"%NCD%" /agent tree "%TESTROOT%." --json --depth 1 > %TEMP%\q2.txt 2>&1
type %TEMP%\q2.txt
echo.

echo === Test with trailing space quote ===
"%NCD%" /agent tree "E:\llama\NewChangeDirectory\ " --json --depth 1 > %TEMP%\q3.txt 2>&1
type %TEMP%\q3.txt
echo.

del %TEMP%\q1.txt 2>nul
del %TEMP%\q2.txt 2>nul
del %TEMP%\q3.txt 2>nul
endlocal
