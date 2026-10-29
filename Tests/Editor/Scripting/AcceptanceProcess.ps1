function Stop-UrsaAcceptanceProcess {
    param([Diagnostics.Process]$Process, [Parameter(Mandatory)][string]$ExpectedExecutable)
    if (-not $Process) { return }
    try {
        if ($Process.HasExited) { return }
        if (-not [string]::Equals([IO.Path]::GetFullPath($Process.MainModule.FileName),
                [IO.Path]::GetFullPath($ExpectedExecutable), [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to close an unrelated process: $($Process.Id)"
        }
        [void]$Process.CloseMainWindow()
        if (-not $Process.WaitForExit(3000)) {
            $Process.Kill()
            if (-not $Process.WaitForExit(5000)) { throw "Acceptance process did not close: $($Process.Id)" }
        }
    } catch [ArgumentException] { }
}

function Get-UrsaStartedIde {
    param([string]$BridgeOutput)
    if ($BridgeOutput -match 'Started Visual Studio PID (\d+)') {
        return Get-Process -Id ([int]$Matches[1]) -ErrorAction SilentlyContinue
    }
    return $null
}
