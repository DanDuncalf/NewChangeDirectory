@echo off
setlocal
set "NCD=%CD%\NewChangeDirectory.exe"
set "TESTROOT=E:\llama\NewChangeDirectory"

echo === Agent ls --json --dirs-only ===
"%NCD%" /agent ls "%TESTROOT%" --json --dirs-only > %TEMP%\dbg_ls.txt 2>&1
type %TEMP%\dbg_ls.txt
echo.

echo === Agent tree --flat --depth 2 ===
"%NCD%" /agent tree "%TESTROOT%" --flat --depth 2 > %TEMP%\dbg_tree.txt 2>&1
type %TEMP%\dbg_tree.txt
echo.

echo === Agent tree --json --flat --depth 2 ===
"%NCD%" /agent tree "%TESTROOT%" --json --flat --depth 2 > %TEMP%\dbg_tree_json.txt 2>&1
type %TEMP%\dbg_tree_json.txt
echo.

del %TEMP%\dbg_ls.txt 2>nul
del %TEMP%\dbg_tree.txt 2>nul
del %TEMP%\dbg_tree_json.txt 2>nul
endlocal
