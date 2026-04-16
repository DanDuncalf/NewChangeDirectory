@echo off
setlocal
set "NCD=%CD%\NewChangeDirectory.exe"

echo === Agent ls --json --dirs-only C:\Users ===
"%NCD%" /agent ls "C:\Users" --json --dirs-only > %TEMP%\dbg_ls.txt 2>&1
type %TEMP%\dbg_ls.txt
echo.

echo === Agent tree --flat --depth 2 C:\Users ===
"%NCD%" /agent tree "C:\Users" --flat --depth 2 > %TEMP%\dbg_tree.txt 2>&1
type %TEMP%\dbg_tree.txt
echo.

echo === Agent tree --json --flat --depth 2 C:\Users ===
"%NCD%" /agent tree "C:\Users" --json --flat --depth 2 > %TEMP%\dbg_tree_json.txt 2>&1
type %TEMP%\dbg_tree_json.txt
echo.

del %TEMP%\dbg_ls.txt 2>nul
del %TEMP%\dbg_tree.txt 2>nul
del %TEMP%\dbg_tree_json.txt 2>nul
endlocal
