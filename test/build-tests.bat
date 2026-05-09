@echo off
:: Build test executables for Windows (MSVC)

setlocal enabledelayedexpansion

:: Check if cl.exe is available
where cl >nul 2>nul
if %errorlevel% == 0 goto :build

echo cl.exe not found in PATH. Searching for Visual Studio environment...

:: Try vswhere.exe
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            call "%%i\VC\Auxiliary\Build\vcvars64.bat"
            goto :build
        )
    )
)

:: Check version-numbered paths
for %%N in (18 17 16 15) do (
    for %%E in (Enterprise Professional Community BuildTools) do (
        if exist "%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvars64.bat" (
            call "%ProgramFiles%\Microsoft Visual Studio\%%N\%%E\VC\Auxiliary\Build\vcvars64.bat"
            goto :build
        )
    )
)

echo ERROR: Could not find Visual Studio
exit /b 1

:build
set SRCDIR=..\src
set SHARED=..\..\shared
set OBJDIR=obj
set CFLAGS=/nologo /W3 /O2 /DNDEBUG /D_WIN32_WINNT=0x0601 /DWINVER=0x0601 /D_CRT_SECURE_NO_WARNINGS /DNCD_TEST_BUILD /std:c11 /I%SRCDIR% /I%SHARED% /c /Fo%OBJDIR%\

if not exist %OBJDIR% mkdir %OBJDIR%

echo Building object files...

:: Compile test framework
cl %CFLAGS% test_framework.c
if errorlevel 1 goto :error

:: Compile NCD-specific source files
cl %CFLAGS% %SRCDIR%\database.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\matcher.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\platform.obj %SRCDIR%\platform_ncd.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\scanner.obj %SRCDIR%\scanner.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\service_state.obj %SRCDIR%\service_state.c
if errorlevel 1 goto :error

:: Compile shared source files (with sh_ prefix to avoid name collision)
cl %CFLAGS% /Fo%OBJDIR%\sh_platform.obj %SHARED%\platform.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\sh_strbuilder.obj %SHARED%\strbuilder.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\sh_common.obj %SHARED%\common.c
if errorlevel 1 goto :error

:: Compile additional sources for parallel expansion tests
cl %CFLAGS% %SRCDIR%\cli.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\ui.c
if errorlevel 1 goto :error
cl %CFLAGS% %SRCDIR%\result.c
if errorlevel 1 goto :error
cl %CFLAGS% /Fo%OBJDIR%\shm_platform_win.obj %SRCDIR%\shm_platform_win.c
if errorlevel 1 goto :error

cl %CFLAGS% /Fo%OBJDIR%\shared_state.obj %SRCDIR%\shared_state.c
if errorlevel 1 goto :error

cl %CFLAGS% /Fo%OBJDIR%\state_backend_local.obj %SRCDIR%\state_backend_local.c
if errorlevel 1 goto :error

cl %CFLAGS% /Fo%OBJDIR%\state_backend_service.obj %SRCDIR%\state_backend_service.c
if errorlevel 1 goto :error

cl %CFLAGS% /Fo%OBJDIR%\service_publish.obj %SRCDIR%\service_publish.c
if errorlevel 1 goto :error

echo Linking test executables...

:: Enable Windows long path support in all test executables
set LINK=/MANIFEST:EMBED /MANIFESTINPUT:..\NCD.manifest

:: Database tests (includes scanner.obj for new Tier 2 tests)
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_database.exe test_database.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Matcher tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_matcher.exe test_matcher.c %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %OBJDIR%\database.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Corruption tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_db_corruption.exe test_db_corruption.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Bug detection tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_bugs.exe test_bugs.c %OBJDIR%\test_framework.obj %OBJDIR%\matcher.obj %OBJDIR%\database.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 1: Strbuilder tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_strbuilder.exe test_strbuilder.c %OBJDIR%\test_framework.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 1: Common tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_common.exe test_common.c %OBJDIR%\test_framework.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 1: Platform tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_platform.exe test_platform.c %OBJDIR%\test_framework.obj %OBJDIR%\platform.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 2: Metadata tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_metadata.exe test_metadata.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Tier 2: Scanner tests
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_scanner.exe test_scanner.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Compile IPC for service tests
cl %CFLAGS% %SRCDIR%\control_ipc_win.c
if errorlevel 1 goto :error

