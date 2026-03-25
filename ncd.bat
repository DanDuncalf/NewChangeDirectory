@echo off
::
:: ncd.bat  --  Norton Change Directory wrapper
::
:: This batch file is the user-facing command.  It calls
:: NewChangeDirectory.exe, which writes a small batch snippet to
:: %TEMP%\ncd_result.bat.  We then call that snippet to obtain the
:: three output variables and act on them.
::
:: Why a wrapper?
::   An .exe child process cannot change the working directory of its
::   parent CMD.EXE session (OS limitation).  The batch file can.
::
:: Environment variables set by ncd_result.bat:
::   NCD_STATUS   OK | ERROR
::   NCD_DRIVE    C:         (drive letter + colon, empty on error)
::   NCD_PATH     C:\Users\Scott\Downloads  (empty on error)
::   NCD_MESSAGE  Human-readable status / error string
::

:: ------------------------------------------------------------------
:: 0.  Clean up any leftover result file from a previous run
:: ------------------------------------------------------------------
if exist "%TEMP%\ncd_result.bat" del /f /q "%TEMP%\ncd_result.bat" 2>nul

:: ------------------------------------------------------------------
:: 1.  Initialise the output variables so we can always test them
::     safely even if the exe fails to create the result file.
:: ------------------------------------------------------------------
set "NCD_STATUS=ERROR"
set "NCD_DRIVE="
set "NCD_PATH="
set "NCD_MESSAGE=NewChangeDirectory.exe did not produce a result."

:: ------------------------------------------------------------------
:: 2.  Run the executable -- all arguments passed straight through
:: ------------------------------------------------------------------
NewChangeDirectory.exe %*
set "NCD_EXIT_CODE=%ERRORLEVEL%"

:: ------------------------------------------------------------------
:: 3.  Check if result file was created and act on the result
:: ------------------------------------------------------------------
:: If exe returned 0 (success) but no result file, it's an informational command
if "%NCD_EXIT_CODE%"=="0" if not exist "%TEMP%\ncd_result.bat" goto ncd_cleanup

:: If exe returned non-zero and no result file, show error
if not "%NCD_EXIT_CODE%"=="0" if not exist "%TEMP%\ncd_result.bat" (
    if not "%NCD_MESSAGE%"=="" echo(%NCD_MESSAGE%
    goto ncd_cleanup
)

:: Result file exists - load it and then delete it
if exist "%TEMP%\ncd_result.bat" (
    call "%TEMP%\ncd_result.bat"
    del /f /q "%TEMP%\ncd_result.bat" 2>nul
)

:: ------------------------------------------------------------------
:: 4.  Act on the result from the result file
:: ------------------------------------------------------------------
if /i "%NCD_STATUS%"=="OK" goto ncd_ok
if not "%NCD_MESSAGE%"=="" echo(%NCD_MESSAGE%
goto ncd_cleanup

:ncd_ok
:: Change drive first (CMD needs two separate commands for drive+path)
if not "%NCD_DRIVE%"=="" %NCD_DRIVE%

:: Change directory
if not "%NCD_PATH%"=="" cd /d "%NCD_PATH%"

:: Optionally echo where we landed (comment out if you prefer silence)
if not "%NCD_MESSAGE%"=="" echo(%NCD_MESSAGE%

:ncd_cleanup

:: ------------------------------------------------------------------
:: 5.  Clean up the variables so they don't pollute the session
:: ------------------------------------------------------------------
set "NCD_STATUS="
set "NCD_DRIVE="
set "NCD_PATH="
set "NCD_MESSAGE="
set "NCD_EXIT_CODE="
