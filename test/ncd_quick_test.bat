@echo off
setlocal enabledelayedexpansion
set "LOCALAPPDATA=%TEMP%\ncd_test_cmd"
mkdir "%LOCALAPPDATA%\NCD" 2>nul
mkdir "%TEMP%\ncd_quick_test\a\b\c" 2>nul
cd /d "%TEMP%\ncd_quick_test"
echo CWD: %CD%
echo Running NCD /r. ...
E:\llama\NewChangeDirectory\NewChangeDirectory.exe /r.
echo EXIT: %ERRORLEVEL%
echo DONE
endlocal
