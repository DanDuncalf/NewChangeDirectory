# test_ncd_windows_with_service.ps1 -- "NCD with Service"
#
# NCD client with the service running: state comes from the service's shared
# memory. Includes a check that proves results are served by the service and
# not by the silent fallback to the on-disk database.
# Replaces test\Test-NCD-Windows-With-Service.bat (deleted in bc900c1).
# Linux counterpart: test\test_ncd_wsl_with_service.sh.
#
# Run through the harness: python test\runner.py integration --suite ncd-service

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "NcdIntegration.psm1") -Force

Write-NcdHeader "NCD Client Tests - Windows (With Service)"
$exit = 1
try {
    Initialize-NcdTestEnvironment -Name "withsvc"
    $paths = Get-NcdPaths
    $downloads = Join-Path $paths.TestRoot "Users\scott\Downloads"
    $documents = Join-Path $paths.TestRoot "Users\scott\Documents"
    $projects  = Join-Path $paths.TestRoot "Projects"
    Stop-NcdTestService -Quiet | Out-Null

    Write-Host "Performing initial scan of test tree..."
    $scan = Invoke-Ncd -Arguments @("-r", ".") -TimeoutSeconds 60
    if ($scan.ExitCode -ne 0) { throw "initial scan failed: $($scan.Output)" }

    Write-Host "Starting service..."
    $start = Start-NcdTestService
    if (-not $start.Ready) { Write-Host "  [WARN] service not READY: $($start.Output)" }
    Write-Host ""

    Invoke-NcdTest "Service is running" {
        @(Get-NcdTestServiceProcess).Count -eq 1
    }

    Invoke-NcdTest "Service status shows READY" {
        $s = Get-NcdServiceStatus
        if ($s -ne "READY") { return "status: $s" }
        $true
    }

    Invoke-NcdTest "Help shows service running" {
        $r = Invoke-Ncd -Arguments @("--help") -TimeoutSeconds 15
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode)" }
        if ($r.Output -notmatch 'Service: Running') { return "help header lacks '[Service: Running.]'" }
        $true
    }

    Invoke-NcdTest "Basic search works with service" {
        $r = Invoke-Ncd -Arguments @("--agent:query", "Downloads", "--limit", "1")
        if ($r.StdOut -ine $downloads) { return "got '$($r.Output)', expected '$downloads'" }
        $true
    }

    Invoke-NcdTest "Multi-component search with service" {
        $r = Invoke-Ncd -Arguments @("--agent:query", "scott\Downloads", "--limit", "1")
        $r.StdOut -ieq $downloads
    }

    Invoke-NcdTest "Agent query JSON with service" {
        $r = Invoke-Ncd -Arguments @("--agent:query", "Downloads", "--json")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        $j = $r.StdOut | ConvertFrom-Json
        if ($j.v -ne 1) { return "v=$($j.v)" }
        @($j.results).Count -ge 1 -and $j.results[0].path -ieq $downloads
    }

    Invoke-NcdTest "Agent check --service-status JSON" {
        $r = Invoke-Ncd -Arguments @("--agent:check", "--service-status", "--json")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode)" }
        $j = $r.StdOut | ConvertFrom-Json
        if ($j.status -ne "ready") { return "status '$($j.status)': $($r.StdOut)" }
        $true
    }

    Invoke-NcdTest "Agent check --stats with service" {
        $r = Invoke-Ncd -Arguments @("--agent:check", "--stats")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        if ($r.StdOut -notmatch '([A-Za-z]):(\d+)') { return "unexpected stats: '$($r.StdOut)'" }
        [int]$Matches[2] -ge 12
    }

    Invoke-NcdTest "Agent tree with service" {
        $r = Invoke-Ncd -Arguments @("--agent:tree", $paths.TestRoot, "--depth", "2")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        foreach ($n in @("Projects", "alpha", "Users", "scott", "Data")) {
            if ($r.Output -notmatch "\b$n\b") { return "tree lacks '$n': $($r.Output)" }
        }
        $true
    }

    Invoke-NcdTest "Agent complete with service" {
        $r = Invoke-Ncd -Arguments @("--agent:complete", "Proj")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        $r.Output -match 'Projects'
    }

    Invoke-NcdTest "Search after service restart" {
        Stop-NcdTestService | Out-Null
        $s = Start-NcdTestService
        if (-not $s.Ready) { return "service not READY after restart: $($s.Output)" }
        $r = Invoke-Ncd -Arguments @("--agent:query", "Projects", "--limit", "1")
        $r.StdOut -ieq $projects
    }

    Invoke-NcdTest "Operations work after multiple restarts" {
        for ($i = 0; $i -lt 2; $i++) {
            Stop-NcdTestService | Out-Null
            $s = Start-NcdTestService
            if (-not $s.Ready) { return "restart $($i + 1) not READY: $($s.Output)" }
        }
        $r = Invoke-Ncd -Arguments @("--agent:query", "Documents", "--limit", "1")
        $r.StdOut -ieq $documents
    }

    Invoke-NcdTest "Group added via service is visible to clients" {
        $target = Join-Path $paths.TestRoot "Projects\beta"
        $g = Invoke-Ncd -Arguments @("-g", "@withsvc") -WorkingDirectory $target
        if ($g.ExitCode -ne 0) { return "group add exit $($g.ExitCode): $($g.Output)" }
        $l = Invoke-Ncd -Arguments @("-gl")
        if ($l.Output -notmatch 'withsvc') { return "-gl output: $($l.Output)" }
        $l.Output -match [regex]::Escape($target)
    }

    Invoke-NcdTest "Plain search uses the service (works with no disk database)" {
        # Move the on-disk databases away. A standalone client would find
        # nothing, so a correct answer proves the service served it.
        if ((Get-NcdServiceStatus) -ne "READY") { return "service not READY" }
        $moved = Join-Path $paths.TestData "db_aside"
        New-Item -ItemType Directory -Force -Path $moved | Out-Null
        Get-ChildItem $paths.DataDir -Filter "*.database" | Move-Item -Destination $moved
        try {
            if (@(Get-ChildItem $paths.DataDir -Filter "*.database").Count -ne 0) { return "could not move database files" }
            $r = Invoke-NcdSearch "Downloads"
            if ($r.ExitCode -ne 0) { return "search exit $($r.ExitCode): $($r.Output)" }
            if ($r.ResultFile -notmatch [regex]::Escape($downloads)) { return "result file lacks '$downloads': $($r.ResultFile)" }
            $true
        } finally {
            Get-ChildItem $moved -Filter "*.database" | Move-Item -Destination $paths.DataDir -Force
        }
    }

    Invoke-NcdTest "Agent tree uses the service (works with no disk database)" {
        if ((Get-NcdServiceStatus) -ne "READY") { return "service not READY" }
        $moved = Join-Path $paths.TestData "db_aside"
        New-Item -ItemType Directory -Force -Path $moved | Out-Null
        Get-ChildItem $paths.DataDir -Filter "*.database" | Move-Item -Destination $moved
        try {
            $r = Invoke-Ncd -Arguments @("--agent:tree", $paths.TestRoot, "--depth", "3")
            if ($r.ExitCode -ne 0) { return "tree exit $($r.ExitCode): $($r.Output)" }
            $r.Output -match '\bDownloads\b'
        } finally {
            Get-ChildItem $moved -Filter "*.database" | Move-Item -Destination $paths.DataDir -Force
        }
    }

    Invoke-NcdAgentWriteReadTests -Mode "service"

    Invoke-NcdTest "Agent query/complete/check use the service (work with no disk database)" {
        # A standalone client reads the disk database; with it moved away only
        # the service can answer.
        if ((Get-NcdServiceStatus) -ne "READY") { return "service not READY" }
        $moved = Join-Path $paths.TestData "db_aside"
        New-Item -ItemType Directory -Force -Path $moved | Out-Null
        Get-ChildItem $paths.DataDir -Filter "*.database" | Move-Item -Destination $moved
        try {
            if (@(Get-ChildItem $paths.DataDir -Filter "*.database").Count -ne 0) { return "could not move database files" }
            $q = Invoke-Ncd -Arguments @("--agent:query", "Downloads", "--limit", "1")
            if ($q.StdOut -ine $downloads) { return "query got '$($q.Output)'" }
            $k = Invoke-Ncd -Arguments @("--agent:complete", "Down")
            if ($k.Output -notmatch 'Downloads') { return "complete got '$($k.Output)'" }
            $c = Invoke-Ncd -Arguments @("--agent:check", $downloads, "--json")
            if (-not ($c.StdOut | ConvertFrom-Json).in_db) { return "check in_db false: $($c.StdOut)" }
            $st = Invoke-Ncd -Arguments @("--agent:check", "--stats")
            if ($st.StdOut -notmatch '([A-Za-z]):(\d+)' -or [int]$Matches[2] -lt 12) { return "stats got '$($st.Output)'" }
            $true
        } finally {
            Get-ChildItem $moved -Filter "*.database" | Move-Item -Destination $paths.DataDir -Force
        }
    }

    Invoke-NcdTest "Plain search finds a directory made by agent mkdir (service)" {
        $made = Join-Path $projects "agent_search_svc"
        try {
            $m = Invoke-Ncd -Arguments @("--agent:mkdir", $made, "--json")
            if ($m.ExitCode -ne 0) { return "mkdir exit $($m.ExitCode): $($m.Output)" }
            $r = Invoke-NcdSearch "agent_search_svc"
            if ($r.ExitCode -ne 0) { return "search exit $($r.ExitCode): $($r.Output)" }
            if ($r.ResultFile -notmatch [regex]::Escape($made)) { return "result file lacks '$made': $($r.ResultFile)" }
            $true
        } finally {
            Invoke-Ncd -Arguments @("--agent:rmdir", $made) | Out-Null
            Remove-Item $made -Force -ErrorAction SilentlyContinue
        }
    }

    Invoke-NcdTest "Agent move survives the service saving its database" {
        # The service writes its in-memory database over the disk files when it
        # stops. A move made through the service must be in that database.
        $from = Join-Path $projects "agent_keep_from"
        $to   = Join-Path $projects "agent_keep_to"
        Remove-Item $from, $to -Recurse -Force -ErrorAction SilentlyContinue
        try {
            $m = Invoke-Ncd -Arguments @("--agent:mkdir", $from, "--json")
            if ($m.ExitCode -ne 0) { return "mkdir exit $($m.ExitCode): $($m.Output)" }
            $v = Invoke-Ncd -Arguments @("--agent:mv", $from, $to, "--json")
            if ($v.ExitCode -ne 0) { return "mv exit $($v.ExitCode): $($v.Output)" }
            Stop-NcdTestService | Out-Null
            if (@(Get-NcdTestServiceProcess).Count -ne 0) { return "service did not stop" }
            # Standalone now: read straight from the disk the service saved.
            $q = Invoke-Ncd -Arguments @("--agent:query", "agent_keep_to", "--limit", "1")
            if ($q.StdOut -ine $to) { return "after save, query got '$($q.Output)', expected '$to'" }
            $old = Invoke-Ncd -Arguments @("--agent:query", "agent_keep_from")
            if ($old.ExitCode -eq 0) { return "after save, old path still indexed: $($old.Output)" }
            $s = Start-NcdTestService
            if (-not $s.Ready) { return "service not READY after restart: $($s.Output)" }
            $q2 = Invoke-Ncd -Arguments @("--agent:query", "agent_keep_to", "--limit", "1")
            if ($q2.StdOut -ine $to) { return "after restart, query got '$($q2.Output)'" }
            $true
        } finally {
            Remove-Item $from, $to -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    Invoke-NcdTest "No errors in service log" {
        Stop-NcdTestService | Out-Null
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
