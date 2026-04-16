# Service Spawn Test
# The service may spawn a background process - let's verify

function Write-Header($text) {
    Write-Host "`n========================================" -ForegroundColor Cyan
    Write-Host $text -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

# Clean slate
Get-Process NCDService -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

Write-Header "Service Spawn Behavior Test"

Write-Host "`nBefore start:" -ForegroundColor Yellow
$before = Get-Process NCDService -ErrorAction SilentlyContinue
if ($before) {
    $before | ForEach-Object { Write-Host "  PID: $($_.Id)" }
} else {
    Write-Host "  No NCDService running"
}

Write-Host "`nStarting service..." -ForegroundColor Yellow
$startProc = Start-Process -FilePath ".\NCDService.exe" -ArgumentList "start","-log2" -PassThru -Wait
Write-Host "  Start command PID: $($startProc.Id)" -ForegroundColor Gray
Write-Host "  Start command exited: $($startProc.HasExited)" -ForegroundColor Gray
Write-Host "  Exit code: $($startProc.ExitCode)" -ForegroundColor Gray

# Wait a moment for background spawn
Start-Sleep -Milliseconds 500

Write-Host "`nAfter start command (500ms later):" -ForegroundColor Yellow
$after = Get-Process NCDService -ErrorAction SilentlyContinue
if ($after) {
    $after | ForEach-Object { Write-Host "  PID: $($_.Id) (different from start command!)" -ForegroundColor Green }
} else {
    Write-Host "  No NCDService running" -ForegroundColor Red
}

if ($after) {
    Write-Host "`nService status:" -ForegroundColor Yellow
    $status = & .\NewChangeDirectory.exe "/agent" "check" "--service-status" 2>&1
    Write-Host "  $status"
    
    Write-Host "`nStopping service..." -ForegroundColor Yellow
    $stopOutput = & .\NCDService.exe "stop" 2>&1
    Write-Host "  $stopOutput"
    
    # Wait for stop
    $wait = 0
    while ((Get-Process NCDService -ErrorAction SilentlyContinue) -and $wait -lt 20) {
        Start-Sleep -Milliseconds 100
        $wait++
    }
    
    $remaining = Get-Process NCDService -ErrorAction SilentlyContinue
    if ($remaining) {
        Write-Host "  Service still running - force killing" -ForegroundColor Red
        Stop-Process -Id $remaining.Id -Force
    } else {
        Write-Host "  Service stopped cleanly" -ForegroundColor Green
    }
}

Write-Header "Conclusion"
if ($after) {
    Write-Host "The service DOES spawn a background process!" -ForegroundColor Green
    Write-Host "The 'start' command is just a launcher that exits immediately." -ForegroundColor Gray
    Write-Host "To properly test, we need to:" -ForegroundColor Gray
    Write-Host "  1. Run 'NCDService start' (launcher exits)" -ForegroundColor Gray
    Write-Host "  2. Wait for background service to be ready" -ForegroundColor Gray  
    Write-Host "  3. Run NCD commands" -ForegroundColor Gray
    Write-Host "  4. Run 'NCDService stop' to stop background service" -ForegroundColor Gray
} else {
    Write-Host "The service did NOT spawn a background process" -ForegroundColor Red
    Write-Host "Check logs for startup errors:" -ForegroundColor Gray
    Get-Content "$env:LOCALAPPDATA\NCD\ncd_service.log" -ErrorAction SilentlyContinue | Select-Object -Last 10
}
