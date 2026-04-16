@echo off
setlocal
set "NCD=%CD%\NewChangeDirectory.exe"

echo === Test C:\ ===
"%NCD%" /agent tree "C:\" --json --depth 1 > %TEMP%\q1.txt 2>&1
type %TEMP%\q1.txt
echo.

echo === Test C:\. ===
"%NCD%" /agent tree "C:\." --json --depth 1 > %TEMP%\q2.txt 2>&1
type %TEMP%\q2.txt
echo.

echo === Test C:\Users ===
"%NCD%" /agent tree "C:\Users" --json --depth 1 > %TEMP%\q3.txt 2>&1
type %TEMP%\q3.txt
echo.

del %TEMP%\q1.txt 2>nul
del %TEMP%\q2.txt 2>nul
del %TEMP%\q3.txt 2>nul
endlocal
