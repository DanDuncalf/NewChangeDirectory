$ncd = 'E:\llama\NewChangeDirectory\NewChangeDirectory.exe'
$conf_override = '-conf "E:\temp\ncd_test_config\ncd.metadata"'

# Ensure directory exists
New-Item -ItemType Directory -Force -Path 'E:\temp\ncd_test_config' | Out-Null

Write-Host "Test A: string ArgumentList"
$p = Start-Process -PassThru -NoNewWindow -FilePath $ncd -ArgumentList "$conf_override /xl"
$p.WaitForExit(5000)

Write-Host "Test B: array ArgumentList"
$p = Start-Process -PassThru -NoNewWindow -FilePath $ncd -ArgumentList '-conf','E:\temp\ncd_test_config\ncd.metadata','/xl'
$p.WaitForExit(5000)

Write-Host "Test C: direct call from batch"
