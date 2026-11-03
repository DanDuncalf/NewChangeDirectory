# test_service_windows.ps1 -- "Service Tests (Isolated)"
#
# Exercises NCDService.exe on its own: lifecycle, IPC, version/state queries,
# persistence on graceful stop, and a clean service log.
# Replaces test\Test-Service-Windows.bat (deleted in bc900c1). Linux
# counterpart: test\test_service_wsl.sh.
#
# Run through the harness: python test\runner.py integration --suite service

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "NcdIntegration.psm1") -Force

Write-NcdHeader "Windows Service Tests (Isolated)"
$exit = 1
try {
    Initialize-NcdTestEnvironment -Name "svc"
    $paths = Get-NcdPaths
    $appVersion = Get-NcdAppVersion
    Stop-NcdTestService -Quiet | Out-Null

    # Seed a database so the service has something to publish.
    $scan = Invoke-Ncd -Arguments @("-r", ".") -TimeoutSeconds 60
    if ($scan.ExitCode -ne 0) { throw "initial scan failed: $($scan.Output)" }

    Invoke-NcdTest "Service status when stopped" {
        $r = Invoke-NcdService -Arguments @("status")
        if ($r.Output -notmatch 'stopped') { return "status output: $($r.Output)" }
        if ((Get-NcdServiceStatus) -ne "NOT_RUNNING") { return "client reports: $(Get-NcdServiceStatus)" }
        @(Get-NcdTestServiceProcess).Count -eq 0
    }

    Invoke-NcdTest "Service start" {
        $r = Start-NcdTestService
        if ($r.ExitCode -ne 0) { return "start exit $($r.ExitCode): $($r.Output)" }
        if (-not $r.Ready) { return "service did not reach READY; status=$(Get-NcdServiceStatus); log errors: $(Get-NcdServiceLogErrors)" }
        @(Get-NcdTestServiceProcess).Count -eq 1
    }

    Invoke-NcdTest "Service status when running" {
        $r = Invoke-NcdService -Arguments @("status")
        if ($r.Output -notmatch 'Status:\s*READY') { return "status output: $($r.Output)" }
        $true
    }

    Invoke-NcdTest "Service IPC ping" {
        $r = Invoke-IpcTool "ipc_ping_test" @("--once")
        if ($r.ExitCode -ne 0) { return "ipc_ping_test exit $($r.ExitCode): $($r.Output)" }
        $true
    }

    Invoke-NcdTest "Service version query" {
        $r = Invoke-IpcTool "ipc_state_test" @("--version", "--json")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        $v = ConvertFrom-NcdJsonOutput $r.StdOut
        if ($v.app_version -ne $appVersion) { return "service app_version '$($v.app_version)' != header '$appVersion'" }
        $v.protocol_version -gt 0
    }

    Invoke-NcdTest "Service state info" {
        $r = Invoke-IpcTool "ipc_state_test" @("--info", "--json")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        $s = ConvertFrom-NcdJsonOutput $r.StdOut
        if ($s.db_generation -lt 1) { return "db_generation $($s.db_generation) (no database published)" }
        if ($s.meta_generation -lt 1) { return "meta_generation $($s.meta_generation)" }
        if (-not $s.db_shm_name) { return "no db_shm_name" }
        $true
    }

    Invoke-NcdTest "Service double start" {
        $r = Invoke-NcdService -Arguments @("start", "-log2")
        if ($r.ExitCode -ne 0) { return "second start exit $($r.ExitCode): $($r.Output)" }
        if ($r.Output -notmatch 'Already running') { return "second start output: $($r.Output)" }
        Start-Sleep -Seconds 1
        $n = @(Get-NcdTestServiceProcess).Count
        if ($n -ne 1) { return "$n service processes after double start" }
        (Get-NcdServiceStatus) -eq "READY"
    }

    Invoke-NcdTest "Service stop when running (graceful)" {
        $pidBefore = @(Get-NcdTestServiceProcess)[0].Id
        $r = Invoke-NcdService -Arguments @("stop")
        if ($r.ExitCode -ne 0) { return "stop exit $($r.ExitCode): $($r.Output)" }
        if (-not (Wait-NcdServiceGone -TimeoutSeconds 10)) { return "process $pidBefore still running 10s after stop" }
        (Get-NcdServiceStatus) -eq "NOT_RUNNING"
    }

    Invoke-NcdTest "Service stop when already stopped" {
        $r = Invoke-NcdService -Arguments @("stop") -TimeoutSeconds 20
        if ($r.ExitCode -eq 124) { return "stop hung" }
        if ($r.ExitCode -eq 0) { return "stop returned 0 with no service running: $($r.Output)" }
        @(Get-NcdTestServiceProcess).Count -eq 0
    }

    Invoke-NcdTest "Service restart" {
        $r = Start-NcdTestService
        if (-not $r.Ready) { return "first start not READY: $($r.Output)" }
        Stop-NcdTestService | Out-Null
        if (@(Get-NcdTestServiceProcess).Count -ne 0) { return "service still running after stop" }
        $r = Start-NcdTestService
        if (-not $r.Ready) { return "restart not READY: $($r.Output)" }
        $p = Invoke-IpcTool "ipc_ping_test" @("--once")
        $p.ExitCode -eq 0
    }

    Invoke-NcdTest "Graceful stop persists metadata changes" {
        # Add a group while the service owns the metadata, stop the service,
        # then read the group back in standalone mode from disk.
        $target = Join-Path $paths.TestRoot "Projects\alpha"
        $g = Invoke-Ncd -Arguments @("-g", "@svcpersist") -WorkingDirectory $target
        if ($g.ExitCode -ne 0) { return "group add exit $($g.ExitCode): $($g.Output)" }
        Stop-NcdTestService | Out-Null
        if (@(Get-NcdTestServiceProcess).Count -ne 0) { return "service did not stop" }
        $l = Invoke-Ncd -Arguments @("-gl")
        if ($l.Output -notmatch 'svcpersist') { return "group missing after stop; -gl output: $($l.Output)" }
        $l.Output -match [regex]::Escape($target)
    }

    Invoke-NcdTest "Service termination (stop clears process and IPC)" {
        $r = Start-NcdTestService
        if (-not $r.Ready) { return "start not READY: $($r.Output)" }
        $s = Invoke-IpcTool "ipc_shutdown_test" @("--timeout", "10", "--verify") -TimeoutSeconds 30
        if ($s.ExitCode -ne 0) { return "ipc_shutdown_test exit $($s.ExitCode): $($s.Output)" }
        if (-not (Wait-NcdServiceGone -TimeoutSeconds 10)) { return "process still running after IPC shutdown" }
        $p = Invoke-IpcTool "ipc_ping_test" @("--once")
        if ($p.ExitCode -eq 0) { return "ping still succeeds after shutdown" }
        $true
    }

    Invoke-NcdTest "No errors in service log" {
        if (-not (Test-Path $paths.ServiceLog)) { return "service log not written: $($paths.ServiceLog)" }
        $errs = @(Get-NcdServiceLogErrors)
        if ($errs.Count -gt 0) { return ($errs | Select-Object -First 5) -join " | " }
        $true
    }

    $exit = Complete-NcdSuite
} catch {
    Write-Host "  [FAIL] Suite setup: $($_.Exception.Message)"
    Write-Host "RESULT: FAILED"
    $exit = 1
} finally {
    Remove-NcdTestEnvironment
}
exit $exit
