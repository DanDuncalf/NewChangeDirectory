@echo off
setlocal

echo === Test 1: direct "T:\" ===
cmd /c "echo args: %1 %2 %3" "T:\" arg2 arg3

echo === Test 2: through batch var ===
set "TESTROOT=T:\"
cmd /c "echo args: %1 %2 %3" "%TESTROOT%" arg2 arg3

echo === Test 3: with trailing dot ===
cmd /c "echo args: %1 %2 %3" "T:\." arg2 arg3

endlocal
