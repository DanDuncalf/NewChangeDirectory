# test_ncd_windows_standalone.ps1 -- "NCD Standalone"
#
# NCD client with NO service running: everything is read from disk.
# Replaces test\Test-NCD-Windows-Standalone.bat/.ps1 (deleted/orphaned in
# bc900c1). Linux counterpart: test\test_ncd_wsl_standalone.sh.
#
# Run through the harness: python test\runner.py integration --suite ncd

$ErrorActionPreference = "Stop"
Import-Module (Join-Path $PSScriptRoot "NcdIntegration.psm1") -Force

Write-NcdHeader "NCD Client Tests - Windows (Standalone)"
$exit = 1
try {
    Initialize-NcdTestEnvironment -Name "solo"
    $paths = Get-NcdPaths
    $appVersion = Get-NcdAppVersion
    $downloads = Join-Path $paths.TestRoot "Users\scott\Downloads"
    $dbFile = Join-Path $paths.DataDir "ncd_$($paths.TestRoot.Substring(0,1).ToUpper()).database"
    Stop-NcdTestService -Quiet | Out-Null

    Invoke-NcdTest "Service is NOT running" {
        if (@(Get-NcdTestServiceProcess).Count -ne 0) { return "a service process from this build is running" }
        $s = Get-NcdServiceStatus
        if ($s -ne "NOT_RUNNING") { return "status: $s" }
        $j = (Invoke-Ncd -Arguments @("--agent:check", "--service-status", "--json")).StdOut | ConvertFrom-Json
        $j.status -eq "not_running"
    }

    Invoke-NcdTest "Rescan creates database" {
        if (Test-Path $dbFile) { Remove-Item $dbFile -Force }
        $r = Invoke-Ncd -Arguments @("-r", ".") -TimeoutSeconds 60
        if ($r.ExitCode -ne 0) { return "rescan exit $($r.ExitCode): $($r.Output)" }
        if (-not (Test-Path $dbFile)) { return "no database at $dbFile; output: $($r.Output)" }
        $r.Output -match 'Scan complete'
    }

    Invoke-NcdTest "Help with --help" {
        $r = Invoke-Ncd -Arguments @("--help")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode)" }
        if ($r.Output -notmatch 'Standalone client') { return "help header lacks '[Standalone client.]': $($r.Output.Substring(0, [Math]::Min(200, $r.Output.Length)))" }
        $r.Output -match '--agent'
    }

    Invoke-NcdTest "Version with -v" {
        $r = Invoke-Ncd -Arguments @("-v")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode)" }
        $r.Output -match ("NCD v" + [regex]::Escape($appVersion) + "\b")
    }

    Invoke-NcdTest "Basic search finds directory" {
        $r = Invoke-Ncd -Arguments @("--agent:query", "Downloads", "--limit", "1")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        if ($r.StdOut -ine $downloads) { return "got '$($r.StdOut)', expected '$downloads'" }
        $true
    }

    Invoke-NcdTest "Multi-component search" {
        $r = Invoke-Ncd -Arguments @("--agent:query", "scott\Downloads", "--limit", "1")
        if ($r.StdOut -ine $downloads) { return "got '$($r.StdOut)', expected '$downloads'" }
        $r = Invoke-Ncd -Arguments @("--agent:query", "alpha\src", "--limit", "1")
        $r.StdOut -ieq (Join-Path $paths.TestRoot "Projects\alpha\src")
    }

    Invoke-NcdTest "Agent query JSON output" {
        $r = Invoke-Ncd -Arguments @("--agent:query", "Downloads", "--json")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        $j = $r.StdOut | ConvertFrom-Json
        if ($j.query -ne "Downloads") { return "query field '$($j.query)'" }
        if (@($j.results).Count -lt 1) { return "no results" }
        $j.results[0].path -ieq $downloads
    }

    Invoke-NcdTest "Agent check database age" {
        $r = Invoke-Ncd -Arguments @("--agent:check", "--db-age")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        if ($r.StdOut -notmatch '^\d+$') { return "not a number: '$($r.StdOut)'" }
        [int]$r.StdOut -lt 600
    }

    Invoke-NcdTest "Agent check stats" {
        $r = Invoke-Ncd -Arguments @("--agent:check", "--stats")
        if ($r.ExitCode -ne 0) { return "exit $($r.ExitCode): $($r.Output)" }
        if ($r.StdOut -notmatch '([A-Za-z]):(\d+)') { return "unexpected stats: '$($r.StdOut)'" }
        [int]$Matches[2] -ge 12   # test tree root + 11 directories
    }

    Invoke-NcdTest "Rescan picks up new directory" {
        $new = Join-Path $paths.TestRoot "Data\Zebrafish"
        New-Item -ItemType Directory -Force -Path $new | Out-Null
        $before = Invoke-Ncd -Arguments @("--agent:query", "Zebrafish")
        if ($before.ExitCode -eq 0) { return "found before rescan: $($before.StdOut)" }
        $r = Invoke-Ncd -Arguments @("-r", ".") -TimeoutSeconds 60
        if ($r.ExitCode -ne 0) { return "rescan exit $($r.ExitCode)" }
        $after = Invoke-Ncd -Arguments @("--agent:query", "Zebrafish")
        $after.StdOut -ieq $new
    }

    Invoke-NcdTest "Agent tree/check accept doubled and trailing separators" {
        # Scripts often glue "X:\" + "\sub"; Windows accepts the result, so NCD must too.
        $variants = @(
            ($paths.TestRoot + "\\Projects"),
            ($paths.TestRoot + "\Projects\"),
            ($paths.TestRoot + "/Projects//"),
            ($paths.TestRoot + "\Projects\.")
        )
        foreach ($v in $variants) {
            $t = Invoke-Ncd -Arguments @("--agent:tree", $v, "--depth", "1")
            if ($t.ExitCode -ne 0 -or $t.Output -notmatch '\balpha\b') { return "tree '$v' -> exit $($t.ExitCode): $($t.Output)" }
            $c = Invoke-Ncd -Arguments @("--agent:check", $v)
            if ($c.ExitCode -ne 0) { return "check '$v' -> exit $($c.ExitCode): $($c.Output)" }
        }
        $true
    }

    Invoke-NcdTest "Agent output line endings are exactly CRLF when piped" {
        # Agents read NCD through pipes; text-mode stdout used to emit "\r\r\n".
        foreach ($a in @(@("--agent:tree", (Join-Path $paths.TestRoot "Projects"), "--depth", "2"),
                         @("--agent:query", "src", "--limit", "5"),
                         @("--agent:query", "Downloads", "--json"))) {
            $r = Invoke-Ncd -Arguments $a
            if ($r.ExitCode -ne 0) { return "$($a -join ' ') exit $($r.ExitCode)" }
            if ($r.StdOut.Contains("`r`r")) { return "$($a[0]) output contains CR CR LF" }
            if (($r.StdOut -replace "`r`n", "") -match "[`r`n]") { return "$($a[0]) output has a bare CR or LF" }
        }
        $true
    }

    Invoke-NcdTest "Search after rescan" {
        $r = Invoke-Ncd -Arguments @("--agent:query", "Projects", "--limit", "1")
        $r.StdOut -ieq (Join-Path $paths.TestRoot "Projects")
    }

    Invoke-NcdTest "No match returns error" {
        $r = Invoke-Ncd -Arguments @("NONEXISTENT_DIR_12345") -TimeoutSeconds 15
        if ($r.ExitCode -eq 0) { return "exit 0 for a search with no match" }
        if ($r.ExitCode -eq 124) { return "timed out (interactive prompt?)" }
        if ($r.Output -notmatch 'No directory found') { return "output: $($r.Output)" }
        $q = Invoke-Ncd -Arguments @("--agent:query", "NONEXISTENT_DIR_12345")
        $q.ExitCode -eq 1
    }

    Invoke-NcdAgentWriteReadTests -Mode "standalone"

    Invoke-NcdTest "Database override -d" {
        $custom = Join-Path $paths.TestData ("custom_" + (Get-Random) + ".database")
        $r = Invoke-Ncd -Arguments @("-r", ".", "-d", $custom) -TimeoutSeconds 60
        if ($r.ExitCode -ne 0) { return "rescan exit $($r.ExitCode): $($r.Output)" }
        if (-not (Test-Path $custom)) { return "custom database not created; output: $($r.Output)" }
        # The override must be readable too.
        $q = Invoke-Ncd -Arguments @("-d", $custom, "--agent:query", "Downloads", "--limit", "1")
        if ($q.StdOut -ine $downloads) { return "query via -d got '$($q.Output)'" }
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
