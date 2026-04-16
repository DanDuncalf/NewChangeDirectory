@echo off
setlocal
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "DB=E:\llama\NewChangeDirectory\test_wr.db"
set "NCD_TEST_MODE=1"

cd /d E:\llama\NewChangeDirectory
"%NCD%" /r. /d "%DB%" >nul 2>&1
echo Scan exit code: %ERRORLEVEL%

"%NCD%" /d "%DB%" nonexistent_xyz_42 >nul 2>&1
echo Search exit code: %ERRORLEVEL%

del "%DB%" 2>nul
