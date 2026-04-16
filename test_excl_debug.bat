@echo off
setlocal
set "TESTROOT=%TEMP%\ncd_excl_dbg_%RANDOM%%RANDOM%"
mkdir "%TESTROOT%\Deep\L1\L2\L3\L4\L5\L6\L7\L8\L9\L10" 2>nul
mkdir "%TESTROOT%\Other" 2>nul
set "TEST_DATA=%TEMP%\ncd_excl_data_%RANDOM%%RANDOM%"
mkdir "%TEST_DATA%\NCD" 2>nul
if exist "%LOCALAPPDATA%\NCD\ncd.metadata" (
    copy "%LOCALAPPDATA%\NCD\ncd.metadata" "%TEST_DATA%\NCD\ncd.metadata" >nul 2>&1
) else (
    powershell -NoProfile -Command "[IO.File]::WriteAllBytes('%TEST_DATA%\NCD\ncd.metadata', [byte[]]@(0x4E,0x43,0x4D,0x44,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00))" >nul 2>&1
)
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "CONF=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB=-d "%TEST_DATA%\NCD\ncd_test.database""
set NCD_TEST_MODE=1

pushd "%TESTROOT%"
"%NCD%" %CONF% %DB% /r. /i /s
echo Scan exit: %ERRORLEVEL%
popd

"%NCD%" %CONF% -x "*/Deep"
echo Add exclusion exit: %ERRORLEVEL%

"%NCD%" %CONF% %DB% L10
echo Search L10 before rescan exit: %ERRORLEVEL%

pushd "%TESTROOT%"
"%NCD%" %CONF% %DB% /r. /i /s
echo Rescan exit: %ERRORLEVEL%
popd

"%NCD%" %CONF% %DB% L10
echo Search L10 after rescan exit: %ERRORLEVEL%

"%NCD%" %CONF% %DB% /agent tree "%TESTROOT%" --flat --depth 10 > "%TEMP%\excl_tree.txt" 2>&1
echo Agent tree exit: %ERRORLEVEL%
type "%TEMP%\excl_tree.txt"

rmdir /s /q "%TESTROOT%" 2>nul
rmdir /s /q "%TEST_DATA%" 2>nul
del "%TEMP%\excl_tree.txt" 2>nul
