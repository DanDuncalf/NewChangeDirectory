# Service Diagnostic Test
# Tests to identify why service fails on rapid restart

$LogFile = "$env:LOCALAPPDATA\NCD\ncd_service.log"

function Write-Header($text) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Check-Resources {
    Write-Host "`nChecking for resource contention..." -ForegroundColor Yellow
    
    # Check for existing service processes
    $procs = Get-Process NCDService -ErrorAction SilentlyContinue
    if ($procs) {
        Write-Host "  NCDService processes running: $($procs.Count)" -ForegroundColor Red
        $procs | ForEach-Object { Write-Host "    PID: $($_.Id), Responding: $($_.Responding)" }
    } else {
        Write-Host "  No NCDService processes running" -ForegroundColor Green
    }
    
    # Check for shared memory handles (requires handle.exe from Sysinternals)
    # This is optional - will fail gracefully if not available
    
    # Check named pipes
    $pipes = Get-ChildItem \\.\pipe\ -ErrorAction SilentlyContinue | Where-Object { $_.Name -like "*NCD*" }
    if ($pipes) {
        Write-Host "  NCD named pipes found: $($pipes.Count)" -ForegroundColor Yellow
        $pipes | ForEach-Object { Write-Host "    $($_.Name)" }
    }
}

function Start-ServiceWithLog {
    param($Iteration)
    
    Write-Host "`n--- Starting service (attempt $Iteration) ---" -ForegroundColor Yellow
    
    # Clear log before start
    if (Test-Path $LogFile) { Remove-Item $LogFile -Force }
    
    # Start service in background
    $proc = Start-Process -FilePath ".\NCDService.exe" -ArgumentList "start","-log2" -PassThru -WindowStyle Hidden
    Write-Host "  Started PID: $($proc.Id)" -ForegroundColor Gray
    
    # Wait and poll for status
    $maxWait = 30  # 3 seconds
    $waited = 0
    $status = "UNKNOWN"
    
    while ($waited -lt $maxWait) {
        Start-Sleep -Milliseconds 100
        $waited++
        
        # Check if process died
        $proc.Refresh()
        if ($proc.HasExited) {
            $status = "EXITED (code: $($proc.ExitCode))"
            break
        }
        
        # Check service status via ncd
        $check = & .\NewChangeDirectory.exe "--agent:check" "--service-status" 2>&1
        if ($check -match "READY") {
            $status = "READY"
            break
        }
        if ($check -match "STARTING") {
            $status = "STARTING"
        }
    }
    
    if ($status -eq "UNKNOWN" -and $waited -ge $maxWait) {
        $status = "TIMEOUT"
    }
    
    Write-Host "  Status after ${waited}0ms: $status" -ForegroundColor $(
        if ($status -eq "READY") { "Green" } 
        elseif ($status -match "EXITED|TIMEOUT") { "Red" }
        else { "Yellow" }
    )
    
    # Show relevant log entries
    if (Test-Path $LogFile) {
        $log = Get-Content $LogFile -Raw
        if ($log) {
            Write-Host "  Log excerpt:" -ForegroundColor Gray
            $log -split "`n" | Select-Object -Last 5 | ForEach-Object { 
                if ($_ -match "ERROR|error") {
                    Write-Host "    $_" -ForegroundColor Red
                } else {
                    Write-Host "    $_" -ForegroundColor Gray
                }
            }
        }
    }
    
    return @{ PID = $proc.Id; Status = $status; Process = $proc }
}

function Stop-ServiceWithLog {
    param($PID)
    
    Write-Host "`n--- Stopping service (PID: $PID) ---" -ForegroundColor Yellow
    
    $stopOutput = & .\NCDService.exe "stop" 2>&1
    Write-Host "  Stop command output: $stopOutput" -ForegroundColor Gray
    
    # Wait for process to exit
    $waited = 0
    $proc = Get-Process -Id $PID -ErrorAction SilentlyContinue
    
    while ($proc -and $waited -lt 50) {
        Start-Sleep -Milliseconds 100
        $waited++
        $proc = Get-Process -Id $PID -ErrorAction SilentlyContinue
    }
    
    if ($proc) {
        Write-Host "  Process still running after ${waited}0ms - FORCE KILLING" -ForegroundColor Red
        Stop-Process -Id $PID -Force
        Start-Sleep -Milliseconds 200
    } else {
        Write-Host "  Process exited cleanly after ${waited}0ms" -ForegroundColor Green
    }
}

# Main diagnostic sequence
Write-Header "Service Diagnostic Test"

# Test 1: Clean start/stop with various delays
$delays = @(0, 100, 250, 500, 1000)

foreach ($delay in $delays) {
    Write-Header "Test with ${delay}ms delay after stop"
    
    # Ensure clean state
    Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    Check-Resources
    
    # Start
    $result = Start-ServiceWithLog -Iteration 1
    
    if ($result.Status -eq "READY") {
        # Run a command
        Write-Host "`n  Running test command..." -ForegroundColor Gray
        $cmdResult = & .\NewChangeDirectory.exe "--agent:check" "--stats" 2>&1
        Write-Host "  Command result: $cmdResult" -ForegroundColor Gray
        
        # Stop
        Stop-ServiceWithLog -PID $result.PID
        
        # Wait specified delay
        if ($delay -gt 0) {
            Write-Host "`n  Waiting ${delay}ms before checking resources..." -ForegroundColor Gray
            Start-Sleep -Milliseconds $delay
        }
        
        Check-Resources
    } else {
        Write-Host "`n  FAILED to start service" -ForegroundColor Red
        
        # Check if process is still there
        if (-not $result.Process.HasExited) {
            Stop-Process -Id $result.PID -Force -ErrorAction SilentlyContinue
        }
    }
    
    Start-Sleep -Milliseconds 500
}

# Final summary
Write-Header "Diagnostic Complete"
Write-Host "Check the log file for errors: $LogFile" -ForegroundColor Yellow
Write-Host "`nLook for patterns:" -ForegroundColor Gray
Write-Host "  - 'Start command received' without subsequent 'READY' = startup failure" -ForegroundColor Gray
Write-Host "  - Errors related to shared memory or named pipes = resource contention" -ForegroundColor Gray
Write-Host "  - Process exiting with non-zero code = crash during init" -ForegroundColor Gray
