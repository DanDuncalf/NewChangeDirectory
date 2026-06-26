@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0.."

:: Build main binaries first, keeping obj files for linking alt services
echo === Running build.bat with NCD_KEEP_OBJ=1 ===
set NCD_KEEP_OBJ=1
call build.bat
if errorlevel 1 exit /b 1

:: Verify obj files exist
if not exist "obj\x64\database.obj" (
    echo ERROR: obj files not found after build.bat
    exit /b 1
)

:: Re-source the VS environment (build.bat's setlocal/endlocal wiped it)
echo Setting up Visual Studio environment for alt services...
call "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo WARNING: Could not find vcvars64.bat at expected path, trying alternatives...
    for %%d in (18 17 16) do (
        for %%e in (Community Professional Enterprise) do (
            if exist "%ProgramFiles%\Microsoft Visual Studio\%%d\%%e\VC\Auxiliary\Build\vcvars64.bat" (
                call "%ProgramFiles%\Microsoft Visual Studio\%%d\%%e\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
                goto :vcvars_ok
            )
        )
    )
)
:vcvars_ok
where cl >nul 2>nul
if errorlevel 1 (
    echo ERROR: Cannot find MSVC compiler
    exit /b 1
)

:: === v1.3 (lower than current 1.5) ===
echo Building NCDService_v13.exe (v1.3)...
cl /nologo /W3 /O2 /MD /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_APP_VERSION=\"1.3\" /std:c11 /Isrc /I../shared /c /Fo:obj\x64\service_main_v13.obj src\service_main.c
if errorlevel 1 exit /b 1

link /nologo /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:NCD.manifest /OUT:test\NCDService_v13.exe obj\x64\service_main_v13.obj obj\x64\ui.obj obj\x64\service_state.obj obj\x64\service_publish.obj obj\x64\state_backend_service.obj obj\x64\state_backend_local.obj obj\x64\database.obj obj\x64\scanner.obj obj\x64\matcher.obj obj\x64\platform_ncd.obj obj\x64\cli.obj obj\x64\result.obj obj\x64\shared_state.obj obj\x64\shm_types.obj obj\x64\shm_platform_win.obj obj\x64\control_ipc_common.obj obj\x64\control_ipc_win.obj obj\x64\sh_platform.obj obj\x64\sh_strbuilder.obj obj\x64\sh_common.obj kernel32.lib user32.lib advapi32.lib shlwapi.lib
if errorlevel 1 exit /b 1
echo   Built test\NCDService_v13.exe (v1.3)

:: === v1.7 (higher than current 1.5) ===
echo Building NCDService_v17.exe (v1.7)...
cl /nologo /W3 /O2 /MD /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_APP_VERSION=\"1.7\" /std:c11 /Isrc /I../shared /c /Fo:obj\x64\service_main_v17.obj src\service_main.c
if errorlevel 1 exit /b 1

link /nologo /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTINPUT:NCD.manifest /OUT:test\NCDService_v17.exe obj\x64\service_main_v17.obj obj\x64\ui.obj obj\x64\service_state.obj obj\x64\service_publish.obj obj\x64\state_backend_service.obj obj\x64\state_backend_local.obj obj\x64\database.obj obj\x64\scanner.obj obj\x64\matcher.obj obj\x64\platform_ncd.obj obj\x64\cli.obj obj\x64\result.obj obj\x64\shared_state.obj obj\x64\shm_types.obj obj\x64\shm_platform_win.obj obj\x64\control_ipc_common.obj obj\x64\control_ipc_win.obj obj\x64\sh_platform.obj obj\x64\sh_strbuilder.obj obj\x64\sh_common.obj kernel32.lib user32.lib advapi32.lib shlwapi.lib
if errorlevel 1 exit /b 1
echo   Built test\NCDService_v17.exe (v1.7)

:: Clean up obj files now that we're done
rmdir /s /q obj 2>nul

echo.
echo All alternative services built successfully.
endlocal
exit /b 0
