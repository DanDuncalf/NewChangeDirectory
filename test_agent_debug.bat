@echo off
setlocal
set "TESTROOT=%TEMP%\ncd_agent_dbg_%RANDOM%%RANDOM%"
mkdir "%TESTROOT%\Projects\alpha\src" 2>nul
mkdir "%TESTROOT%\Users\scott\Downloads" 2>nul
set "TEST_DATA=%TEMP%\ncd_agent_data_%RANDOM%%RANDOM%"
mkdir "%TEST_DATA%\NCD" 2>nul
set "NCD=E:\llama\NewChangeDirectory\NewChangeDirectory.exe"
set "CONF=-conf "%TEST_DATA%\NCD\ncd.metadata""
set "DB=-d "%TEST_DATA%\NCD\ncd_test.database""
set NCD_TEST_MODE=1

pushd "%TESTROOT%"
"%NCD%" %CONF% %DB% /r. /i /s
echo Scan exit: %ERRORLEVEL%
popd

"%NCD%" %CONF% %DB% /agent tree "%TESTROOT%\Projects" --json --depth 2 > "%TEMP%\agent_dbg_out.txt" 2>&1
echo Agent tree exit: %ERRORLEVEL%
type "%TEMP%\agent_dbg_out.txt"

rmdir /s /q "%TESTROOT%" 2>nul
rmdir /s /q "%TEST_DATA%" 2>nul
del "%TEMP%\agent_dbg_out.txt" 2>nul