:: Service tests: test_service_lazy_load.exe
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_lazy_load.exe test_service_lazy_load.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\service_state.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_lifecycle.exe
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_lifecycle.exe test_service_lifecycle.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_integration.exe
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_integration.exe test_service_integration.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_service_version_compat.exe
echo Building test_service_version_compat.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_version_compat.exe test_service_version_compat.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests: test_legacy_service_shutdown.exe
echo Building test_legacy_service_shutdown.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_legacy_service_shutdown.exe test_legacy_service_shutdown.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Compile IPC test dependencies
echo Compiling IPC test dependencies...
cl %CFLAGS% %SRCDIR%\control_ipc_win.c
if errorlevel 1 goto :error

cl %CFLAGS% ipc_test_common.c
if errorlevel 1 goto :error

:: Phase 1: IPC Ping Test
echo Building ipc_ping_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_ping_test.exe ipc_ping_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 1: IPC State Test
echo Building ipc_state_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_state_test.exe ipc_state_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 1: IPC Shutdown Test
echo Building ipc_shutdown_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_shutdown_test.exe ipc_shutdown_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 2: IPC Metadata Test
echo Building ipc_metadata_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_metadata_test.exe ipc_metadata_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 2: IPC Heuristic Test
echo Building ipc_heuristic_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_heuristic_test.exe ipc_heuristic_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 2: IPC Rescan Test
echo Building ipc_rescan_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_rescan_test.exe ipc_rescan_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 3: IPC Flush Test
echo Building ipc_flush_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_flush_test.exe ipc_flush_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 3: IPC CLI
echo Building ipc_cli.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_cli.exe ipc_cli.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 4: IPC Fuzzer
echo Building ipc_fuzzer.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_fuzzer.exe ipc_fuzzer.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Phase 4: IPC Stress Test
echo Building ipc_stress_test.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:ipc_stress_test.exe ipc_stress_test.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Standalone threaded service race tester (not part of harness yet)
echo Building service_race_tester.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:service_race_tester.exe service_race_tester.c %OBJDIR%\ipc_test_common.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: ==========================================================================
:: PARALLEL EXPANSION TESTS (430+ tests across 16 files)
:: ==========================================================================

:: Agent 1: Data Core Tests (90 tests)
echo Building test_database_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_database_extended.exe test_database_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_odd_cases.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_odd_cases.exe test_odd_cases.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building fuzz_database_load.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:fuzz_database_load.exe fuzz_database_load.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Agent 2: Service Infrastructure Tests (100 tests)
echo Building test_service_stress.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_stress.exe test_service_stress.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\service_state.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_service_race_conditions.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_race_conditions.exe test_service_race_conditions.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\service_state.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_service_rescan.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_rescan.exe test_service_rescan.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\service_state.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_service_init_db.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_init_db.exe test_service_init_db.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\service_state.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_ipc_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_ipc_extended.exe test_ipc_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\service_state.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_shm_stress.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_shm_stress.exe test_shm_stress.c %OBJDIR%\test_framework.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building fuzz_ipc.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:fuzz_ipc.exe fuzz_ipc.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\service_state.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Agent 3: UI and Main Tests (100 tests)
echo Building test_ui_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /Fe:test_ui_extended.exe test_ui_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\ui.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_ui_exclusions.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /Fe:test_ui_exclusions.exe test_ui_exclusions.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\ui.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_main.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_main.exe test_main.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\result.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Agent 4: Input Processing Tests (140 tests)
echo Building test_cli_edge_cases.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_cli_edge_cases.exe test_cli_edge_cases.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\cli.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_scanner_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_scanner_extended.exe test_scanner_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\cli.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_matcher_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_matcher_extended.exe test_matcher_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\cli.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_result_edge_cases.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_result_edge_cases.exe test_result_edge_cases.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\cli.obj %OBJDIR%\result.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_stress.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_stress.exe test_stress.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\cli.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: ==========================================================================
:: MISSING TESTS (aligning with Linux Makefile)
:: ==========================================================================

:: Tier 1: Config, History, IPC, Result output
echo Building test_config.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_config.exe test_config.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_history.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_history.exe test_history.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_ipc.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_ipc.exe test_ipc.c %OBJDIR%\test_framework.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_result_output.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_result_output.exe test_result_output.c %OBJDIR%\test_framework.obj %OBJDIR%\result.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Extended tests
echo Building test_cli_parse.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_cli_parse.exe test_cli_parse.c %OBJDIR%\test_framework.obj %OBJDIR%\cli.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_cli_parse_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_cli_parse_extended.exe test_cli_parse_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\cli.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: SKIPPED: test_platform_extended.c triggers MSVC internal compiler error (C1001)
:: on both /O2 and /Od. This is a compiler bug, not a code bug.
:: The test builds fine with GCC/Clang on Linux.
:: echo Building test_platform_extended.exe...
:: cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_platform_extended.exe test_platform_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
:: if errorlevel 1 goto :error

