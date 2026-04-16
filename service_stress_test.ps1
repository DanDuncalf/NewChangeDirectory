# Service Start/Stop Stress Test with Logging
# Tests service lifecycle under varying conditions

$LogFile = "$env:LOCALAPPDATA\NCD\ncd_service.log"
$TestResults = @()
$Iterations = 7

function Write-Header($text) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Clear-ServiceLog {
    if (Test-Path $LogFile) {
        Remove-Item $LogFile -Force -ErrorAction SilentlyContinue
    }
}

function Get-ServiceLog {
    if (Test-Path $LogFile) {
        return Get-Content $LogFile -Raw
    }
    return ""
}

function Check-LogErrors {
    param($LogContent)
    $errors = @()
    if ($LogContent -match "ERROR") {
        $errors += $LogContent | Select-String "ERROR|error" | ForEach-Object { $_.Line }
    }
    return $errors
}

function Test-ServiceCycle {
    param($Iteration)
    
    Write-Header "Iteration $Iteration of $Iterations"
    
    # Clear previous log
    Clear-ServiceLog
    
    $result = @{
        Iteration = $Iteration
        StartSuccess = $false
        StopSuccess = $false
        Commands = @()
        Errors = @()
        LogContent = ""
    }
    
    # Start service with detailed logging
    Write-Host "Starting service with -log2..." -ForegroundColor Yellow
    $startTime = Get-Date
    $proc = Start-Process -FilePath ".\NCDService.exe" -ArgumentList "start","-log2" -PassThru -WindowStyle Hidden
    
    # Wait for service to be ready (check via ncd /agent check)
    $ready = $false
    $waitCount = 0
    while (-not $ready -and $waitCount -lt 20) {
        Start-Sleep -Milliseconds 100
        $check = & .\NewChangeDirectory.exe "/agent" "check" "--service-status" 2>&1
        if ($check -match "READY|STARTING") {
            $ready = $true
        }
        $waitCount++
    }
    
    $result.StartTime = ((Get-Date) - $startTime).TotalMilliseconds
    $result.StartSuccess = $ready
    
    if (-not $ready) {
        Write-Host "  WARNING: Service may not have started properly" -ForegroundColor Red
        $result.Errors += "Service start timeout"
    } else {
        Write-Host "  Service ready in $($result.StartTime)ms" -ForegroundColor Green
    }
    
    # Run various NCD commands (non-TUI)
    $commands = @(
        @("/agent", "check", "--service-status"),
        @("/hl"),
        @("/agent", "tree", ".", "--depth", "2"),
        @("/agent", "check", "--stats"),
        @("/fl"),  # frequent list
        @("/gl")   # group list
    )
    
    foreach ($cmd in $commands) {
        $cmdStr = $cmd -join " "
        Write-Host "  Running: ncd $cmdStr" -NoNewline
        $cmdStart = Get-Date
        $output = & .\NewChangeDirectory.exe $cmd 2>&1
        $cmdTime = ((Get-Date) - $cmdStart).TotalMilliseconds
        $exitCode = $LASTEXITCODE
        
        $cmdResult = @{
            Command = $cmdStr
            ExitCode = $exitCode
            Duration = $cmdTime
            Output = $output
        }
        $result.Commands += $cmdResult
        
        if ($exitCode -eq 0) {
            Write-Host " OK (${cmdTime}ms)" -ForegroundColor Green
        } else {
            Write-Host " FAIL (exit: $exitCode, ${cmdTime}ms)" -ForegroundColor Red
        }
        
        Start-Sleep -Milliseconds 50
    }
    
    # Stop service
    Write-Host "Stopping service..." -NoNewline -ForegroundColor Yellow
    $stopStart = Get-Date
    $stopOutput = & .\NCDService.exe "stop" 2>&1
    $stopTime = ((Get-Date) - $stopStart).TotalMilliseconds
    
    # Wait for process to exit
    $stopped = $false
    $stopWait = 0
    while (-not $stopped -and $stopWait -lt 50) {
        Start-Sleep -Milliseconds 100
        $proc.Refresh()
        if ($proc.HasExited) {
            $stopped = $true
        }
        $stopWait++
    }
    
    # Force kill if still running
    if (-not $stopped) {
        Write-Host " FORCE KILL REQUIRED" -ForegroundColor Red
        $result.Errors += "Service required force kill"
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        $stopped = $true
    } else {
        Write-Host " OK (${stopTime}ms)" -ForegroundColor Green
    }
    
    $result.StopSuccess = $stopped
    $result.StopTime = $stopTime
    
    # Check log for errors
    Start-Sleep -Milliseconds 200  # Allow log flush
    $logContent = Get-ServiceLog
    $result.LogContent = $logContent
    $logErrors = Check-LogErrors -LogContent $logContent
    if ($logErrors) {
        $result.Errors += $logErrors
    }
    
    # Save log for this iteration
    $logCopy = "service_log_iter$Iteration.log"
    if (Test-Path $LogFile) {
        Copy-Item $LogFile $logCopy -Force
    }
    
    return $result
}

