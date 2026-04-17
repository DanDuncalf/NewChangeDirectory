# Correct Service Test
# Understanding: NCDService start runs in foreground, needs to be backgrounded

$LogFile = "$env:LOCALAPPDATA\NCD\ncd_service.log"
$Iterations = 7

function Write-Header($text) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Clear-ServiceState {
    Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    if (Test-Path $LogFile) { Remove-Item $LogFile -Force }
}

function Start-ServiceBackground {
    # Start service in a hidden window (truly background)
    $proc = Start-Process -FilePath ".\NCDService.exe" -ArgumentList "start","-log2" -WindowStyle Hidden -PassThru
    return $proc
}

function Wait-ForServiceReady {
    param($TimeoutMs = 5000)
    
    $startTime = Get-Date
    while (((Get-Date) - $startTime).TotalMilliseconds -lt $TimeoutMs) {
        $status = & .\NewChangeDirectory.exe "--agent:check" "--service-status" 2>&1
        if ($status -match "READY") { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Stop-ServiceGraceful {
    $result = & .\NCDService.exe "stop" 2>&1
    $exitCode = $LASTEXITCODE
    
    # Wait for process to exit
    $wait = 0
    $proc = Get-Process NCDService -ErrorAction SilentlyContinue
    while ($proc -and $wait -lt 30) {
        Start-Sleep -Milliseconds 100
        $wait++
        $proc = Get-Process NCDService -ErrorAction SilentlyContinue
    }
    
    # Force kill if needed
    if ($proc) {
        Stop-Process -Id $proc.Id -Force
        return @{ Success = $false; Method = "force_kill"; WaitTime = $wait }
    }
    
    return @{ Success = $true; Method = "graceful"; WaitTime = $wait }
}

# Main test
Write-Header "Corrected Service Start/Stop Test ($Iterations cycles)"

$results = @()

for ($i = 1; $i -le $Iterations; $i++) {
    Write-Host "`n--- Cycle $i of $Iterations ---" -ForegroundColor Yellow
    
    # Clean state
    Clear-ServiceState
    
    $result = @{ Cycle = $i; StartOK = $false; StopOK = $false; StopMethod = ""; Errors = @() }
    
    # Start service
    Write-Host "  Starting service..." -NoNewline
    $svcProc = Start-ServiceBackground
    Start-Sleep -Milliseconds 200
    
    # Wait for ready
    $ready = Wait-ForServiceReady -TimeoutMs 3000
    $result.StartOK = $ready
    
    if ($ready) {
        Write-Host " READY" -ForegroundColor Green
        
        # Run NCD commands
        $commands = @(
            @("--agent:check", "--service-status"),
            @("-h:l"),
            @("--agent:tree", ".", "--depth", "2"),
            @("--agent:check", "--stats"),
            @("-g:l")
        )
        
        foreach ($cmd in $commands) {
            $cmdStr = $cmd -join " "
            $output = & .\NewChangeDirectory.exe $cmd 2>&1
            $exitCode = $LASTEXITCODE
            $symbol = if ($exitCode -eq 0) { "OK" } else { "FAIL" }
            Write-Host "    $symbol $cmdStr" -ForegroundColor $(if($exitCode -eq 0){"Green"}else{"Red"})
        }
        
        # Stop service
        Write-Host "  Stopping service..." -NoNewline
        $stopResult = Stop-ServiceGraceful
        $result.StopOK = $stopResult.Success
        $result.StopMethod = $stopResult.Method
        
        if ($stopResult.Success) {
            Write-Host " OK ($($stopResult.WaitTime)0ms)" -ForegroundColor Green
        } else {
            Write-Host " FORCE KILL" -ForegroundColor Red
        }
    } else {
        Write-Host " FAILED" -ForegroundColor Red
        $result.Errors += "Service failed to become ready"
        Stop-Process -Id $svcProc.Id -Force -ErrorAction SilentlyContinue
    }
    
    $results += $result
}

# Summary
Write-Header "Test Summary"

Write-Host "`nResults by cycle:" -ForegroundColor Yellow
foreach ($r in $results) {
    $status = if ($r.StartOK -and $r.StopOK) { "PASS" } else { "FAIL" }
    $color = if ($status -eq "PASS") { "Green" } else { "Red" }
    Write-Host "  Cycle $($r.Cycle): " -NoNewline
    Write-Host $status -ForegroundColor $color -NoNewline
    if ($r.StopMethod -eq "force_kill") {
        Write-Host " (force kill required)" -ForegroundColor Red -NoNewline
    }
    Write-Host ""
}

$passCount = ($results | Where-Object { $_.StartOK -and $_.StopOK }).Count
$failCount = $Iterations - $passCount
$forceKills = ($results | Where-Object { $_.StopMethod -eq "force_kill" }).Count

Write-Header "Statistics"
Write-Host "Total cycles: $Iterations"
Write-Host "Passed: $passCount" -ForegroundColor $(if($passCount -eq $Iterations){"Green"}else{"Yellow"})
Write-Host "Failed: $failCount" -ForegroundColor $(if($failCount -gt 0){"Red"}else{"Green"})
Write-Host "Force kills required: $forceKills" -ForegroundColor $(if($forceKills -gt 0){"Red"}else{"Green"})

# Check logs for errors
Write-Header "Log Analysis"
if (Test-Path $LogFile) {
    $errors = Get-Content $LogFile | Select-String "ERROR"
    if ($errors) {
        Write-Host "Errors found:" -ForegroundColor Red
        $errors | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
    } else {
        Write-Host "No errors in service log" -ForegroundColor Green
    }
} else {
    Write-Host "No log file found" -ForegroundColor Yellow
}
