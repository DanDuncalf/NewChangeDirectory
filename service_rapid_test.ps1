# Rapid Service Start/Stop Stress Test
# Tests service lifecycle with minimal delays

$LogFile = "$env:LOCALAPPDATA\NCD\ncd_service.log"
$Iterations = 10

function Write-Header($text) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

# Kill any existing service
Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

Write-Header "Rapid Service Start/Stop Test ($Iterations cycles)"

$results = @()

for ($i = 1; $i -le $Iterations; $i++) {
    Write-Host "Cycle $i`: " -NoNewline
    
    $result = @{ Cycle = $i; StartOK = $false; StopOK = $false; Errors = @() }
    
    # Start service
    $startProc = Start-Process -FilePath ".\NCDService.exe" -ArgumentList "start","-log2" -PassThru -WindowStyle Hidden
    
    # Quick check
    Start-Sleep -Milliseconds 200
    $check = & .\NewChangeDirectory.exe "/agent" "check" "--service-status" 2>&1
    $result.StartOK = ($check -match "READY|STARTING")
    
    # Run a quick command
    if ($result.StartOK) {
        $null = & .\NewChangeDirectory.exe "/agent" "check" "--stats" 2>&1
    }
    
    # Stop service
    $null = & .\NCDService.exe "stop" 2>&1
    
    # Wait briefly
    Start-Sleep -Milliseconds 300
    
    # Check if process is still running
    $svc = Get-Process NCDService -ErrorAction SilentlyContinue
    if ($svc) {
        # Try graceful wait
        $wait = 0
        while ($svc -and $wait -lt 10) {
            Start-Sleep -Milliseconds 100
            $svc = Get-Process NCDService -ErrorAction SilentlyContinue
            $wait++
        }
        
        if ($svc) {
            $result.Errors += "Required force kill"
            Stop-Process -Id $svc.Id -Force
        }
    }
    
    $result.StopOK = ($null -eq (Get-Process NCDService -ErrorAction SilentlyContinue))
    
    # Status
    if ($result.StartOK -and $result.StopOK) {
        Write-Host "PASS" -ForegroundColor Green
    } else {
        $status = @()
        if (-not $result.StartOK) { $status += "START_FAIL" }
        if (-not $result.StopOK) { $status += "STOP_FAIL" }
        if ($result.Errors) { $status += $result.Errors }
        Write-Host ($status -join ", ") -ForegroundColor Red
    }
    
    $results += $result
    Start-Sleep -Milliseconds 100
}

# Summary
Write-Header "Summary"
$passCount = ($results | Where-Object { $_.StartOK -and $_.StopOK }).Count
$failCount = $Iterations - $passCount

Write-Host "Passed: $passCount / $Iterations" -ForegroundColor $(if($passCount -eq $Iterations){"Green"}else{"Yellow"})
Write-Host "Failed: $failCount" -ForegroundColor $(if($failCount -gt 0){"Red"}else{"Green"})

if ($failCount -gt 0) {
    Write-Host "`nFailed cycles:" -ForegroundColor Red
    $results | Where-Object { -not ($_.StartOK -and $_.StopOK) } | ForEach-Object {
        Write-Host "  Cycle $($_.Cycle): Start=$($_.StartOK), Stop=$($_.StopOK), Errors=$($_.Errors -join ', ')"
    }
}

# Check final log for errors
Write-Header "Log Analysis"
if (Test-Path $LogFile) {
    $errors = Get-Content $LogFile | Select-String "ERROR" 
    if ($errors) {
        Write-Host "Errors found in log:" -ForegroundColor Red
        $errors | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    } else {
        Write-Host "No errors found in service log" -ForegroundColor Green
    }
    
    Write-Host "`nLast 20 log entries:" -ForegroundColor Gray
    Get-Content $LogFile | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
} else {
    Write-Host "No log file found" -ForegroundColor Yellow
}
