$ncd = 'E:\llama\NewChangeDirectory\NewChangeDirectory.exe'
$conf_override = '-conf "E:\temp\ncd metadata\ncd.metadata"'

Write-Host "Test A: string ArgumentList with quoted path containing space"
$p = Start-Process -PassThru -NoNewWindow -FilePath $ncd -ArgumentList "$conf_override /?"
$p.WaitForExit(5000)

Write-Host "Test B: array ArgumentList"
$p = Start-Process -PassThru -NoNewWindow -FilePath $ncd -ArgumentList '-conf','E:\temp\ncd metadata\ncd.metadata','/?'
$p.WaitForExit(5000)