# Main test loop
Write-Header "Service Start/Stop Stress Test"
Write-Host "Running $Iterations iterations with logging enabled"
Write-Host "Log file: $LogFile"

# Ensure clean state
Write-Host "`nCleaning up any existing service processes..."
Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1

# Run test cycles
for ($i = 1; $i -le $Iterations; $i++) {
    $result = Test-ServiceCycle -Iteration $i
    $TestResults += $result
    Start-Sleep -Milliseconds 500
}

# Final cleanup
Write-Host "`nFinal cleanup..." -ForegroundColor Yellow
Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force

# Summary report
Write-Header "Test Summary"

$totalErrors = 0
$failedStarts = 0
$failedStops = 0

foreach ($r in $TestResults) {
    $status = if ($r.StartSuccess -and $r.StopSuccess -and $r.Errors.Count -eq 0) { "PASS" } else { "FAIL" }
    $color = if ($status -eq "PASS") { "Green" } else { "Red" }
    
    Write-Host "Iteration $($r.Iteration): " -NoNewline
    Write-Host $status -ForegroundColor $color -NoNewline
    Write-Host " (Start: $($r.StartTime)ms, Stop: $($r.StopTime)ms)" -NoNewline
    
    if ($r.Errors.Count -gt 0) {
        Write-Host " - Errors: $($r.Errors.Count)" -ForegroundColor Red
        $totalErrors += $r.Errors.Count
        foreach ($err in $r.Errors) {
            Write-Host "    - $err" -ForegroundColor DarkRed
        }
    } else {
        Write-Host ""
    }
    
    if (-not $r.StartSuccess) { $failedStarts++ }
    if (-not $r.StopSuccess) { $failedStops++ }
}

Write-Header "Final Statistics"
Write-Host "Total Iterations: $Iterations"
Write-Host "Failed Starts: $failedStarts" -ForegroundColor $(if($failedStarts -gt 0){"Red"}else{"Green"})
Write-Host "Failed Stops: $failedStops" -ForegroundColor $(if($failedStops -gt 0){"Red"}else{"Green"})
Write-Host "Total Errors: $totalErrors" -ForegroundColor $(if($totalErrors -gt 0){"Red"}else{"Green"})

# Export detailed results
$TestResults | ConvertTo-Json -Depth 10 | Out-File "service_stress_test_results.json"
Write-Host "`nDetailed results saved to: service_stress_test_results.json"

# Show any error logs
$errorLogs = Get-ChildItem "service_log_iter*.log" -ErrorAction SilentlyContinue | Where-Object { (Get-Content $_ -Raw) -match "ERROR" }
if ($errorLogs) {
    Write-Header "Logs Containing Errors"
    foreach ($log in $errorLogs) {
        Write-Host "`n$($log.Name):" -ForegroundColor Yellow
        Get-Content $log | Select-String "ERROR|error" | ForEach-Object { "  $_" }
    }
}
