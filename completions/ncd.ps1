# NCD PowerShell completion script
# Add this to your PowerShell $PROFILE: . /path/to/completions/ncd.ps1

Register-ArgumentCompleter -CommandName ncd -ScriptBlock {
    param($commandName, $wordToComplete, $commandAst, $fakeBoundParameters)

    # Skip flag completion
    if ($wordToComplete -match '^[/-]') { return }

    $results = & NewChangeDirectory.exe --agent:complete $wordToComplete --limit 20 2>$null
    foreach ($line in $results) {
        if ($line.Trim()) {
            [System.Management.Automation.CompletionResult]::new(
                $line.Trim(),
                $line.Trim(),
                'ParameterValue',
                $line.Trim()
            )
        }
    }
}
