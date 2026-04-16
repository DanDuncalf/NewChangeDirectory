@echo off
setlocal
set "TESTROOT=T:\"
set "NCD=%CD%\NewChangeDirectory.exe"

echo === Bad quoting ===
"%NCD%" /agent tree "%TESTROOT%" --json --depth 1 > %TEMP%\bad.txt 2>&1
type %TEMP%\bad.txt

echo.
echo === Good quoting (T:.\) ===
"%NCD%" /agent tree "T:.\"" --json --depth 1 > %TEMP%\good.txt 2>&1
type %TEMP%\good.txt

echo.
echo === Good quoting (T:\.) ===
"%NCD%" /agent tree "T:\." --json --depth 1 > %TEMP%\good2.txt 2>&1
type %TEMP%\good2.txt

del %TEMP%\bad.txt 2>nul
del %TEMP%\good.txt 2>nul
del %TEMP%\good2.txt 2>nul
endlocal
