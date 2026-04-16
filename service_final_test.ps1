# Final Service Stress Test with Full Logging
# Accumulates logs across all cycles

$LogFile = "$env:LOCALAPPDATA\NCD\ncd_service.log"
$MasterLog = "service_master_test.log"
$Iterations = 7

function Write-Header($text) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Clear-ServiceState {
    Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
}

# Clear master log at start
if (Test-Path $MasterLog) { Remove-Item $MasterLog }
"Service Test Started at $(Get-Date)" | Out-File $MasterLog

Write-Header "Service Stress Test with Logging ($Iterations cycles)"

$results = @()

for ($i = 1; $i -le $Iterations; $i++) {
    Write-Host "`n--- Cycle $i of $Iterations ---" -ForegroundColor Yellow
    
    # Clean state but preserve log
    Clear-ServiceState
    if (Test-Path $LogFile) { Remove-Item $LogFile -Force }
    
    $result = @{ Cycle = $i; StartOK = $false; StopOK = $false; Commands = @() }
    
    # Start service (background)
    Write-Host "  Starting service..." -NoNewline
    $proc = Start-Process -FilePath ".\NCDService.exe" -ArgumentList "start","-log2" -WindowStyle Hidden -PassThru
    Start-Sleep -Milliseconds 300
    
    # Wait for ready
    $ready = $false
    $wait = 0
    while (-not $ready -and $wait -lt 30) {
        $status = & .\NewChangeDirectory.exe "/agent" "check" "--service-status" 2>&1
        if ($status -match "READY") { $ready = $true }
        Start-Sleep -Milliseconds 100
        $wait++
    }
    $result.StartOK = $ready
    
    if ($ready) {
        Write-Host " READY (${wait}0ms)" -ForegroundColor Green
        
        # Run commands
        $cmds = @("/hl", "/agent check --stats", "/gl")
        foreach ($c in $cmds) {
            $cmdArray = $c -split " "
            $output = & .\NewChangeDirectory.exe $cmdArray 2>&1
            $ec = $LASTEXITCODE
            $result.Commands += @{ Cmd = $c; ExitCode = $ec }
            $sym = if ($ec -eq 0) { "OK" } else { "FAIL" }
            Write-Host "    $sym $c" -ForegroundColor $(if($ec -eq 0){"Green"}else{"Red"})
        }
        
        # Stop service
        Write-Host "  Stopping service..." -NoNewline
        $stopStart = Get-Date
        $null = & .\NCDService.exe "stop" 2>&1
        
        # Wait for exit
        $sw = 0
        $svc = Get-Process NCDService -ErrorAction SilentlyContinue
        while ($svc -and $sw -lt 50) {
            Start-Sleep -Milliseconds 100
            $sw++
            $svc = Get-Process NCDService -ErrorAction SilentlyContinue
        }
        $stopTime = ((Get-Date) - $stopStart).TotalMilliseconds
        
        if ($svc) {
            Stop-Process -Id $svc.Id -Force
            Write-Host " FORCE KILL" -ForegroundColor Red
            $result.StopOK = $false
        } else {
            Write-Host " OK (${stopTime}ms)" -ForegroundColor Green
            $result.StopOK = $true
        }
    } else {
        Write-Host " FAILED" -ForegroundColor Red
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
    
    # Append cycle log to master log
    "`n=== CYCLE $i ===" | Out-File $MasterLog -Append
    if (Test-Path $LogFile) {
        Get-Content $LogFile | Out-File $MasterLog -Append
    } else {
        "No log file generated" | Out-File $MasterLog -Append
    }
    
    $results += $result
}

# Summary
Write-Header "Final Summary"
$pass = ($results | Where-Object { $_.StartOK -and $_.StopOK }).Count
Write-Host "Passed: $pass / $Iterations" -ForegroundColor $(if($pass -eq $Iterations){"Green"}else{"Red"})

# Analyze master log for patterns
Write-Header "Log Analysis"
if (Test-Path $MasterLog) {
    $content = Get-Content $MasterLog -Raw
    
    # Count key events
    $startCount = ([regex]::Matches($content, "Start command received")).Count
    $readyCount = ([regex]::Matches($content, "Service state changed to READY")).Count
    $shutdownCount = ([regex]::Matches($content, "shutdown|Shutdown")).Count
    $errorCount = ([regex]::Matches($content, "ERROR")).Count
    
    Write-Host "Events across all cycles:" -ForegroundColor Yellow
    Write-Host "  Start commands: $startCount"
    Write-Host "  Ready states:   $readyCount" -ForegroundColor $(if($readyCount -eq $Iterations){"Green"}else{"Red"})
    Write-Host "  Shutdowns:      $shutdownCount"
    Write-Host "  Errors:         $errorCount" -ForegroundColor $(if($errorCount -eq 0){"Green"}else{"Red"})
    
    if ($readyCount -ne $Iterations) {
        Write-Host "`nWARNING: Not all services reached READY state!" -ForegroundColor Red
    }
    
    # Show sample log from first successful cycle
    Write-Host "`nSample log from cycle 1:" -ForegroundColor Gray
    $cycles = $content -split "=== CYCLE \d+ ==="
    if ($cycles[1]) {
        $cycles[1] -split "`n" | Select-Object -First 15 | ForEach-Object {
            Write-Host "  $_" -ForegroundColor Gray
        }
    }
    
    Write-Host "`nFull log saved to: $MasterLog" -ForegroundColor Yellow
}
