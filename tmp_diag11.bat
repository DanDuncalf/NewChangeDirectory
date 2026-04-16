@echo off
setlocal
set NCD_TEST_MODE=1
set TEST_DATA=%TEMP%\ncd_diag_12345
rmdir /s /q %TEST_DATA% 2>nul
mkdir %TEST_DATA%\NCD
set LOCALAPPDATA=%TEST_DATA%
set TESTROOT=%TEMP%\ncd_diag_tree_12345
rmdir /s /q %TESTROOT% 2>nul
mkdir %TESTROOT%\Reports
mkdir %TESTROOT%\Reports\Sub1
pushd %TESTROOT%
NewChangeDirectory.exe -conf %TEST_DATA%\NCD\ncd.metadata /r. > %TEMP%\diag_rescan.txt 2>&1
type %TEMP%\diag_rescan.txt
echo --- Database hex dump ---
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%TEST_DATA%\NCD\ncd_C.database'); Write-Host 'File size:' $b.Length; for($i=0; $i -lt [Math]::Min(112,$b.Length); $i++) { Write-Host -NoNewline ('{0:X2} ' -f $b[$i]); if(($i+1) % 16 -eq 0) { Write-Host } }; if($b.Length % 16 -ne 0) { Write-Host }"
echo --- Label at offset 40 ---
powershell -NoProfile -Command "$b=[IO.File]::ReadAllBytes('%TEST_DATA%\NCD\ncd_C.database'); $label=[System.Text.Encoding]::ASCII.GetString($b, 40, 64).Trim([char]0); Write-Host 'Label: ['$label']' Length: $label.Length"
popd
rmdir /s /q %TEST_DATA% 2>nul
rmdir /s /q %TESTROOT% 2>nul
