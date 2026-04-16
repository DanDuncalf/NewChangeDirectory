$conf = '-conf "C:\temp\ncd.metadata"'

Write-Host "Test 1: string ArgumentList with -conf prefix"
$p = Start-Process -PassThru -NoNewWindow -FilePath "E:\llama\NewChangeDirectory\test_echo.bat" -ArgumentList "$conf -x `"*/Deep`""
$p.WaitForExit(5000)
Start-Sleep -Milliseconds 500

Write-Host "Test 2: array ArgumentList"
$p = Start-Process -PassThru -NoNewWindow -FilePath "E:\llama\NewChangeDirectory\test_echo.bat" -ArgumentList '-conf','C:\temp\ncd.metadata','-x','*/Deep'
$p.WaitForExit(5000)
