@echo off
setlocal EnableExtensions

set "SRC_DIR=%~dp0"
set "DEST_DIR=%USERPROFILE%\Tools\bin"

if not "%~1"=="" set "DEST_DIR=%~1"

if not exist "%SRC_DIR%NewChangeDirectory.exe" (
    echo ERROR: "%SRC_DIR%NewChangeDirectory.exe" not found.
    exit /b 1
)

if not exist "%SRC_DIR%ncd.bat" (
    echo ERROR: "%SRC_DIR%ncd.bat" not found.
    exit /b 1
)

if not exist "%DEST_DIR%" (
    mkdir "%DEST_DIR%" || (
        echo ERROR: Could not create "%DEST_DIR%".
        exit /b 1
    )
)

copy /Y "%SRC_DIR%NewChangeDirectory.exe" "%DEST_DIR%\" >nul || (
    echo ERROR: Failed to copy NewChangeDirectory.exe.
    exit /b 1
)

copy /Y "%SRC_DIR%ncd.bat" "%DEST_DIR%\" >nul || (
    echo ERROR: Failed to copy ncd.bat.
    exit /b 1
)

echo Deployed to "%DEST_DIR%":
echo   - NewChangeDirectory.exe
echo   - ncd.bat

endlocal
exit /b 0
