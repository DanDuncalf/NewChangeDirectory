$p = Start-Process -PassThru -NoNewWindow -FilePath 'E:\llama\NewChangeDirectory\NewChangeDirectory.exe' -ArgumentList '/r.';
if (-not $p.WaitForExit(15000)) { $p.Kill(); exit 1 } else { exit $p.ExitCode }