echo Building test_strbuilder_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_strbuilder_extended.exe test_strbuilder_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_common_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_common_extended.exe test_common_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_integration_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_integration_extended.exe test_integration_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Agent tests
echo Building test_agent_cli_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_cli_extended.exe test_agent_cli_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\cli.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_integration.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_integration.exe test_agent_integration.c %OBJDIR%\test_framework.obj %OBJDIR%\cli.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_query_tree.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_query_tree.exe test_agent_query_tree.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_ls_check.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_ls_check.exe test_agent_ls_check.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_complete_mkdir_quit.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_complete_mkdir_quit.exe test_agent_complete_mkdir_quit.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: UI tests
echo Building test_ui_selector.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /Fe:test_ui_selector.exe test_ui_selector.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\ui.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_ui_history.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /Fe:test_ui_history.exe test_ui_history.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\ui.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_ui_config.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /Fe:test_ui_config.exe test_ui_config.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\ui.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_ui_navigator.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /Fe:test_ui_navigator.exe test_ui_navigator.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\ui.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Service tests (extended)
echo Building test_service_database.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_database.exe test_service_database.c %OBJDIR%\test_framework.obj %OBJDIR%\service_publish.obj %OBJDIR%\shared_state.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\service_state.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_service_empty_db.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_empty_db.exe test_service_empty_db.c %OBJDIR%\test_framework.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\state_backend_local.obj %OBJDIR%\state_backend_service.obj %OBJDIR%\service_publish.obj %OBJDIR%\shared_state.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\service_state.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_service_ipc.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_ipc.exe test_service_ipc.c %OBJDIR%\test_framework.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\state_backend_local.obj %OBJDIR%\state_backend_service.obj %OBJDIR%\service_publish.obj %OBJDIR%\shared_state.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\service_state.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_service_logging.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_logging.exe test_service_logging.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_service_parity.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_service_parity.exe test_service_parity.c %OBJDIR%\test_framework.obj %OBJDIR%\control_ipc_win.obj %OBJDIR%\shared_state.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\state_backend_local.obj %OBJDIR%\state_backend_service.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Shared state / SHM
echo Building test_shared_state_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_shared_state_extended.exe test_shared_state_extended.c %OBJDIR%\test_framework.obj %OBJDIR%\shared_state.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_shm_platform.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_shm_platform.exe test_shm_platform.c %OBJDIR%\test_framework.obj %OBJDIR%\shm_platform_win.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Key injection / TUI
echo Building test_key_injection.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /DNCD_TEST_BUILD /Fe:test_key_injection.exe test_key_injection.c %OBJDIR%\test_framework.obj %OBJDIR%\ui.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: test_tui_integration.c is POSIX-only (uses unistd.h) - skip on Windows

:: ==========================================================================
:: Agent command tests (moved from build_new_tests.bat for unified build)
:: ==========================================================================
echo Building test_agent_mkdir_extended.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_mkdir_extended.exe test_agent_mkdir_extended.c %OBJDIR%\test_framework.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_rmdir.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_rmdir.exe test_agent_rmdir.c %OBJDIR%\test_framework.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_mv.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_mv.exe test_agent_mv.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\scanner.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_verify.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_verify.exe test_agent_verify.c %OBJDIR%\test_framework.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo Building test_agent_mode.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_agent_mode.exe test_agent_mode.c %OBJDIR%\test_framework.obj %OBJDIR%\database.obj %OBJDIR%\matcher.obj %OBJDIR%\platform.obj %OBJDIR%\cli.obj %OBJDIR%\sh_platform.obj %OBJDIR%\sh_strbuilder.obj %OBJDIR%\sh_common.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

:: Framework contract test (Phase 1 SKIP semantics validation)
echo Building test_framework_contract.exe...
cl /nologo /W3 /O2 /I%SRCDIR% /I%SHARED% /I. /DPLATFORM_WINDOWS=1 /Fe:test_framework_contract.exe test_framework_contract.c %OBJDIR%\test_framework.obj kernel32.lib user32.lib shlwapi.lib advapi32.lib
if errorlevel 1 goto :error

echo.
echo Build successful!
echo.

:: Clean up
rmdir /s /q %OBJDIR% 2>nul

goto :end

:error
echo.
echo BUILD FAILED.
exit /b 1

:end
endlocal
